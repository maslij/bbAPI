#include "billing/entitlement_manager.h"
#include "logger.h"
#include <algorithm>

namespace brinkbyte {
namespace billing {

EntitlementManager::EntitlementManager(
    std::shared_ptr<BillingClient> billing_client,
    std::shared_ptr<database::RedisCache> redis_cache,
    std::shared_ptr<FeatureEntitlementRepository> entitlement_repo,
    int cache_ttl_seconds
)
    : billing_client_(billing_client),
      redis_cache_(redis_cache),
      entitlement_repo_(entitlement_repo),
      cache_ttl_seconds_(cache_ttl_seconds)
{
    initializeGrowthPackMapping();
    LOG_INFO("EntitlementManager", "Initialized with cache TTL: " + std::to_string(cache_ttl_seconds) + "s");
}

EntitlementResult EntitlementManager::checkFeatureAccess(
    const std::string& tenant_id,
    FeatureCategory category,
    const std::string& feature_name
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string category_str = featureCategoryToString(category);
    LOG_DEBUG("EntitlementManager", "Checking feature access: tenant=" + tenant_id + 
              ", category=" + category_str + ", feature=" + feature_name);
    
    // Step 1: Check cache
    auto cached_result = getCachedEntitlement(tenant_id, category, feature_name);
    if (cached_result.is_enabled || !cached_result.error_message.empty()) {
        LOG_DEBUG("EntitlementManager", "Cache hit for feature: " + feature_name);
        return cached_result;
    }
    
    // Step 2: Query billing server
    auto result = queryBillingServer(tenant_id, category, feature_name);
    
    // Step 3: Store in cache and database
    storeEntitlementInCache(tenant_id, category, feature_name, result);
    storeEntitlementInDatabase(tenant_id, category, feature_name, result);
    
    LOG_INFO("EntitlementManager", "Feature " + feature_name + " for tenant " + tenant_id + ": " + 
             (result.is_enabled ? "ENABLED" : "DISABLED"));
    
    return result;
}

bool EntitlementManager::hasGrowthPack(const std::string& tenant_id, const std::string& pack_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto enabled_packs = getEnabledGrowthPacks(tenant_id);
    return std::find(enabled_packs.begin(), enabled_packs.end(), pack_name) != enabled_packs.end();
}

std::vector<std::string> EntitlementManager::getEnabledGrowthPacks(const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check cache first
    std::string cache_key = getGrowthPackCacheKey(tenant_id);
    auto cached_json = redis_cache_->getJson(cache_key);
    
    if (cached_json.has_value() && !cached_json.value().is_null()) {
        if (cached_json.value().contains("enabled_packs") && cached_json.value()["enabled_packs"].is_array()) {
            std::vector<std::string> packs;
            for (const auto& pack : cached_json.value()["enabled_packs"]) {
                if (pack.is_string()) {
                    packs.push_back(pack.get<std::string>());
                }
            }
            return packs;
        }
    }
    
    // Query billing server
    try {
        auto response = billing_client_->getEnabledGrowthPacks(tenant_id);
        
        std::vector<std::string> packs;
        if (response.contains("enabled_packs") && response["enabled_packs"].is_array()) {
            for (const auto& pack : response["enabled_packs"]) {
                if (pack.is_string()) {
                    packs.push_back(pack.get<std::string>());
                }
            }
        }
        
        // Cache the result
        nlohmann::json cache_json;
        cache_json["enabled_packs"] = packs;
        redis_cache_->setJson(cache_key, cache_json, cache_ttl_seconds_);
        
        return packs;
        
    } catch (const std::exception& e) {
        LOG_ERROR("EntitlementManager", "Failed to get growth packs: " + std::string(e.what()));
        return {};
    }
}

bool EntitlementManager::incrementQuotaUsage(
    const std::string& tenant_id,
    FeatureCategory category,
    const std::string& feature_name,
    int amount
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string category_str = featureCategoryToString(category);
    return entitlement_repo_->incrementQuotaUsed(tenant_id, category_str, feature_name, amount);
}

int EntitlementManager::getQuotaRemaining(
    const std::string& tenant_id,
    FeatureCategory category,
    const std::string& feature_name
) {
    auto result = checkFeatureAccess(tenant_id, category, feature_name);
    return result.quota_remaining;
}

bool EntitlementManager::syncEntitlements(const std::string& tenant_id, bool force_refresh) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Get enabled growth packs
        auto packs = getEnabledGrowthPacks(tenant_id);
        
        LOG_INFO("EntitlementManager", "Synced " + std::to_string(packs.size()) + 
                 " growth packs for tenant: " + tenant_id);
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("EntitlementManager", "Failed to sync entitlements: " + std::string(e.what()));
        return false;
    }
}

std::vector<FeatureEntitlement> EntitlementManager::getTenantEntitlements(const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return entitlement_repo_->findByTenant(tenant_id);
}

int EntitlementManager::clearStaleEntitlements(int minutes_threshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    return entitlement_repo_->clearStale(minutes_threshold);
}

bool EntitlementManager::isCVModelAllowed(const std::string& tenant_id, const std::string& model_name) {
    auto result = checkFeatureAccess(tenant_id, FeatureCategory::CV_MODELS, model_name);
    return result.is_enabled;
}

bool EntitlementManager::isAnalyticsAllowed(const std::string& tenant_id, const std::string& analytics_type) {
    auto result = checkFeatureAccess(tenant_id, FeatureCategory::ANALYTICS, analytics_type);
    return result.is_enabled;
}

bool EntitlementManager::isOutputAllowed(const std::string& tenant_id, const std::string& output_type) {
    auto result = checkFeatureAccess(tenant_id, FeatureCategory::OUTPUTS, output_type);
    return result.is_enabled;
}

// Static methods

std::string EntitlementManager::featureCategoryToString(FeatureCategory category) {
    switch (category) {
        case FeatureCategory::CV_MODELS: return "cv_models";
        case FeatureCategory::ANALYTICS: return "analytics";
        case FeatureCategory::OUTPUTS: return "outputs";
        case FeatureCategory::STORAGE: return "storage";
        case FeatureCategory::LLM_SEATS: return "llm_seats";
        case FeatureCategory::AGENTS: return "agents";
        case FeatureCategory::API_CALLS: return "api_calls";
        case FeatureCategory::INTEGRATIONS: return "integrations";
        default: return "unknown";
    }
}

EntitlementManager::FeatureCategory EntitlementManager::stringToFeatureCategory(const std::string& category_str) {
    if (category_str == "cv_models") return FeatureCategory::CV_MODELS;
    if (category_str == "analytics") return FeatureCategory::ANALYTICS;
    if (category_str == "outputs") return FeatureCategory::OUTPUTS;
    if (category_str == "storage") return FeatureCategory::STORAGE;
    if (category_str == "llm_seats") return FeatureCategory::LLM_SEATS;
    if (category_str == "agents") return FeatureCategory::AGENTS;
    if (category_str == "api_calls") return FeatureCategory::API_CALLS;
    if (category_str == "integrations") return FeatureCategory::INTEGRATIONS;
    return FeatureCategory::CV_MODELS;  // Default
}

// Private methods

std::string EntitlementManager::getEntitlementCacheKey(
    const std::string& tenant_id,
    const std::string& category,
    const std::string& feature_name
) const {
    return "entitlement:" + tenant_id + ":" + category + ":" + feature_name;
}

std::string EntitlementManager::getGrowthPackCacheKey(const std::string& tenant_id) const {
    return "growth_packs:" + tenant_id;
}

void EntitlementManager::initializeGrowthPackMapping() {
    // Map growth packs to features
    growth_pack_features_["Advanced Analytics"] = {
        "heatmap", "line_crossing", "dwell_time", "crowd_density", 
        "custom_reports", "historical_analysis"
    };
    
    growth_pack_features_["Active Transport"] = {
        "pedestrian_detection", "cyclist_detection", "escooter_detection",
        "movement_patterns", "speed_analysis"
    };
    
    growth_pack_features_["Cloud Storage"] = {
        "cloud_backup", "extended_retention", "encrypted_storage"
    };
    
    growth_pack_features_["API Integration"] = {
        "unlimited_api", "webhooks", "custom_integrations", "priority_support"
    };
    
    LOG_DEBUG("EntitlementManager", "Initialized " + std::to_string(growth_pack_features_.size()) + 
              " growth pack mappings");
}

EntitlementResult EntitlementManager::queryBillingServer(
    const std::string& tenant_id,
    FeatureCategory category,
    const std::string& feature_name
) {
    try {
        nlohmann::json request;
        request["tenant_id"] = tenant_id;
        request["feature_category"] = featureCategoryToString(category);
        request["feature_name"] = feature_name;
        
        auto response = billing_client_->checkEntitlement(request);
        
        EntitlementResult result;
        result.is_enabled = response.value("is_enabled", false);
        result.quota_limit = response.value("quota_limit", -1);
        result.quota_used = response.value("quota_used", 0);
        result.quota_remaining = response.value("quota_remaining", result.quota_limit);
        
        // Parse valid_until
        if (response.contains("valid_until") && response["valid_until"].is_string()) {
            // For simplicity, set to 30 days from now
            result.valid_until = std::chrono::system_clock::now() + std::chrono::hours(24 * 30);
        } else {
            result.valid_until = std::chrono::system_clock::now() + std::chrono::hours(24 * 30);
        }
        
        return result;
        
    } catch (const std::exception& e) {
        LOG_ERROR("EntitlementManager", "Billing server query failed: " + std::string(e.what()));
        
        EntitlementResult result;
        result.is_enabled = false;
        result.quota_limit = 0;
        result.quota_used = 0;
        result.quota_remaining = 0;
        result.error_message = e.what();
        
        return result;
    }
}

void EntitlementManager::storeEntitlementInCache(
    const std::string& tenant_id,
    FeatureCategory category,
    const std::string& feature_name,
    const EntitlementResult& result
) {
    std::string cache_key = getEntitlementCacheKey(tenant_id, featureCategoryToString(category), feature_name);
    
    nlohmann::json cache_json;
    cache_json["is_enabled"] = result.is_enabled;
    cache_json["quota_limit"] = result.quota_limit;
    cache_json["quota_used"] = result.quota_used;
    cache_json["quota_remaining"] = result.quota_remaining;
    
    redis_cache_->setJson(cache_key, cache_json, cache_ttl_seconds_);
    
    LOG_DEBUG("EntitlementManager", "Stored entitlement in cache: " + cache_key);
}

void EntitlementManager::storeEntitlementInDatabase(
    const std::string& tenant_id,
    FeatureCategory category,
    const std::string& feature_name,
    const EntitlementResult& result
) {
    std::string category_str = featureCategoryToString(category);
    
    // Check if entitlement already exists
    auto existing = entitlement_repo_->findByTenantAndFeature(tenant_id, category_str, feature_name);
    
    FeatureEntitlement entitlement;
    entitlement.tenant_id = tenant_id;
    entitlement.feature_category = category_str;
    entitlement.feature_name = feature_name;
    entitlement.is_enabled = result.is_enabled;
    entitlement.quota_limit = result.quota_limit;
    entitlement.quota_used = result.quota_used;
    entitlement.valid_until = std::chrono::system_clock::to_time_t(result.valid_until);
    entitlement.last_checked = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    entitlement.created_at = entitlement.last_checked;
    entitlement.updated_at = entitlement.last_checked;
    
    if (existing.has_value()) {
        // Update existing
        entitlement_repo_->update(entitlement);
    } else {
        // Save new
        entitlement_repo_->save(entitlement);
    }
    
    LOG_DEBUG("EntitlementManager", "Stored entitlement in database: " + tenant_id + "/" + feature_name);
}

EntitlementResult EntitlementManager::getCachedEntitlement(
    const std::string& tenant_id,
    FeatureCategory category,
    const std::string& feature_name
) {
    // Try Redis cache first
    std::string cache_key = getEntitlementCacheKey(tenant_id, featureCategoryToString(category), feature_name);
    auto cached_json = redis_cache_->getJson(cache_key);
    
    if (cached_json.has_value() && !cached_json.value().is_null()) {
        EntitlementResult result;
        result.is_enabled = cached_json.value().value("is_enabled", false);
        result.quota_limit = cached_json.value().value("quota_limit", -1);
        result.quota_used = cached_json.value().value("quota_used", 0);
        result.quota_remaining = cached_json.value().value("quota_remaining", result.quota_limit);
        return result;
    }
    
    // Try database
    std::string category_str = featureCategoryToString(category);
    auto entitlement_opt = entitlement_repo_->findByTenantAndFeature(tenant_id, category_str, feature_name);
    
    if (entitlement_opt.has_value()) {
        const auto& ent = entitlement_opt.value();
        
        EntitlementResult result;
        result.is_enabled = ent.is_enabled;
        result.quota_limit = ent.quota_limit;
        result.quota_used = ent.quota_used;
        result.quota_remaining = ent.quota_limit == -1 ? -1 : (ent.quota_limit - ent.quota_used);
        result.valid_until = std::chrono::system_clock::from_time_t(ent.valid_until);
        
        return result;
    }
    
    // No cached data
    EntitlementResult result;
    result.is_enabled = false;
    result.quota_limit = 0;
    result.quota_used = 0;
    result.quota_remaining = 0;
    
    return result;
}

} // namespace billing
} // namespace brinkbyte


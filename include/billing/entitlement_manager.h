#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include "billing_client.h"
#include "database/redis_cache.h"
#include "database/postgres_connection.h"
#include "billing/repository.h"

namespace brinkbyte {
namespace billing {

// Feature categories for entitlement checking
enum class FeatureCategory {
    CV_MODELS,          // Object detection models
    ANALYTICS,          // Advanced analytics
    OUTPUTS,            // Alert outputs (SMS, webhook, etc.)
    STORAGE,            // Cloud storage
    LLM_SEATS,          // LLM agent seats
    AGENTS,             // AI agents
    API_CALLS,          // API access
    INTEGRATIONS        // Third-party integrations
};

// Entitlement check result
struct EntitlementResult {
    bool is_enabled;
    int quota_limit;        // -1 for unlimited, 0 for disabled
    int quota_used;
    int quota_remaining;    // Calculated: limit - used
    std::chrono::system_clock::time_point valid_until;
    std::string error_message;
};

/**
 * @brief Manages feature entitlements based on growth packs
 * 
 * Features:
 * - Check if a tenant has access to specific features
 * - Cache entitlements locally (5 minute TTL by default)
 * - Track quota usage (API calls, LLM tokens, etc.)
 * - Refresh entitlements periodically from billing server
 * - Map growth packs to feature access
 */
class EntitlementManager {
public:
    /**
     * @brief Constructor
     * @param billing_client HTTP client for billing server communication
     * @param redis_cache Redis cache for entitlement caching
     * @param entitlement_repo Repository for entitlement persistence
     * @param cache_ttl_seconds Cache TTL in seconds (default: 300 = 5 minutes)
     */
    explicit EntitlementManager(
        std::shared_ptr<BillingClient> billing_client,
        std::shared_ptr<tapi::database::RedisCache> redis_cache,
        std::shared_ptr<FeatureEntitlementRepository> entitlement_repo,
        int cache_ttl_seconds = 300  // 5 minutes default
    );
    
    ~EntitlementManager() = default;
    
    /**
     * @brief Check if a feature is enabled for a tenant
     * 
     * Flow:
     * 1. Check Redis cache for recent entitlement
     * 2. If cache miss, query billing server
     * 3. Store result in cache and PostgreSQL
     * 
     * @param tenant_id Tenant identifier
     * @param category Feature category
     * @param feature_name Specific feature name
     * @return EntitlementResult Entitlement check result
     */
    EntitlementResult checkFeatureAccess(
        const std::string& tenant_id,
        FeatureCategory category,
        const std::string& feature_name
    );
    
    /**
     * @brief Check if a tenant has a specific growth pack enabled
     * @param tenant_id Tenant identifier
     * @param pack_name Growth pack name (e.g., "Advanced Analytics")
     * @return true if pack is enabled
     */
    bool hasGrowthPack(const std::string& tenant_id, const std::string& pack_name);
    
    /**
     * @brief Get all enabled growth packs for a tenant
     * @param tenant_id Tenant identifier
     * @return Vector of growth pack names
     */
    std::vector<std::string> getEnabledGrowthPacks(const std::string& tenant_id);
    
    /**
     * @brief Increment quota usage for a feature
     * @param tenant_id Tenant identifier
     * @param category Feature category
     * @param feature_name Specific feature name
     * @param amount Amount to increment
     * @return true if increment successful
     */
    bool incrementQuotaUsage(
        const std::string& tenant_id,
        FeatureCategory category,
        const std::string& feature_name,
        int amount = 1
    );
    
    /**
     * @brief Get quota remaining for a feature
     * @param tenant_id Tenant identifier
     * @param category Feature category
     * @param feature_name Specific feature name
     * @return Remaining quota (-1 for unlimited, 0 for exhausted)
     */
    int getQuotaRemaining(
        const std::string& tenant_id,
        FeatureCategory category,
        const std::string& feature_name
    );
    
    /**
     * @brief Sync entitlements from billing server
     * @param tenant_id Tenant identifier
     * @param force_refresh Skip cache and force re-sync
     * @return true if sync successful
     */
    bool syncEntitlements(const std::string& tenant_id, bool force_refresh = false);
    
    /**
     * @brief Get all entitlements for a tenant
     * @param tenant_id Tenant identifier
     * @return Vector of feature entitlements
     */
    std::vector<FeatureEntitlement> getTenantEntitlements(const std::string& tenant_id);
    
    /**
     * @brief Clear stale entitlements from cache and database
     * @param minutes_threshold Age threshold in minutes
     * @return Number of entitlements cleared
     */
    int clearStaleEntitlements(int minutes_threshold = 30);
    
    /**
     * @brief Check if a specific CV model is allowed
     * @param tenant_id Tenant identifier
     * @param model_name Model name (e.g., "yolov7", "person_detection")
     * @return true if model is allowed
     */
    bool isCVModelAllowed(const std::string& tenant_id, const std::string& model_name);
    
    /**
     * @brief Check if analytics feature is allowed
     * @param tenant_id Tenant identifier
     * @param analytics_type Type of analytics (e.g., "heatmap", "line_crossing")
     * @return true if analytics is allowed
     */
    bool isAnalyticsAllowed(const std::string& tenant_id, const std::string& analytics_type);
    
    /**
     * @brief Check if output type is allowed
     * @param tenant_id Tenant identifier
     * @param output_type Type of output (e.g., "sms", "webhook", "email")
     * @return true if output is allowed
     */
    bool isOutputAllowed(const std::string& tenant_id, const std::string& output_type);

    /**
     * @brief Convert feature category enum to string
     * @param category Feature category enum
     * @return String representation
     */
    static std::string featureCategoryToString(FeatureCategory category);
    
    /**
     * @brief Convert string to feature category enum
     * @param category_str String representation
     * @return Feature category enum
     */
    static FeatureCategory stringToFeatureCategory(const std::string& category_str);

private:
    std::shared_ptr<BillingClient> billing_client_;
    std::shared_ptr<tapi::database::RedisCache> redis_cache_;
    std::shared_ptr<FeatureEntitlementRepository> entitlement_repo_;
    
    int cache_ttl_seconds_;
    
    // Growth pack to feature mapping
    std::map<std::string, std::vector<std::string>> growth_pack_features_;
    
    mutable std::mutex mutex_;
    
    // Cache key generators
    std::string getEntitlementCacheKey(
        const std::string& tenant_id,
        const std::string& category,
        const std::string& feature_name
    ) const;
    
    std::string getGrowthPackCacheKey(const std::string& tenant_id) const;
    
    // Helper methods
    void initializeGrowthPackMapping();
    EntitlementResult queryBillingServer(
        const std::string& tenant_id,
        FeatureCategory category,
        const std::string& feature_name
    );
    void storeEntitlementInCache(
        const std::string& tenant_id,
        FeatureCategory category,
        const std::string& feature_name,
        const EntitlementResult& result
    );
    void storeEntitlementInDatabase(
        const std::string& tenant_id,
        FeatureCategory category,
        const std::string& feature_name,
        const EntitlementResult& result
    );
    EntitlementResult getCachedEntitlement(
        const std::string& tenant_id,
        FeatureCategory category,
        const std::string& feature_name
    );
};

} // namespace billing
} // namespace brinkbyte


#include "billing/license_validator.h"
#include "logger.h"
#include <sstream>

namespace brinkbyte {
namespace billing {

LicenseValidator::LicenseValidator(
    std::shared_ptr<BillingClient> billing_client,
    std::shared_ptr<database::RedisCache> redis_cache,
    std::shared_ptr<CameraLicenseRepository> license_repo,
    int cache_ttl_seconds
)
    : billing_client_(billing_client),
      redis_cache_(redis_cache),
      license_repo_(license_repo),
      cache_ttl_seconds_(cache_ttl_seconds),
      degraded_mode_(false),
      last_sync_time_(std::chrono::system_clock::now())
{
    LOG_INFO("LicenseValidator", "Initialized with cache TTL: " + std::to_string(cache_ttl_seconds) + "s");
}

LicenseValidationResult LicenseValidator::validateCameraLicense(
    const std::string& camera_id,
    const std::string& tenant_id,
    bool force_refresh
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_DEBUG("LicenseValidator", "Validating license for camera: " + camera_id + ", tenant: " + tenant_id);
    
    // Step 1: Check cache if not forcing refresh
    if (!force_refresh) {
        std::string cache_key = getCacheKey(camera_id);
        auto cached_json = redis_cache_->getJson(cache_key);
        
        if (cached_json.has_value() && !cached_json.value().is_null()) {
            LOG_DEBUG("LicenseValidator", "Cache hit for camera: " + camera_id);
            return parseValidationResponse(cached_json.value());
        }
        
        LOG_DEBUG("LicenseValidator", "Cache miss for camera: " + camera_id);
    }
    
    // Step 2: Query billing server
    try {
        nlohmann::json request;
        request["camera_id"] = camera_id;
        request["tenant_id"] = tenant_id;
        
        auto response = billing_client_->validateCameraLicense(request);
        
        if (response.contains("is_valid")) {
            // Parse and cache the result
            auto result = parseValidationResponse(response);
            
            // Store in cache and database
            storeLicenseInCache(camera_id, result);
            storeLicenseInDatabase(camera_id, tenant_id, result);
            
            // Update last sync time and clear degraded mode
            last_sync_time_ = std::chrono::system_clock::now();
            setDegradedMode(false);
            
            LOG_INFO("LicenseValidator", "Validated camera " + camera_id + ": " + 
                     (result.is_valid ? "VALID" : "INVALID") + " (" + result.license_mode + ")");
            
            return result;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("LicenseValidator", "Billing server query failed: " + std::string(e.what()));
    }
    
    // Step 3: Billing server offline - use cached/degraded mode
    LOG_WARN("LicenseValidator", "Billing server unreachable, using degraded mode");
    setDegradedMode(true);
    return handleOfflineValidation(camera_id, tenant_id);
}

bool LicenseValidator::canAddCamera(const std::string& tenant_id, int current_camera_count) {
    int limit = getCameraLimit(tenant_id);
    
    if (limit == -1) {
        // Unlimited cameras
        return true;
    }
    
    if (limit == 0) {
        // No cameras allowed
        return false;
    }
    
    return current_camera_count < limit;
}

int LicenseValidator::getCameraLimit(const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Query billing server for license status
        auto response = billing_client_->getLicenseStatus(tenant_id);
        
        if (response.contains("license_mode")) {
            std::string mode = response["license_mode"];
            
            if (mode == "trial") {
                // Trial mode has a limit
                return response.value("trial_max_cameras", 2);
            } else if (mode == "base") {
                // Base license is unlimited
                return -1;
            } else {
                // Unlicensed
                return 0;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("LicenseValidator", "Failed to get camera limit: " + std::string(e.what()));
    }
    
    // Default to trial limit if billing server is unavailable
    return 2;
}

bool LicenseValidator::syncLicenseFromBillingServer(
    const std::string& camera_id,
    const std::string& tenant_id
) {
    // Force refresh from billing server
    auto result = validateCameraLicense(camera_id, tenant_id, true);
    return result.is_valid;
}

LicenseValidationResult LicenseValidator::getCachedLicense(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Try Redis cache first
    std::string cache_key = getCacheKey(camera_id);
    auto cached_json = redis_cache_->getJson(cache_key);
    
    if (cached_json.has_value() && !cached_json.value().is_null()) {
        return parseValidationResponse(cached_json.value());
    }
    
    // Fall back to PostgreSQL
    auto license_opt = license_repo_->findById(camera_id);
    if (license_opt.has_value()) {
        const auto& license = license_opt.value();
        
        LicenseValidationResult result;
        result.is_valid = license.is_valid;
        result.license_mode = license.license_mode;
        result.enabled_growth_packs = license.enabled_growth_packs;
        result.valid_until = std::chrono::system_clock::from_time_t(license.valid_until);
        result.cameras_allowed = -1;  // Unknown from database
        
        return result;
    }
    
    // No cached data available
    LicenseValidationResult result;
    result.is_valid = false;
    result.license_mode = "unlicensed";
    result.error_message = "No cached license data available";
    
    return result;
}

bool LicenseValidator::revokeLicense(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remove from cache
    std::string cache_key = getCacheKey(camera_id);
    // Note: RedisCache doesn't have a delete method yet, would need to add it
    
    // Remove from database
    return license_repo_->remove(camera_id);
}

std::vector<CameraLicense> LicenseValidator::getTenantLicenses(const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return license_repo_->findByTenant(tenant_id);
}

void LicenseValidator::updateValidationTimestamp(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    license_repo_->updateValidationTime(camera_id);
}

bool LicenseValidator::isDegradedMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return degraded_mode_;
}

std::chrono::seconds LicenseValidator::getTimeSinceLastSync() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - last_sync_time_);
}

// Private methods

std::string LicenseValidator::getCacheKey(const std::string& camera_id) const {
    return "license:camera:" + camera_id;
}

std::string LicenseValidator::getTenantCacheKey(const std::string& tenant_id) const {
    return "license:tenant:" + tenant_id;
}

LicenseValidationResult LicenseValidator::parseValidationResponse(const nlohmann::json& response) {
    LicenseValidationResult result;
    
    result.is_valid = response.value("is_valid", false);
    result.license_mode = response.value("license_mode", "unlicensed");
    
    // Parse growth packs
    if (response.contains("enabled_growth_packs") && response["enabled_growth_packs"].is_array()) {
        for (const auto& pack : response["enabled_growth_packs"]) {
            if (pack.is_string()) {
                result.enabled_growth_packs.push_back(pack.get<std::string>());
            }
        }
    }
    
    // Parse valid_until
    if (response.contains("valid_until") && response["valid_until"].is_string()) {
        // Parse ISO 8601 date string
        std::string date_str = response["valid_until"];
        // For simplicity, set to 1 year from now (proper parsing would be better)
        result.valid_until = std::chrono::system_clock::now() + std::chrono::hours(24 * 365);
    } else {
        result.valid_until = std::chrono::system_clock::now() + std::chrono::hours(24 * 365);
    }
    
    // Parse cameras_allowed
    if (response.contains("cameras_allowed")) {
        if (response["cameras_allowed"].is_null()) {
            result.cameras_allowed = -1;  // Unlimited
        } else if (response["cameras_allowed"].is_number()) {
            result.cameras_allowed = response["cameras_allowed"];
        }
    } else {
        result.cameras_allowed = -1;
    }
    
    return result;
}

void LicenseValidator::storeLicenseInCache(
    const std::string& camera_id,
    const LicenseValidationResult& result
) {
    std::string cache_key = getCacheKey(camera_id);
    
    // Create JSON representation
    nlohmann::json cache_json;
    cache_json["is_valid"] = result.is_valid;
    cache_json["license_mode"] = result.license_mode;
    cache_json["enabled_growth_packs"] = result.enabled_growth_packs;
    if (result.cameras_allowed == -1) {
        cache_json["cameras_allowed"] = nullptr;
    } else {
        cache_json["cameras_allowed"] = result.cameras_allowed;
    }
    
    // Store with TTL
    redis_cache_->setJson(cache_key, cache_json, cache_ttl_seconds_);
    
    LOG_DEBUG("LicenseValidator", "Stored license in cache for camera: " + camera_id);
}

void LicenseValidator::storeLicenseInDatabase(
    const std::string& camera_id,
    const std::string& tenant_id,
    const LicenseValidationResult& result
) {
    CameraLicense license;
    license.camera_id = camera_id;
    license.tenant_id = tenant_id;
    license.device_id = "";  // TODO: Get device ID from config
    license.license_mode = result.license_mode;
    license.is_valid = result.is_valid;
    license.valid_until = std::chrono::system_clock::to_time_t(result.valid_until);
    license.enabled_growth_packs = result.enabled_growth_packs;
    license.last_validated = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    license.created_at = license.last_validated;
    license.updated_at = license.last_validated;
    
    // Try to update first, if that fails, save new
    if (!license_repo_->update(license)) {
        license_repo_->save(license);
    }
    
    LOG_DEBUG("LicenseValidator", "Stored license in database for camera: " + camera_id);
}

LicenseValidationResult LicenseValidator::handleOfflineValidation(
    const std::string& camera_id,
    const std::string& tenant_id
) {
    LOG_WARN("LicenseValidator", "Billing server offline, using cached data for camera: " + camera_id);
    
    // Try to get from cache or database
    auto cached_result = getCachedLicense(camera_id);
    
    if (cached_result.is_valid) {
        // Check if cached license is still valid (not expired)
        auto now = std::chrono::system_clock::now();
        if (now < cached_result.valid_until) {
            LOG_INFO("LicenseValidator", "Using cached valid license for camera: " + camera_id);
            cached_result.error_message = "Degraded mode: using cached license";
            return cached_result;
        } else {
            LOG_WARN("LicenseValidator", "Cached license expired for camera: " + camera_id);
            cached_result.is_valid = false;
            cached_result.error_message = "Cached license expired";
            return cached_result;
        }
    }
    
    // No valid cached license - return invalid
    LOG_ERROR("LicenseValidator", "No valid cached license for camera: " + camera_id);
    
    LicenseValidationResult result;
    result.is_valid = false;
    result.license_mode = "unlicensed";
    result.error_message = "No cached license available and billing server offline";
    
    return result;
}

void LicenseValidator::setDegradedMode(bool degraded) {
    if (degraded != degraded_mode_) {
        degraded_mode_ = degraded;
        if (degraded) {
            LOG_WARN("LicenseValidator", "Entering degraded mode (billing server offline)");
        } else {
            LOG_INFO("LicenseValidator", "Exiting degraded mode (billing server reconnected)");
        }
    }
}

} // namespace billing
} // namespace brinkbyte


#pragma once

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include "billing_client.h"
#include "database/redis_cache.h"
#include "database/postgres_connection.h"
#include "billing/repository.h"

namespace brinkbyte {
namespace billing {

// License validation result
struct LicenseValidationResult {
    bool is_valid;
    std::string license_mode;  // trial, base, unlicensed
    std::vector<std::string> enabled_growth_packs;
    std::chrono::system_clock::time_point valid_until;
    int cameras_allowed;  // -1 for unlimited
    std::string error_message;
};

/**
 * @brief Validates camera licenses against the billing server
 * 
 * Features:
 * - Validates camera licenses with the billing server
 * - Caches validation results in Redis (1 hour TTL)
 * - Stores licenses in PostgreSQL
 * - Graceful degradation when billing server is offline
 * - Trial vs. Base license enforcement
 * - Growth pack entitlement tracking
 */
class LicenseValidator {
public:
    /**
     * @brief Constructor
     * @param billing_client HTTP client for billing server communication
     * @param redis_cache Redis cache for validation caching
     * @param license_repo Repository for license persistence
     * @param cache_ttl_seconds Cache TTL in seconds (default: 3600 = 1 hour)
     */
    explicit LicenseValidator(
        std::shared_ptr<BillingClient> billing_client,
        std::shared_ptr<tapi::database::RedisCache> redis_cache,
        std::shared_ptr<CameraLicenseRepository> license_repo,
        int cache_ttl_seconds = 3600
    );
    
    ~LicenseValidator() = default;
    
    /**
     * @brief Validate a camera license
     * 
     * Flow:
     * 1. Check Redis cache for recent validation
     * 2. If cache miss, query billing server
     * 3. Store result in cache and PostgreSQL
     * 4. If billing server offline, use cached data with degraded mode
     * 
     * @param camera_id Camera identifier
     * @param tenant_id Tenant identifier
     * @param force_refresh Skip cache and force re-validation
     * @return LicenseValidationResult Validation result
     */
    LicenseValidationResult validateCameraLicense(
        const std::string& camera_id,
        const std::string& tenant_id,
        bool force_refresh = false
    );
    
    /**
     * @brief Check if a tenant can add more cameras
     * @param tenant_id Tenant identifier
     * @param current_camera_count Current active camera count
     * @return true if tenant can add more cameras, false otherwise
     */
    bool canAddCamera(const std::string& tenant_id, int current_camera_count);
    
    /**
     * @brief Get the maximum cameras allowed for a tenant
     * @param tenant_id Tenant identifier
     * @return Camera limit (-1 for unlimited, 0 for no access, positive for limit)
     */
    int getCameraLimit(const std::string& tenant_id);
    
    /**
     * @brief Sync license from billing server to local database
     * @param camera_id Camera identifier
     * @param tenant_id Tenant identifier
     * @return true if sync successful, false otherwise
     */
    bool syncLicenseFromBillingServer(
        const std::string& camera_id,
        const std::string& tenant_id
    );
    
    /**
     * @brief Get license status from cache or database (offline mode)
     * @param camera_id Camera identifier
     * @return LicenseValidationResult Cached validation result
     */
    LicenseValidationResult getCachedLicense(const std::string& camera_id);
    
    /**
     * @brief Remove a camera license from cache and database
     * @param camera_id Camera identifier
     * @return true if removed successfully
     */
    bool revokeLicense(const std::string& camera_id);
    
    /**
     * @brief Get all licenses for a tenant
     * @param tenant_id Tenant identifier
     * @return Vector of camera licenses
     */
    std::vector<CameraLicense> getTenantLicenses(const std::string& tenant_id);
    
    /**
     * @brief Update validation timestamp for a camera
     * @param camera_id Camera identifier
     */
    void updateValidationTimestamp(const std::string& camera_id);
    
    /**
     * @brief Check if graceful degradation is active
     * @return true if operating in degraded mode (offline)
     */
    bool isDegradedMode() const;
    
    /**
     * @brief Get time since last successful billing server contact
     * @return Duration since last contact
     */
    std::chrono::seconds getTimeSinceLastSync() const;

private:
    std::shared_ptr<BillingClient> billing_client_;
    std::shared_ptr<tapi::database::RedisCache> redis_cache_;
    std::shared_ptr<CameraLicenseRepository> license_repo_;
    
    int cache_ttl_seconds_;
    bool degraded_mode_;
    std::chrono::system_clock::time_point last_sync_time_;
    
    mutable std::mutex mutex_;
    
    // Cache key generators
    std::string getCacheKey(const std::string& camera_id) const;
    std::string getTenantCacheKey(const std::string& tenant_id) const;
    
    // Validation helpers
    LicenseValidationResult parseValidationResponse(const nlohmann::json& response);
    void storeLicenseInCache(const std::string& camera_id, const LicenseValidationResult& result);
    void storeLicenseInDatabase(
        const std::string& camera_id,
        const std::string& tenant_id,
        const LicenseValidationResult& result
    );
    
    // Degraded mode helpers
    LicenseValidationResult handleOfflineValidation(
        const std::string& camera_id,
        const std::string& tenant_id
    );
    void setDegradedMode(bool degraded);
};

} // namespace billing
} // namespace brinkbyte


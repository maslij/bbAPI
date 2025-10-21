#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "database/postgres_connection.h"

namespace brinkbyte {
namespace billing {

// Forward declarations
struct EdgeDevice;
struct CameraLicense;
struct FeatureEntitlement;
struct UsageEvent;
struct BillingSyncStatus;

// Edge device entity
struct EdgeDevice {
    std::string device_id;
    std::string tenant_id;
    std::string management_tier;  // basic, advanced, managed
    time_t last_heartbeat;
    std::string status;           // active, offline, suspended
    int active_camera_count;
    time_t created_at;
    time_t updated_at;
};

// Camera license entity
struct CameraLicense {
    std::string camera_id;
    std::string tenant_id;
    std::string device_id;
    std::string license_mode;       // trial, base
    bool is_valid;
    time_t valid_until;
    std::vector<std::string> enabled_growth_packs;
    time_t last_validated;
    time_t created_at;
    time_t updated_at;
};

// Feature entitlement entity
struct FeatureEntitlement {
    std::string tenant_id;
    std::string feature_category;  // cv_models, analytics, outputs, etc.
    std::string feature_name;
    bool is_enabled;
    int quota_limit;
    int quota_used;
    time_t valid_until;
    time_t last_checked;
    time_t created_at;
    time_t updated_at;
};

// Usage event entity
struct UsageEvent {
    std::string event_id;
    std::string tenant_id;
    std::string device_id;
    std::string camera_id;
    std::string event_type;  // api_call, llm_tokens, storage_gb_days, etc.
    double quantity;
    std::string unit;
    std::string metadata_json;
    time_t event_time;
    bool synced;
};

// Billing sync status entity
struct BillingSyncStatus {
    std::string sync_id;
    std::string tenant_id;
    std::string sync_type;  // usage, license, heartbeat
    time_t last_sync_time;
    time_t next_sync_time;
    std::string status;     // success, failed, pending
    int events_synced;
    std::string error_message;
    time_t created_at;
};

/**
 * Repository for edge device operations
 */
class EdgeDeviceRepository {
public:
    explicit EdgeDeviceRepository(std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool);
    
    // CRUD operations
    std::optional<EdgeDevice> findById(const std::string& device_id);
    std::optional<EdgeDevice> findByTenantAndDevice(const std::string& tenant_id, const std::string& device_id);
    bool save(const EdgeDevice& device);
    bool updateHeartbeat(const std::string& device_id, int active_cameras);
    bool updateStatus(const std::string& device_id, const std::string& status);
    
    // Query operations
    std::vector<EdgeDevice> findByTenant(const std::string& tenant_id);
    std::vector<EdgeDevice> findInactive(int minutes_threshold);
    int countByTenant(const std::string& tenant_id);
    
private:
    std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool_;
};

/**
 * Repository for camera license operations
 */
class CameraLicenseRepository {
public:
    explicit CameraLicenseRepository(std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool);
    
    // CRUD operations
    std::optional<CameraLicense> findById(const std::string& camera_id);
    std::optional<CameraLicense> findByTenantAndCamera(const std::string& tenant_id, const std::string& camera_id);
    bool save(const CameraLicense& license);
    bool update(const CameraLicense& license);
    bool remove(const std::string& camera_id);
    
    // Query operations
    std::vector<CameraLicense> findByTenant(const std::string& tenant_id);
    std::vector<CameraLicense> findExpired();
    std::vector<CameraLicense> findExpiringSoon(int days_threshold);
    int countValidByTenant(const std::string& tenant_id);
    int countByMode(const std::string& tenant_id, const std::string& mode);
    
    // Cache operations
    bool updateValidationTime(const std::string& camera_id);
    std::vector<CameraLicense> findStale(int minutes_threshold);
    
private:
    std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool_;
};

/**
 * Repository for feature entitlement operations
 */
class FeatureEntitlementRepository {
public:
    explicit FeatureEntitlementRepository(std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool);
    
    // CRUD operations
    std::optional<FeatureEntitlement> findByTenantAndFeature(
        const std::string& tenant_id,
        const std::string& feature_category,
        const std::string& feature_name
    );
    bool save(const FeatureEntitlement& entitlement);
    bool update(const FeatureEntitlement& entitlement);
    bool incrementQuotaUsed(const std::string& tenant_id, const std::string& feature_category, 
                           const std::string& feature_name, int amount);
    
    // Query operations
    std::vector<FeatureEntitlement> findByTenant(const std::string& tenant_id);
    std::vector<FeatureEntitlement> findEnabledByTenant(const std::string& tenant_id);
    std::vector<FeatureEntitlement> findByCategory(const std::string& tenant_id, const std::string& category);
    std::vector<FeatureEntitlement> findExpired();
    std::vector<FeatureEntitlement> findStale(int minutes_threshold);
    
    // Bulk operations
    bool clearStale(int minutes_threshold);
    
private:
    std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool_;
};

/**
 * Repository for usage event operations
 */
class UsageEventRepository {
public:
    explicit UsageEventRepository(std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool);
    
    // CRUD operations
    std::string save(const UsageEvent& event);
    bool saveBatch(const std::vector<UsageEvent>& events);
    bool markSynced(const std::vector<std::string>& event_ids);
    
    // Query operations
    std::vector<UsageEvent> findUnsynced(int limit = 1000);
    std::vector<UsageEvent> findByTenant(const std::string& tenant_id, time_t start_time, time_t end_time);
    std::vector<UsageEvent> findByType(const std::string& tenant_id, const std::string& event_type, 
                                       time_t start_time, time_t end_time);
    
    // Aggregation operations
    double sumQuantity(const std::string& tenant_id, const std::string& event_type, 
                      time_t start_time, time_t end_time);
    std::map<std::string, double> sumByType(const std::string& tenant_id, time_t start_time, time_t end_time);
    
    // Maintenance operations
    int deleteOld(int days_retention);
    int countUnsynced();
    
private:
    std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool_;
};

/**
 * Repository for billing sync status operations
 */
class BillingSyncStatusRepository {
public:
    explicit BillingSyncStatusRepository(std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool);
    
    // CRUD operations
    std::optional<BillingSyncStatus> findLatest(const std::string& tenant_id, const std::string& sync_type);
    bool save(const BillingSyncStatus& status);
    bool update(const BillingSyncStatus& status);
    
    // Query operations
    std::vector<BillingSyncStatus> findPending();
    std::vector<BillingSyncStatus> findFailed(int hours_threshold);
    std::vector<BillingSyncStatus> findByTenant(const std::string& tenant_id);
    
    // Statistics
    int countSuccessful(const std::string& tenant_id, time_t since);
    int countFailed(const std::string& tenant_id, time_t since);
    
private:
    std::shared_ptr<tapi::database::PostgreSQLConnectionPool> pool_;
};

} // namespace billing
} // namespace brinkbyte


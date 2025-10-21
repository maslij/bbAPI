#pragma once

#include <string>
#include <chrono>
#include <map>
#include <vector>

namespace brinkbyte {
namespace billing {

/**
 * @brief Configuration for billing service integration
 * Loaded from environment variables and config files
 */
struct BillingConfig {
    // ===== Billing Service Configuration =====
    std::string billing_service_url = "https://billing.brinkbyte.com/api/v1";
    std::string billing_api_key = "";
    int billing_timeout_ms = 5000;
    int billing_max_retries = 3;
    bool mock_billing_service = false;  // Use mock for local development
    
    // ===== Edge Device Identity =====
    std::string device_id = "auto";  // Auto-generate from hardware UUID if "auto"
    std::string tenant_id = "";
    std::string management_tier = "basic";  // basic ($50/mo) or managed ($65/mo)
    
    // ===== PostgreSQL Database =====
    std::string postgres_host = "localhost";
    int postgres_port = 5432;
    std::string postgres_database = "tapi_edge";
    std::string postgres_user = "tapi_user";
    std::string postgres_password = "tapi_dev_password";
    int postgres_pool_size = 10;
    int postgres_connection_timeout_ms = 5000;
    
    // ===== Redis Cache =====
    std::string redis_host = "localhost";
    int redis_port = 6379;
    std::string redis_password = "";
    int redis_max_memory_mb = 256;
    int redis_connection_timeout_ms = 3000;
    
    // ===== License Caching Configuration =====
    std::chrono::seconds license_cache_ttl{3600};  // 1 hour
    std::chrono::seconds entitlement_cache_ttl{300};  // 5 minutes
    bool enable_offline_mode = true;  // Allow operation with cached licenses when offline
    int offline_grace_period_hours = 24;  // How long to accept cached licenses in offline mode
    
    // ===== Usage Tracking Configuration =====
    int usage_batch_size = 1000;  // Number of events to batch before syncing
    std::chrono::minutes usage_sync_interval{5};  // How often to sync to billing service
    bool track_api_calls = true;
    bool track_llm_tokens = true;
    bool track_storage = true;
    bool track_agent_executions = true;
    bool track_sms = true;
    
    // ===== Feature Flags =====
    bool enable_license_validation = true;
    bool enable_usage_tracking = true;
    bool enable_heartbeat = true;
    std::chrono::minutes heartbeat_interval{15};  // How often to send heartbeat
    bool bypass_license_check = false;  // DANGEROUS: Bypass all license checks (dev only)
    
    // ===== API Server Configuration =====
    int api_port = 8080;
    int api_threads = 4;
    bool api_enable_cors = true;
    
    // ===== Logging Configuration =====
    std::string log_level = "INFO";  // DEBUG, INFO, WARN, ERROR
    bool log_to_file = true;
    std::string log_file_path = "/var/log/tapi/tapi.log";
    int log_max_file_size_mb = 100;
    int log_max_files = 10;
    
    // ===== Development/Debug Settings =====
    bool debug_mode = false;
    
    /**
     * @brief Load configuration from environment variables and config files
     * Priority: Environment Variables > Config File > Defaults
     */
    static BillingConfig load();
    
    /**
     * @brief Load from environment variables
     */
    static BillingConfig loadFromEnvironment();
    
    /**
     * @brief Validate configuration (check required fields)
     */
    bool validate() const;
    
    /**
     * @brief Get PostgreSQL connection string
     */
    std::string getPostgresConnectionString() const;
    
    /**
     * @brief Print configuration (with sensitive fields masked)
     */
    std::string toString() const;
    
private:
    /**
     * @brief Get environment variable with default value
     */
    static std::string getEnv(const std::string& key, const std::string& default_value = "");
    static int getEnvInt(const std::string& key, int default_value);
    static bool getEnvBool(const std::string& key, bool default_value);
    
    /**
     * @brief Generate device ID from hardware UUID
     */
    static std::string generateDeviceId();
};

/**
 * @brief Growth pack feature mappings
 * Defines which features are included in each growth pack
 */
class GrowthPackFeatures {
public:
    // Feature categories
    enum class Category {
        CV_MODELS,
        ANALYTICS,
        OUTPUTS,
        AGENTS,
        LLM
    };
    
    // Growth pack types
    enum class PackType {
        BASE,
        ADVANCED_ANALYTICS,
        INDUSTRY_ACTIVE_TRANSPORT,
        INDUSTRY_ADVANCED_VEHICLES,
        INDUSTRY_EMERGENCY_VEHICLES,
        INDUSTRY_RETAIL,
        INDUSTRY_MINING,
        INDUSTRY_AIRPORTS,
        INDUSTRY_WATERWAYS,
        INTELLIGENCE,
        INTEGRATION,
        DATA
    };
    
    /**
     * @brief Get all features included in a growth pack
     */
    static std::map<Category, std::vector<std::string>> getFeaturesForPack(PackType pack);
    
    /**
     * @brief Check if a feature is included in a growth pack
     */
    static bool isFeatureInPack(PackType pack, Category category, const std::string& feature_name);
    
    /**
     * @brief Get pack type from name string
     */
    static PackType packTypeFromString(const std::string& pack_name);
    
    /**
     * @brief Get pack name from type
     */
    static std::string packTypeToString(PackType pack);
    
    /**
     * @brief Get category from string
     */
    static Category categoryFromString(const std::string& category_name);
    
    /**
     * @brief Get category string
     */
    static std::string categoryToString(Category category);
    
    /**
     * @brief Initialize feature mappings (call once at startup)
     */
    static void initialize();
    
private:
    static std::map<PackType, std::map<Category, std::vector<std::string>>> feature_map_;
    static bool initialized_;
};

/**
 * @brief Pricing constants for reference
 */
namespace Pricing {
    // Base pricing
    constexpr double CAMERA_BASE_LICENSE_MONTHLY = 60.0;  // $60/camera/month
    constexpr double EDGE_DEVICE_BASIC_MONTHLY = 50.0;  // $50/device/month
    constexpr double EDGE_DEVICE_MANAGED_MONTHLY = 65.0;  // $65/device/month
    
    // Growth packs
    constexpr double ADVANCED_ANALYTICS_PER_CAMERA_MONTHLY = 20.0;  // $20/camera/month
    constexpr double INTELLIGENCE_PACK_TENANT_MONTHLY = 400.0;  // $400/tenant/month
    constexpr double INTELLIGENCE_PACK_EXTRA_SEAT = 120.0;  // $120/seat/month
    
    // Data retention
    constexpr double DATA_PACK_24M_PER_CAMERA_MONTHLY = 1.50;  // $1.50/camera/month
    constexpr double DATA_PACK_36M_PER_CAMERA_MONTHLY = 1.00;  // $1.00/camera/month
    constexpr double CLOUD_EXPORT_TENANT_MONTHLY = 150.0;  // $150/tenant/month
    
    // Usage-based pricing
    constexpr double API_OVERAGE_PER_1K_CALLS = 0.05;  // $0.05 per 1000 API calls
    
    // VMS integrations
    constexpr double VMS_CONNECTOR_ONE_TIME = 500.0;  // $500 one-time
    constexpr double VMS_CONNECTOR_ANNUAL = 75.0;  // $75/year
    
    // Trial limits
    constexpr int TRIAL_CAMERA_LIMIT = 2;
    constexpr int TRIAL_DURATION_DAYS = 90;  // 3 months
    
    // Quotas
    constexpr int BASE_LICENSE_API_CALLS_MONTHLY = 50000;
    constexpr int BASE_LICENSE_LLM_TOKENS_MONTHLY = 250000;
    constexpr int FREE_TRIAL_LLM_TOKENS_MONTHLY = 50000;
    constexpr int INTELLIGENCE_PACK_LLM_TOKENS_PER_SEAT_MONTHLY = 250000;
}

} // namespace billing
} // namespace brinkbyte


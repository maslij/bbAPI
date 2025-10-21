#include "billing/billing_config.h"
#include "logger.h"
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid/uuid.h>

namespace brinkbyte {
namespace billing {

// Static member initialization
std::map<GrowthPackFeatures::PackType, std::map<GrowthPackFeatures::Category, std::vector<std::string>>> 
    GrowthPackFeatures::feature_map_;
bool GrowthPackFeatures::initialized_ = false;

// Helper function to get environment variable with default
std::string BillingConfig::getEnv(const std::string& key, const std::string& default_value) {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : default_value;
}

int BillingConfig::getEnvInt(const std::string& key, int default_value) {
    const char* val = std::getenv(key.c_str());
    if (!val) return default_value;
    try {
        return std::stoi(val);
    } catch (...) {
        LOG_WARN("BillingConfig", "Invalid integer value for " + key + ", using default");
        return default_value;
    }
}

bool BillingConfig::getEnvBool(const std::string& key, bool default_value) {
    const char* val = std::getenv(key.c_str());
    if (!val) return default_value;
    std::string str_val(val);
    std::transform(str_val.begin(), str_val.end(), str_val.begin(), ::tolower);
    return (str_val == "true" || str_val == "1" || str_val == "yes");
}

std::string BillingConfig::generateDeviceId() {
    // Try to read machine-id first (Linux)
    std::ifstream machine_id_file("/etc/machine-id");
    if (machine_id_file.is_open()) {
        std::string machine_id;
        std::getline(machine_id_file, machine_id);
        machine_id_file.close();
        if (!machine_id.empty()) {
            return machine_id;
        }
    }
    
    // Fallback: generate UUID
    uuid_t uuid;
    char uuid_str[37];
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_str);
    return std::string(uuid_str);
}

BillingConfig BillingConfig::load() {
    LOG_INFO("BillingConfig", "Loading billing configuration from environment");
    return loadFromEnvironment();
}

BillingConfig BillingConfig::loadFromEnvironment() {
    BillingConfig config;
    
    // Billing Service Configuration
    config.billing_service_url = getEnv("BILLING_SERVICE_URL", "https://billing.brinkbyte.com/api/v1");
    config.billing_api_key = getEnv("BILLING_API_KEY", "");
    config.billing_timeout_ms = getEnvInt("BILLING_TIMEOUT_MS", 5000);
    config.billing_max_retries = getEnvInt("BILLING_MAX_RETRIES", 3);
    config.mock_billing_service = getEnvBool("MOCK_BILLING_SERVICE", false);
    
    // Edge Device Identity
    std::string device_id_env = getEnv("EDGE_DEVICE_ID", "auto");
    if (device_id_env == "auto") {
        config.device_id = generateDeviceId();
        LOG_INFO("BillingConfig", "Generated device_id: " + config.device_id);
    } else {
        config.device_id = device_id_env;
    }
    config.tenant_id = getEnv("TENANT_ID", "");
    config.management_tier = getEnv("MANAGEMENT_TIER", "basic");
    
    // PostgreSQL Database
    config.postgres_host = getEnv("POSTGRES_HOST", "localhost");
    config.postgres_port = getEnvInt("POSTGRES_PORT", 5432);
    config.postgres_database = getEnv("POSTGRES_DATABASE", "tapi_edge");
    config.postgres_user = getEnv("POSTGRES_USER", "tapi_user");
    config.postgres_password = getEnv("POSTGRES_PASSWORD", "tapi_dev_password");
    config.postgres_pool_size = getEnvInt("POSTGRES_POOL_SIZE", 10);
    config.postgres_connection_timeout_ms = getEnvInt("POSTGRES_CONNECTION_TIMEOUT_MS", 5000);
    
    // Redis Cache
    config.redis_host = getEnv("REDIS_HOST", "localhost");
    config.redis_port = getEnvInt("REDIS_PORT", 6379);
    config.redis_password = getEnv("REDIS_PASSWORD", "");
    config.redis_max_memory_mb = getEnvInt("REDIS_MAX_MEMORY_MB", 256);
    config.redis_connection_timeout_ms = getEnvInt("REDIS_CONNECTION_TIMEOUT_MS", 3000);
    
    // License Caching Configuration
    int license_ttl = getEnvInt("LICENSE_CACHE_TTL_SECONDS", 3600);
    config.license_cache_ttl = std::chrono::seconds(license_ttl);
    
    int entitlement_ttl = getEnvInt("ENTITLEMENT_CACHE_TTL_SECONDS", 300);
    config.entitlement_cache_ttl = std::chrono::seconds(entitlement_ttl);
    
    config.enable_offline_mode = getEnvBool("ENABLE_OFFLINE_MODE", true);
    config.offline_grace_period_hours = getEnvInt("OFFLINE_GRACE_PERIOD_HOURS", 24);
    
    // Usage Tracking Configuration
    config.usage_batch_size = getEnvInt("USAGE_BATCH_SIZE", 1000);
    
    int sync_interval = getEnvInt("USAGE_SYNC_INTERVAL_MINUTES", 5);
    config.usage_sync_interval = std::chrono::minutes(sync_interval);
    
    config.track_api_calls = getEnvBool("TRACK_API_CALLS", true);
    config.track_llm_tokens = getEnvBool("TRACK_LLM_TOKENS", true);
    config.track_storage = getEnvBool("TRACK_STORAGE", true);
    config.track_agent_executions = getEnvBool("TRACK_AGENT_EXECUTIONS", true);
    config.track_sms = getEnvBool("TRACK_SMS", true);
    
    // Feature Flags
    config.enable_license_validation = getEnvBool("ENABLE_LICENSE_VALIDATION", true);
    config.enable_usage_tracking = getEnvBool("ENABLE_USAGE_TRACKING", true);
    config.enable_heartbeat = getEnvBool("ENABLE_HEARTBEAT", true);
    
    int heartbeat_interval = getEnvInt("HEARTBEAT_INTERVAL_MINUTES", 15);
    config.heartbeat_interval = std::chrono::minutes(heartbeat_interval);
    
    config.bypass_license_check = getEnvBool("BYPASS_LICENSE_CHECK", false);
    
    // API Server Configuration
    config.api_port = getEnvInt("API_PORT", 8080);
    config.api_threads = getEnvInt("API_THREADS", 4);
    config.api_enable_cors = getEnvBool("API_ENABLE_CORS", true);
    
    // Logging Configuration
    config.log_level = getEnv("LOG_LEVEL", "INFO");
    config.log_to_file = getEnvBool("LOG_TO_FILE", true);
    config.log_file_path = getEnv("LOG_FILE_PATH", "/var/log/tapi/tapi.log");
    config.log_max_file_size_mb = getEnvInt("LOG_MAX_FILE_SIZE_MB", 100);
    config.log_max_files = getEnvInt("LOG_MAX_FILES", 10);
    
    // Development/Debug Settings
    config.debug_mode = getEnvBool("DEBUG_MODE", false);
    
    // Validate configuration
    if (!config.validate()) {
        LOG_ERROR("BillingConfig", "Configuration validation failed");
    }
    
    // Log configuration summary (masked)
    LOG_INFO("BillingConfig", "Configuration loaded successfully");
    if (config.debug_mode) {
        LOG_DEBUG("BillingConfig", config.toString());
    }
    
    return config;
}

bool BillingConfig::validate() const {
    bool valid = true;
    
    // Check required fields
    if (tenant_id.empty() && !bypass_license_check) {
        LOG_ERROR("BillingConfig", "TENANT_ID is required");
        valid = false;
    }
    
    if (billing_api_key.empty() && !mock_billing_service && !bypass_license_check) {
        LOG_WARN("BillingConfig", "BILLING_API_KEY is empty - billing service calls will fail");
    }
    
    if (device_id.empty()) {
        LOG_ERROR("BillingConfig", "EDGE_DEVICE_ID could not be determined");
        valid = false;
    }
    
    // Validate database configuration
    if (postgres_host.empty() || postgres_database.empty() || postgres_user.empty()) {
        LOG_ERROR("BillingConfig", "PostgreSQL configuration is incomplete");
        valid = false;
    }
    
    // Validate Redis configuration
    if (redis_host.empty()) {
        LOG_ERROR("BillingConfig", "Redis host is not configured");
        valid = false;
    }
    
    // Validate management tier
    if (management_tier != "basic" && management_tier != "managed") {
        LOG_WARN("BillingConfig", "Invalid management_tier: " + management_tier + ", defaulting to 'basic'");
    }
    
    return valid;
}

std::string BillingConfig::getPostgresConnectionString() const {
    std::ostringstream oss;
    oss << "host=" << postgres_host
        << " port=" << postgres_port
        << " dbname=" << postgres_database
        << " user=" << postgres_user
        << " password=" << postgres_password
        << " connect_timeout=" << (postgres_connection_timeout_ms / 1000);
    return oss.str();
}

std::string BillingConfig::toString() const {
    std::ostringstream oss;
    oss << "BillingConfig:\n";
    oss << "  Billing Service URL: " << billing_service_url << "\n";
    oss << "  Billing API Key: " << (billing_api_key.empty() ? "<not set>" : "***masked***") << "\n";
    oss << "  Device ID: " << device_id << "\n";
    oss << "  Tenant ID: " << (tenant_id.empty() ? "<not set>" : tenant_id) << "\n";
    oss << "  Management Tier: " << management_tier << "\n";
    oss << "  PostgreSQL: " << postgres_host << ":" << postgres_port << "/" << postgres_database << "\n";
    oss << "  Redis: " << redis_host << ":" << redis_port << "\n";
    oss << "  License Cache TTL: " << license_cache_ttl.count() << "s\n";
    oss << "  Entitlement Cache TTL: " << entitlement_cache_ttl.count() << "s\n";
    oss << "  Offline Mode: " << (enable_offline_mode ? "enabled" : "disabled") << "\n";
    oss << "  Usage Batch Size: " << usage_batch_size << "\n";
    oss << "  Usage Sync Interval: " << usage_sync_interval.count() << "min\n";
    oss << "  License Validation: " << (enable_license_validation ? "enabled" : "disabled") << "\n";
    oss << "  Usage Tracking: " << (enable_usage_tracking ? "enabled" : "disabled") << "\n";
    oss << "  Heartbeat: " << (enable_heartbeat ? "enabled" : "disabled") << "\n";
    oss << "  Bypass License Check: " << (bypass_license_check ? "YES (DANGEROUS)" : "no") << "\n";
    return oss.str();
}

// =====================================================
// GrowthPackFeatures Implementation
// =====================================================

void GrowthPackFeatures::initialize() {
    if (initialized_) return;
    
    LOG_INFO("GrowthPackFeatures", "Initializing growth pack feature mappings");
    
    // Base License ($60/cam/mo)
    feature_map_[PackType::BASE][Category::CV_MODELS] = {
        "person", "car", "van", "truck", "bus", "motorcycle"
    };
    feature_map_[PackType::BASE][Category::ANALYTICS] = {
        "detection", "tracking", "counting", "dwell", "heatmap", 
        "direction", "speed", "privacy_mask"
    };
    feature_map_[PackType::BASE][Category::OUTPUTS] = {
        "edge_io", "dashboard", "email", "webhook", "api"
    };
    
    // Advanced Analytics Pack ($20/cam/mo)
    feature_map_[PackType::ADVANCED_ANALYTICS][Category::ANALYTICS] = {
        "near_miss", "interaction_time", "queue_counter", "object_size"
    };
    
    // Industry Pack: Active Transport
    feature_map_[PackType::INDUSTRY_ACTIVE_TRANSPORT][Category::CV_MODELS] = {
        "bike", "scooter", "pram", "wheelchair"
    };
    
    // Industry Pack: Advanced Vehicles
    feature_map_[PackType::INDUSTRY_ADVANCED_VEHICLES][Category::CV_MODELS] = {
        "car", "ute", "van", "bus", "light_rigid", "medium_rigid", 
        "heavy_rigid", "prime_mover", "heavy_articulated"
    };
    
    // Industry Pack: Emergency Vehicles
    feature_map_[PackType::INDUSTRY_EMERGENCY_VEHICLES][Category::CV_MODELS] = {
        "police", "ambulance", "fire_fighter"
    };
    
    // Industry Pack: Retail
    feature_map_[PackType::INDUSTRY_RETAIL][Category::CV_MODELS] = {
        "trolley", "staff", "customer"
    };
    
    // Industry Pack: Mining
    feature_map_[PackType::INDUSTRY_MINING][Category::CV_MODELS] = {
        "light_vehicle", "heavy_vehicle", "ppe"
    };
    
    // Industry Pack: Airports
    feature_map_[PackType::INDUSTRY_AIRPORTS][Category::CV_MODELS] = {
        "trolley", "plane", "gse", "fuel_truck", "tug", "tractor", "belt_loader"
    };
    
    // Industry Pack: Waterways
    feature_map_[PackType::INDUSTRY_WATERWAYS][Category::CV_MODELS] = {
        "boat_commercial", "boat_recreational", "boat_fishing", "boat_cruise",
        "boat_tanker", "boat_cargo", "jetski", "kayak"
    };
    
    // Intelligence Pack ($400/tenant/mo)
    feature_map_[PackType::INTELLIGENCE][Category::LLM] = {
        "analyst_seat_full", "premium_connectors", "automated_reports"
    };
    
    // Integration Pack
    feature_map_[PackType::INTEGRATION][Category::OUTPUTS] = {
        "sms", "cloud_export", "vms_connectors"
    };
    
    initialized_ = true;
    LOG_INFO("GrowthPackFeatures", "Feature mappings initialized successfully");
}

std::map<GrowthPackFeatures::Category, std::vector<std::string>> 
GrowthPackFeatures::getFeaturesForPack(PackType pack) {
    if (!initialized_) initialize();
    
    auto it = feature_map_.find(pack);
    if (it != feature_map_.end()) {
        return it->second;
    }
    return {};
}

bool GrowthPackFeatures::isFeatureInPack(PackType pack, Category category, const std::string& feature_name) {
    if (!initialized_) initialize();
    
    auto pack_it = feature_map_.find(pack);
    if (pack_it == feature_map_.end()) return false;
    
    auto cat_it = pack_it->second.find(category);
    if (cat_it == pack_it->second.end()) return false;
    
    const auto& features = cat_it->second;
    return std::find(features.begin(), features.end(), feature_name) != features.end();
}

GrowthPackFeatures::PackType GrowthPackFeatures::packTypeFromString(const std::string& pack_name) {
    if (pack_name == "base") return PackType::BASE;
    if (pack_name == "advanced_analytics") return PackType::ADVANCED_ANALYTICS;
    if (pack_name == "active_transport") return PackType::INDUSTRY_ACTIVE_TRANSPORT;
    if (pack_name == "advanced_vehicles") return PackType::INDUSTRY_ADVANCED_VEHICLES;
    if (pack_name == "emergency_vehicles") return PackType::INDUSTRY_EMERGENCY_VEHICLES;
    if (pack_name == "retail") return PackType::INDUSTRY_RETAIL;
    if (pack_name == "mining") return PackType::INDUSTRY_MINING;
    if (pack_name == "airports") return PackType::INDUSTRY_AIRPORTS;
    if (pack_name == "waterways") return PackType::INDUSTRY_WATERWAYS;
    if (pack_name == "intelligence") return PackType::INTELLIGENCE;
    if (pack_name == "integration") return PackType::INTEGRATION;
    if (pack_name == "data") return PackType::DATA;
    
    LOG_WARN("GrowthPackFeatures", "Unknown pack type: " + pack_name);
    return PackType::BASE;
}

std::string GrowthPackFeatures::packTypeToString(PackType pack) {
    switch (pack) {
        case PackType::BASE: return "base";
        case PackType::ADVANCED_ANALYTICS: return "advanced_analytics";
        case PackType::INDUSTRY_ACTIVE_TRANSPORT: return "active_transport";
        case PackType::INDUSTRY_ADVANCED_VEHICLES: return "advanced_vehicles";
        case PackType::INDUSTRY_EMERGENCY_VEHICLES: return "emergency_vehicles";
        case PackType::INDUSTRY_RETAIL: return "retail";
        case PackType::INDUSTRY_MINING: return "mining";
        case PackType::INDUSTRY_AIRPORTS: return "airports";
        case PackType::INDUSTRY_WATERWAYS: return "waterways";
        case PackType::INTELLIGENCE: return "intelligence";
        case PackType::INTEGRATION: return "integration";
        case PackType::DATA: return "data";
        default: return "unknown";
    }
}

GrowthPackFeatures::Category GrowthPackFeatures::categoryFromString(const std::string& category_name) {
    if (category_name == "cv_models") return Category::CV_MODELS;
    if (category_name == "analytics") return Category::ANALYTICS;
    if (category_name == "outputs") return Category::OUTPUTS;
    if (category_name == "agents") return Category::AGENTS;
    if (category_name == "llm") return Category::LLM;
    
    LOG_WARN("GrowthPackFeatures", "Unknown category: " + category_name);
    return Category::CV_MODELS;
}

std::string GrowthPackFeatures::categoryToString(Category category) {
    switch (category) {
        case Category::CV_MODELS: return "cv_models";
        case Category::ANALYTICS: return "analytics";
        case Category::OUTPUTS: return "outputs";
        case Category::AGENTS: return "agents";
        case Category::LLM: return "llm";
        default: return "unknown";
    }
}

} // namespace billing
} // namespace brinkbyte


#include "global_config.h"
#include "logger.h"
#include <iostream>

namespace tapi {

GlobalConfig::GlobalConfig()
    : aiServerUrl_("http://localhost:8000"), useSharedMemory_(false), port_(8080) {
}

GlobalConfig& GlobalConfig::getInstance() {
    static GlobalConfig instance;
    return instance;
}

bool GlobalConfig::initialize(const std::string& aiServerUrl, bool useSharedMemory, int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check environment variables first - they take highest precedence
    const char* envServerUrl = getenv("AI_SERVER_URL");
    if (envServerUrl && strlen(envServerUrl) > 0) {
        aiServerUrl_ = envServerUrl;
        LOG_INFO("GlobalConfig", "Using AI server URL from environment: " + aiServerUrl_);
    } else {
        // Try alternative environment variable names as fallback
        envServerUrl = getenv("SERVER_URL");
        if (envServerUrl && strlen(envServerUrl) > 0) {
            aiServerUrl_ = envServerUrl;
            LOG_INFO("GlobalConfig", "Using SERVER_URL from environment: " + aiServerUrl_);
        } else {
            // Use the provided URL from command line or default value
            aiServerUrl_ = aiServerUrl;
            LOG_INFO("GlobalConfig", "Using AI server URL from command line: " + aiServerUrl_);
        }
    }
    
    // Check environment variable for shared memory setting
    const char* envSharedMem = getenv("USE_SHARED_MEMORY");
    if (envSharedMem && (std::string(envSharedMem) == "1" || std::string(envSharedMem) == "true")) {
        useSharedMemory_ = true;
        LOG_INFO("GlobalConfig", "Using shared memory setting from environment: true");
    } else {
        // Use the provided value from command line or default value
        useSharedMemory_ = useSharedMemory;
        LOG_INFO("GlobalConfig", "Using shared memory setting from command line: " + std::string(useSharedMemory_ ? "true" : "false"));
    }
    
    // Set environment variable for components that check it directly
    if (useSharedMemory_) {
        setenv("USE_SHARED_MEMORY", "1", 1); // overwrite existing value if any
    } else {
        unsetenv("USE_SHARED_MEMORY");
    }
    
    // Set port from command line
    port_ = port;
    LOG_INFO("GlobalConfig", "Using port: " + std::to_string(port_));
    
    // Store the configuration in the ConfigManager for persistence
    // Only do this if ConfigManager is initialized to avoid errors
    if (ConfigManager::getInstance().isReady()) {
        ConfigManager::getInstance().setConfig("ai_server_url", aiServerUrl_);
        ConfigManager::getInstance().setConfig("use_shared_memory", useSharedMemory_);
        ConfigManager::getInstance().setConfig("port", port_);
        LOG_INFO("GlobalConfig", "Updated ConfigManager with current global settings");
    }
    
    return true;
}

std::string GlobalConfig::getAiServerUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Always check environment variables first - they take highest precedence
    const char* envServerUrl = getenv("AI_SERVER_URL");
    if (envServerUrl && strlen(envServerUrl) > 0) {
        LOG_INFO("GlobalConfig", "getAiServerUrl: Using value from AI_SERVER_URL env: " + std::string(envServerUrl));
        return envServerUrl;
    }
    
    // Try alternative environment variable names as fallback
    envServerUrl = getenv("SERVER_URL");
    if (envServerUrl && strlen(envServerUrl) > 0) {
        LOG_INFO("GlobalConfig", "getAiServerUrl: Using value from SERVER_URL env: " + std::string(envServerUrl));
        return envServerUrl;
    }
    
    // Check ConfigManager for stored value - this takes precedence over the member variable
    // because it could have been updated through the API
    // Only check ConfigManager if it's initialized
    if (ConfigManager::getInstance().isReady()) {
        auto configValue = ConfigManager::getInstance().getConfig("ai_server_url");
        
        // BUG FIX: Check if configValue itself has an "ai_server_url" field
        // This is what the API returns from the config endpoint
        if (!configValue.empty()) {
            if (configValue.is_string()) {
                LOG_INFO("GlobalConfig", "getAiServerUrl: Using string value from ConfigManager: " + configValue.get<std::string>());
                return configValue.get<std::string>();
            } 
            else if (configValue.is_object() && configValue.contains("ai_server_url") && configValue["ai_server_url"].is_string()) {
                std::string configUrl = configValue["ai_server_url"].get<std::string>();
                LOG_INFO("GlobalConfig", "getAiServerUrl: Using value from ConfigManager object: " + configUrl);
                return configUrl;
            }
        }
    } else {
        LOG_INFO("GlobalConfig", "getAiServerUrl: ConfigManager not ready, skipping config check");
    }
    
    // Otherwise return the stored value
    LOG_INFO("GlobalConfig", "getAiServerUrl: Using value from instance variable: " + aiServerUrl_);
    return aiServerUrl_;
}

bool GlobalConfig::getUseSharedMemory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return useSharedMemory_;
}

int GlobalConfig::getPort() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return port_;
}

void GlobalConfig::setAiServerUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    aiServerUrl_ = url;
    
    // Also update in ConfigManager for persistence
    ConfigManager::getInstance().setConfig("ai_server_url", url);
    LOG_INFO("GlobalConfig", "AI server URL updated to: " + url);
}

void GlobalConfig::setUseSharedMemory(bool use) {
    std::lock_guard<std::mutex> lock(mutex_);
    useSharedMemory_ = use;
    
    // Update environment variable
    if (use) {
        setenv("USE_SHARED_MEMORY", "1", 1);
    } else {
        unsetenv("USE_SHARED_MEMORY");
    }
    
    // Also update in ConfigManager for persistence
    ConfigManager::getInstance().setConfig("use_shared_memory", use);
    LOG_INFO("GlobalConfig", "Use shared memory updated to: " + std::string(use ? "true" : "false"));
}

void GlobalConfig::setPort(int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    port_ = port;
    
    // Also update in ConfigManager for persistence
    ConfigManager::getInstance().setConfig("port", port);
    LOG_INFO("GlobalConfig", "Port updated to: " + std::to_string(port));
}

} // namespace tapi 
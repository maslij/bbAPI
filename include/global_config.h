#pragma once

#include <string>
#include <mutex>
#include "config_manager.h"

namespace tapi {

/**
 * @brief Global configuration for the application
 * 
 * This class provides a centralized place for all global configuration settings.
 * It reads from environment variables, command line arguments, and the config database.
 */
class GlobalConfig {
public:
    /**
     * @brief Get the singleton instance
     * 
     * @return GlobalConfig& The singleton instance
     */
    static GlobalConfig& getInstance();
    
    /**
     * @brief Initialize global configuration
     * 
     * @param aiServerUrl AI server URL from command line or default
     * @param useSharedMemory Whether to use shared memory from command line or default
     * @param port Application HTTP port
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(const std::string& aiServerUrl, bool useSharedMemory, int port);
    
    /**
     * @brief Get the AI server URL
     * 
     * @return std::string The AI server URL
     */
    std::string getAiServerUrl() const;
    
    /**
     * @brief Get whether to use shared memory
     * 
     * @return bool Whether to use shared memory
     */
    bool getUseSharedMemory() const;
    
    /**
     * @brief Get the application HTTP port
     * 
     * @return int The port number
     */
    int getPort() const;
    
    /**
     * @brief Set the AI server URL
     * 
     * @param url The new AI server URL
     */
    void setAiServerUrl(const std::string& url);
    
    /**
     * @brief Set whether to use shared memory
     * 
     * @param use Whether to use shared memory
     */
    void setUseSharedMemory(bool use);
    
    /**
     * @brief Set the application HTTP port
     * 
     * @param port The new port number
     */
    void setPort(int port);
    
private:
    // Private constructor for singleton
    GlobalConfig();
    
    // No copy or move
    GlobalConfig(const GlobalConfig&) = delete;
    GlobalConfig& operator=(const GlobalConfig&) = delete;
    GlobalConfig(GlobalConfig&&) = delete;
    GlobalConfig& operator=(GlobalConfig&&) = delete;
    
    std::string aiServerUrl_;
    bool useSharedMemory_;
    int port_;
    
    mutable std::mutex mutex_;
};

} // namespace tapi 
#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <mutex>
#include <unordered_map>

namespace tapi {

/**
 * @brief Configuration Manager for persistent storage of app configuration
 * 
 * This class manages storing and retrieving configuration data using SQLite.
 * Configuration is stored as JSON in a simple key-value structure.
 */
class ConfigManager {
public:
    /**
     * @brief Get the singleton instance
     * 
     * @return ConfigManager& The singleton instance
     */
    static ConfigManager& getInstance();
    
    /**
     * @brief Initialize the configuration database
     * 
     * @param dbPath Path to SQLite database file
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(const std::string& dbPath);
    
    /**
     * @brief Get configuration value for the given key
     * 
     * @param key Configuration key
     * @return nlohmann::json Configuration value as JSON, empty if not found
     */
    nlohmann::json getConfig(const std::string& key);
    
    /**
     * @brief Set configuration value for the given key
     * 
     * @param key Configuration key
     * @param value Configuration value as JSON
     * @return true if successful, false otherwise
     */
    bool setConfig(const std::string& key, const nlohmann::json& value);
    
    /**
     * @brief Delete configuration for the given key
     * 
     * @param key Configuration key
     * @return true if successful, false otherwise
     */
    bool deleteConfig(const std::string& key);
    
    /**
     * @brief Get all configuration entries
     * 
     * @return nlohmann::json Object containing all configuration
     */
    nlohmann::json getAllConfig();

    /**
     * @brief Get camera configuration
     * 
     * @param cameraId Camera ID
     * @return nlohmann::json Camera configuration as JSON
     */
    nlohmann::json getCameraConfig(const std::string& cameraId);
    
    /**
     * @brief Save camera configuration
     * 
     * @param cameraId Camera ID
     * @param config Camera configuration as JSON
     * @return true if successful, false otherwise
     */
    bool saveCameraConfig(const std::string& cameraId, const nlohmann::json& config);
    
    /**
     * @brief Delete camera configuration
     * 
     * @param cameraId Camera ID
     * @return true if successful, false otherwise
     */
    bool deleteCameraConfig(const std::string& cameraId);
    
    /**
     * @brief Get all camera configurations
     * 
     * @return nlohmann::json Object with camera ID as key and config as value
     */
    nlohmann::json getAllCameraConfigs();
    
    /**
     * @brief Check if database is initialized and ready
     * 
     * @return true if database is ready, false otherwise
     */
    bool isReady() const;
    
    /**
     * @brief Get the database path
     * 
     * @return std::string Path to the database file
     */
    std::string getDatabasePath() const;

private:
    // Private constructor for singleton
    ConfigManager();
    
    // Cleanup on destruction
    ~ConfigManager();
    
    // No copy or move
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;
    
    // Create tables if not exists
    bool createTables();
    
    // Execute SQL with error handling
    bool executeSQL(const std::string& sql);
    
    // Last SQLite error message
    std::string getLastError() const;
    
    // Cache to avoid frequent database reads
    void updateConfigCache(const std::string& key, const nlohmann::json& value);
    void clearConfigCache(const std::string& key);
    void refreshConfigCache();
    
    sqlite3* db_;                              ///< SQLite database handle
    std::string dbPath_;                       ///< Path to database file
    bool isInitialized_;                       ///< Flag indicating if DB is initialized
    mutable std::mutex mutex_;                 ///< Mutex for thread safety
    std::unordered_map<std::string, nlohmann::json> configCache_;  ///< In-memory cache of config
};

} // namespace tapi 
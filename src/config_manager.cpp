#include "config_manager.h"
#include "logger.h"
#include <iostream>
#include <filesystem>

namespace tapi {

ConfigManager::ConfigManager() 
    : db_(nullptr), isInitialized_(false) {
}

ConfigManager::~ConfigManager() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::initialize(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Close any existing database
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        isInitialized_ = false;
    }
    
    dbPath_ = dbPath;
    
    // Create directory if it doesn't exist
    auto dir = std::filesystem::path(dbPath).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
    
    // Open database
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(dbPath_.c_str(), &db_, flags, nullptr);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Cannot open database: " + std::string(sqlite3_errmsg(db_)));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    // Set pragmas for better performance
    const char* pragmas[] = {
        "PRAGMA journal_mode = WAL",
        "PRAGMA synchronous = NORMAL",
        "PRAGMA cache_size = 10000",
        "PRAGMA foreign_keys = ON",
        nullptr
    };
    
    char* errMsg = nullptr;
    for (int i = 0; pragmas[i] != nullptr; i++) {
        rc = sqlite3_exec(db_, pragmas[i], nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            LOG_WARN("ConfigManager", "Failed to set pragma: " + std::string(errMsg));
            sqlite3_free(errMsg);
            // Continue with other pragmas
        }
    }
    
    // Create tables
    if (!createTables()) {
        LOG_ERROR("ConfigManager", "Failed to create tables");
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    // Load configuration cache
    refreshConfigCache();
    
    isInitialized_ = true;
    LOG_INFO("ConfigManager", "Configuration database initialized at " + dbPath_);
    return true;
}

bool ConfigManager::createTables() {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS config ("
        "   key TEXT PRIMARY KEY,"
        "   value TEXT NOT NULL,"
        "   updated_at INTEGER NOT NULL"
        ");"
        
        "CREATE TABLE IF NOT EXISTS camera_config ("
        "   camera_id TEXT PRIMARY KEY,"
        "   config TEXT NOT NULL,"
        "   updated_at INTEGER NOT NULL"
        ");";
    
    return executeSQL(sql);
}

bool ConfigManager::executeSQL(const std::string& sql) {
    if (!db_) {
        LOG_ERROR("ConfigManager", "Database not initialized");
        return false;
    }
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "SQL error: " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

std::string ConfigManager::getLastError() const {
    if (!db_) {
        return "Database not initialized";
    }
    
    return sqlite3_errmsg(db_);
}

void ConfigManager::refreshConfigCache() {
    configCache_.clear();
    
    if (!db_) {
        return;
    }
    
    // General config
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT key, value FROM config;";
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Failed to prepare query: " + getLastError());
        return;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        
        try {
            configCache_[key] = nlohmann::json::parse(value);
        } catch (const std::exception& e) {
            LOG_ERROR("ConfigManager", "Failed to parse JSON for key " + key + ": " + e.what());
            // Store as string if parsing fails
            configCache_[key] = value;
        }
    }
    
    sqlite3_finalize(stmt);
}

void ConfigManager::updateConfigCache(const std::string& key, const nlohmann::json& value) {
    configCache_[key] = value;
}

void ConfigManager::clearConfigCache(const std::string& key) {
    if (configCache_.find(key) != configCache_.end()) {
        configCache_.erase(key);
    }
}

nlohmann::json ConfigManager::getConfig(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check cache first
    auto it = configCache_.find(key);
    if (it != configCache_.end()) {
        return it->second;
    }
    
    if (!db_) {
        LOG_ERROR("ConfigManager", "Database not initialized");
        return nlohmann::json();
    }
    
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT value FROM config WHERE key = ?;";
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Failed to prepare query: " + getLastError());
        return nlohmann::json();
    }
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    
    nlohmann::json result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        
        try {
            result = nlohmann::json::parse(value);
            updateConfigCache(key, result);
        } catch (const std::exception& e) {
            LOG_ERROR("ConfigManager", "Failed to parse JSON for key " + key + ": " + e.what());
            // Return as string if parsing fails
            result = value;
            updateConfigCache(key, result);
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

bool ConfigManager::setConfig(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) {
        LOG_ERROR("ConfigManager", "Database not initialized");
        return false;
    }
    
    // Convert JSON to string
    std::string valueStr;
    try {
        valueStr = value.dump();
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager", "Failed to serialize JSON for key " + key + ": " + e.what());
        return false;
    }
    
    sqlite3_stmt* stmt = nullptr;
    std::string sql = 
        "INSERT INTO config (key, value, updated_at) "
        "VALUES (?, ?, strftime('%s','now')) "
        "ON CONFLICT (key) DO UPDATE SET "
        "value = excluded.value, "
        "updated_at = excluded.updated_at;";
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Failed to prepare query: " + getLastError());
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, valueStr.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        LOG_ERROR("ConfigManager", "Failed to set config: " + getLastError());
        return false;
    }
    
    // Update cache
    updateConfigCache(key, value);
    
    return true;
}

bool ConfigManager::deleteConfig(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) {
        LOG_ERROR("ConfigManager", "Database not initialized");
        return false;
    }
    
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "DELETE FROM config WHERE key = ?;";
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Failed to prepare query: " + getLastError());
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        LOG_ERROR("ConfigManager", "Failed to delete config: " + getLastError());
        return false;
    }
    
    // Update cache
    clearConfigCache(key);
    
    return true;
}

nlohmann::json ConfigManager::getAllConfig() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Refresh cache to ensure it's up to date
    refreshConfigCache();
    
    nlohmann::json result = nlohmann::json::object();
    for (const auto& pair : configCache_) {
        result[pair.first] = pair.second;
    }
    
    return result;
}

nlohmann::json ConfigManager::getCameraConfig(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) {
        LOG_ERROR("ConfigManager", "Database not initialized");
        return nlohmann::json();
    }
    
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT config FROM camera_config WHERE camera_id = ?;";
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Failed to prepare query: " + getLastError());
        return nlohmann::json();
    }
    
    sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_TRANSIENT);
    
    nlohmann::json result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string config = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        
        try {
            result = nlohmann::json::parse(config);
        } catch (const std::exception& e) {
            LOG_ERROR("ConfigManager", "Failed to parse JSON for camera " + cameraId + ": " + e.what());
            result = nlohmann::json();
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

bool ConfigManager::saveCameraConfig(const std::string& cameraId, const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) {
        LOG_ERROR("ConfigManager", "Database not initialized");
        return false;
    }
    
    // Convert JSON to string
    std::string configStr;
    try {
        configStr = config.dump();
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager", "Failed to serialize JSON for camera " + cameraId + ": " + e.what());
        return false;
    }
    
    sqlite3_stmt* stmt = nullptr;
    std::string sql = 
        "INSERT INTO camera_config (camera_id, config, updated_at) "
        "VALUES (?, ?, strftime('%s','now')) "
        "ON CONFLICT (camera_id) DO UPDATE SET "
        "config = excluded.config, "
        "updated_at = excluded.updated_at;";
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Failed to prepare query: " + getLastError());
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, configStr.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        LOG_ERROR("ConfigManager", "Failed to save camera config: " + getLastError());
        return false;
    }
    
    return true;
}

bool ConfigManager::deleteCameraConfig(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) {
        LOG_ERROR("ConfigManager", "Database not initialized");
        return false;
    }
    
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "DELETE FROM camera_config WHERE camera_id = ?;";
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Failed to prepare query: " + getLastError());
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        LOG_ERROR("ConfigManager", "Failed to delete camera config: " + getLastError());
        return false;
    }
    
    return true;
}

nlohmann::json ConfigManager::getAllCameraConfigs() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!db_) {
        LOG_ERROR("ConfigManager", "Database not initialized");
        return nlohmann::json::object();
    }
    
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT camera_id, config FROM camera_config;";
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("ConfigManager", "Failed to prepare query: " + getLastError());
        return nlohmann::json::object();
    }
    
    nlohmann::json result = nlohmann::json::object();
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string cameraId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string configStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        
        try {
            result[cameraId] = nlohmann::json::parse(configStr);
        } catch (const std::exception& e) {
            LOG_ERROR("ConfigManager", "Failed to parse JSON for camera " + cameraId + ": " + e.what());
            // Skip this entry if parsing fails
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

bool ConfigManager::isReady() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isInitialized_ && db_ != nullptr;
}

std::string ConfigManager::getDatabasePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dbPath_;
}

} // namespace tapi 
#pragma once

#include <string>
#include <memory>
#include <crow.h>
#include <crow/middlewares/cors.h>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "camera_manager.h"
#include <iostream>
#include <future>
#include <queue>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <uuid/uuid.h>

namespace tapi {

// Forward declarations for helper structures
struct ApiLoggingConfig;
struct RequestContext;

/**
 * @brief API Logging Middleware for Crow
 */
class ApiLoggingMiddleware {
public:
    struct context {
        std::chrono::high_resolution_clock::time_point start_time;
        std::string method;
        std::string url;
        std::string client_ip;
        size_t request_size = 0;
        std::string request_id;
    };
    
    void before_handle(crow::request& req, crow::response& res, context& ctx);
    void after_handle(crow::request& req, crow::response& res, context& ctx);
};

// Request timeout middleware removed due to implementation issues
// The API logging middleware still tracks slow/timeout requests for monitoring

/**
 * @brief Background task manager to handle long-running operations
 */
class BackgroundTaskManager {
public:
    struct TaskStatus {
        enum class State {
            PENDING,
            RUNNING,
            COMPLETED,
            FAILED
        };
        
        State state;
        std::string taskId;
        std::string taskType;
        std::string targetId;
        double progress;
        std::string message;
        std::chrono::system_clock::time_point createdAt;
        std::chrono::system_clock::time_point updatedAt;
    };
    
    static BackgroundTaskManager& getInstance() {
        static BackgroundTaskManager instance;
        return instance;
    }
    
    BackgroundTaskManager(const BackgroundTaskManager&) = delete;
    BackgroundTaskManager& operator=(const BackgroundTaskManager&) = delete;
    
    ~BackgroundTaskManager() {
        shutdown();
    }
    
    // Submit a task to be executed asynchronously
    std::string submitTask(std::string taskType, std::string targetId, 
                          std::function<bool(std::function<void(double, std::string)>)> taskFunc);
    
    // Get status of a specific task
    TaskStatus getTaskStatus(const std::string& taskId);
    
    // Get all tasks
    std::vector<TaskStatus> getAllTasks();
    
    // Clean up completed tasks older than specified duration (in seconds)
    void cleanupOldTasks(int maxAgeSecs = 3600);
    
    // Shutdown the task manager
    void shutdown();
    
private:
    BackgroundTaskManager();
    
    void workerThread();
    
    struct Task {
        std::string id;
        std::string type;
        std::string targetId;
        std::function<bool(std::function<void(double, std::string)>)> func;
        std::chrono::system_clock::time_point createdAt;
    };
    
    std::unordered_map<std::string, TaskStatus> taskStatuses_;
    std::queue<Task> taskQueue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread workerThread_;
    std::atomic<bool> running_;
};

/**
 * @brief REST API for the tAPI service
 * 
 * The API class sets up and handles all REST API endpoints for the tAPI
 * service, allowing clients to manage video streams.
 */
class Api {
public:
    /**
     * @brief Construct a new Api object
     * 
     * @param port Port to listen on
     */
    Api(int port = 8000);

    /**
     * @brief Destructor
     */
    ~Api();

    /**
     * @brief Start the API server
     * 
     * @param threaded Run in a separate thread if true
     */
    void start(bool threaded = true);

    /**
     * @brief Stop the API server
     */
    void stop();

    /**
     * @brief Initialize the API with a license key
     * 
     * @param licenseKey License key for the edge device
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(const std::string& licenseKey);
    
    /**
     * @brief Load previously saved configuration from the database
     * 
     * @return true if loading succeeded, false otherwise
     */
    bool loadSavedConfig();

private:
    crow::App<crow::CORSHandler, ApiLoggingMiddleware> app_; ///< Crow application with CORS support and API logging
    int port_; ///< Port to listen on
    std::string configDbPath_; ///< Path to the configuration database
    
    /**
     * @brief Set up all API routes
     */
    void setupRoutes();

    /**
     * @brief Set up license and authentication routes
     */
    void setupLicenseRoutes();

    /**
     * @brief Set up camera management routes
     */
    void setupCameraRoutes();

    /**
     * @brief Set up component management routes
     */
    void setupComponentRoutes();
    
    /**
     * @brief Set up system management routes
     */
    void setupManagementRoutes();
    
    /**
     * @brief Set up frame access routes
     */
    void setupFrameRoutes();
    
    /**
     * @brief Set up database routes for telemetry data access
     */
    void setupDatabaseRoutes();
    
    /**
     * @brief Set up CORS headers
     */
    void setupCORS();

    /**
     * @brief Set up background task routes
     */
    void setupBackgroundTaskRoutes();
    
    /**
     * @brief Set up configuration management routes
     */
    void setupConfigRoutes();
    
    /**
     * @brief Set up API logging middleware
     */
    void setupApiLogging();
    
    /**
     * @brief Configure server for optimal concurrency
     */
    void configureServerConcurrency();
    
    /**
     * @brief Set up API logging management routes
     */
    void setupApiLoggingRoutes();

    /**
     * @brief Check if a valid license is present
     * 
     * @param req HTTP request
     * @param res HTTP response
     * @return true if license is valid, false otherwise
     */
    bool checkLicense(const crow::request& req, crow::response& res);

    /**
     * @brief Check license compliance for all cameras and stop non-compliant ones
     * 
     * Verifies each camera's components against the current license tier restrictions.
     * Any cameras using components not allowed by the current license will be stopped.
     * 
     * @return Number of cameras that were stopped due to license violations
     */
    int enforceLicenseRestrictions();
    
    /**
     * @brief Save camera configuration to database
     * 
     * @param cameraId Camera ID
     * @return true if successful, false otherwise
     */
    bool saveCameraConfigToDB(const std::string& cameraId);
    
    /**
     * @brief Load camera configuration from database
     * 
     * @param cameraId Camera ID
     * @return true if successful, false otherwise
     */
    bool loadCameraConfigFromDB(const std::string& cameraId);
    
    /**
     * @brief Initialize the configuration database
     * 
     * @return true if successful, false otherwise
     */
    bool initializeConfigDB();
    
    /**
     * @brief Sanitize component JSON to ensure consistent data types
     * 
     * @param componentJson Component JSON to sanitize
     */
    void sanitizeComponentJson(nlohmann::json& componentJson);
    
    /**
     * @brief Sanitize configuration JSON to ensure consistent data types
     * 
     * @param configJson Configuration JSON to sanitize
     */
    void sanitizeConfigJson(nlohmann::json& configJson);
};

} // namespace tapi 
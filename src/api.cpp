#include "api.h"
#include <nlohmann/json.hpp>
#include <thread>
#include "component_factory.h"
#include <uuid/uuid.h>
#include "logger.h"
#include "components/processor/object_detector_processor.h"
#include "components/sink/file_sink.h"
#include "components/sink/database_sink.h"
#include <fstream>
#include "config_manager.h"
#include <filesystem>
#include "global_config.h"
#include <chrono>
#include <iomanip>
#include <sstream>

// For chrono literals (2s syntax)
using namespace std::chrono_literals;

namespace tapi {

// API Logging Configuration Structure
struct ApiLoggingConfig {
    bool enabled = false;
    bool log_request_body = false;
    bool log_response_body = false;
    int slow_request_threshold_ms = 1000;  // 1 second default
    int timeout_threshold_ms = 30000;      // 30 seconds default
    bool log_only_slow_requests = false;
    bool include_request_headers = false;
    bool include_response_headers = false;
    
    void loadFromConfig() {
        auto& configManager = ConfigManager::getInstance();
        if (configManager.isReady()) {
            auto enabledConfig = configManager.getConfig("api_logging_enabled");
            if (!enabledConfig.empty() && enabledConfig.is_boolean()) {
                enabled = enabledConfig.get<bool>();
            }
            
            auto logBodyConfig = configManager.getConfig("api_logging_log_request_body");
            if (!logBodyConfig.empty() && logBodyConfig.is_boolean()) {
                log_request_body = logBodyConfig.get<bool>();
            }
            
            auto logResponseBodyConfig = configManager.getConfig("api_logging_log_response_body");
            if (!logResponseBodyConfig.empty() && logResponseBodyConfig.is_boolean()) {
                log_response_body = logResponseBodyConfig.get<bool>();
            }
            
            auto slowThresholdConfig = configManager.getConfig("api_logging_slow_threshold_ms");
            if (!slowThresholdConfig.empty() && slowThresholdConfig.is_number_integer()) {
                slow_request_threshold_ms = slowThresholdConfig.get<int>();
            }
            
            auto timeoutThresholdConfig = configManager.getConfig("api_logging_timeout_threshold_ms");
            if (!timeoutThresholdConfig.empty() && timeoutThresholdConfig.is_number_integer()) {
                timeout_threshold_ms = timeoutThresholdConfig.get<int>();
            }
            
            auto logOnlySlowConfig = configManager.getConfig("api_logging_log_only_slow");
            if (!logOnlySlowConfig.empty() && logOnlySlowConfig.is_boolean()) {
                log_only_slow_requests = logOnlySlowConfig.get<bool>();
            }
            
            auto includeHeadersConfig = configManager.getConfig("api_logging_include_request_headers");
            if (!includeHeadersConfig.empty() && includeHeadersConfig.is_boolean()) {
                include_request_headers = includeHeadersConfig.get<bool>();
            }
            
            auto includeResponseHeadersConfig = configManager.getConfig("api_logging_include_response_headers");
            if (!includeResponseHeadersConfig.empty() && includeResponseHeadersConfig.is_boolean()) {
                include_response_headers = includeResponseHeadersConfig.get<bool>();
            }
        }
    }
};



// Global API logging configuration
static ApiLoggingConfig g_apiLoggingConfig;

// Helper function to get current timestamp as string
std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// Helper function to get client IP from request
std::string getClientIP(const crow::request& req) {
    // Check for X-Forwarded-For header first (common in reverse proxy setups)
    auto xff_header = req.get_header_value("X-Forwarded-For");
    if (!xff_header.empty()) {
        return xff_header;
    }
    
    // Check for X-Real-IP header
    auto real_ip_header = req.get_header_value("X-Real-IP");
    if (!real_ip_header.empty()) {
        return real_ip_header;
    }
    
    // Fall back to remote address if available
    // Note: Crow doesn't directly expose remote address, so we'll use "unknown"
    return "unknown";
}



// Implementation of ApiLoggingMiddleware methods
void ApiLoggingMiddleware::before_handle(crow::request& req, crow::response& res, context& ctx) {
    // Reload configuration on each request (allows runtime changes)
    g_apiLoggingConfig.loadFromConfig();
    
    // Initialize context
    ctx.start_time = std::chrono::high_resolution_clock::now();
    ctx.method = crow::method_name(req.method);
    ctx.url = req.url;
    ctx.client_ip = getClientIP(req);
    ctx.request_size = req.body.size();
    
    // Generate unique request ID
    uuid_t uuid;
    char uuid_str[37];
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_str);
    ctx.request_id = std::string(uuid_str).substr(0, 8); // Short ID for logs
    
    // Log request start
    if (!g_apiLoggingConfig.enabled) return;
    
    if (!g_apiLoggingConfig.log_only_slow_requests) {
        std::stringstream logMsg;
        logMsg << "[API-REQ-START] [" << ctx.request_id << "] "
               << ctx.method << " " << ctx.url 
               << " from " << ctx.client_ip
               << " (size: " << ctx.request_size << " bytes)";
        
        if (g_apiLoggingConfig.include_request_headers) {
            logMsg << " Headers: {";
            bool first = true;
            for (const auto& header : req.headers) {
                if (!first) logMsg << ", ";
                logMsg << header.first << "='" << header.second << "'";
                first = false;
            }
            logMsg << "}";
        }
        
        if (g_apiLoggingConfig.log_request_body && !req.body.empty()) {
            logMsg << " Body: " << req.body;
        }
        
        LOG_INFO("API-TIMING", logMsg.str());
    }
}

void ApiLoggingMiddleware::after_handle(crow::request& req, crow::response& res, context& ctx) {
    if (!g_apiLoggingConfig.enabled) return;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - ctx.start_time);
    
    long duration_ms = duration.count();
    bool is_slow = duration_ms >= g_apiLoggingConfig.slow_request_threshold_ms;
    bool is_timeout = duration_ms >= g_apiLoggingConfig.timeout_threshold_ms;
    
    // Determine log level based on performance
    std::string logLevel = "INFO";
    if (is_timeout) {
        logLevel = "ERROR";
    } else if (is_slow) {
        logLevel = "WARN";
    }
    
    // Skip logging if configured to log only slow requests and this isn't slow
    if (g_apiLoggingConfig.log_only_slow_requests && !is_slow) {
        return;
    }
    
    std::stringstream logMsg;
    logMsg << "[API-REQ-COMPLETE] [" << ctx.request_id << "] "
           << ctx.method << " " << ctx.url
           << " -> " << res.code << " (" << duration_ms << "ms)"
           << " from " << ctx.client_ip
           << " (req: " << ctx.request_size << " bytes, res: " << res.body.size() << " bytes)";
    
    if (is_timeout) {
        logMsg << " [TIMEOUT]";
    } else if (is_slow) {
        logMsg << " [SLOW]";
    }
    
    if (g_apiLoggingConfig.include_response_headers) {
        logMsg << " Response-Headers: {";
        bool first = true;
        for (const auto& header : res.headers) {
            if (!first) logMsg << ", ";
            logMsg << header.first << "='" << header.second << "'";
            first = false;
        }
        logMsg << "}";
    }
    
    if (g_apiLoggingConfig.log_response_body && !res.body.empty() && 
        res.body.size() < 1000) { // Only log small response bodies
        logMsg << " Response-Body: " << res.body;
    }
    
    // Log with appropriate level
    if (logLevel == "ERROR") {
        LOG_ERROR("API-TIMING", logMsg.str());
    } else if (logLevel == "WARN") {
        LOG_WARN("API-TIMING", logMsg.str());
    } else {
        LOG_INFO("API-TIMING", logMsg.str());
    }
    
    // Additional performance metrics logging
    if (is_slow || is_timeout) {
        std::stringstream perfMsg;
        perfMsg << "[API-PERFORMANCE] [" << ctx.request_id << "] "
                << "Endpoint: " << ctx.method << " " << ctx.url
                << ", Duration: " << duration_ms << "ms"
                << ", Status: " << res.code
                << ", Request-Size: " << ctx.request_size << "B"
                << ", Response-Size: " << res.body.size() << "B"
                << ", Timestamp: " << getCurrentTimestamp();
        
        if (is_timeout) {
            LOG_ERROR("API-PERFORMANCE", perfMsg.str() + " [TIMEOUT DETECTED]");
        } else {
            LOG_WARN("API-PERFORMANCE", perfMsg.str() + " [SLOW REQUEST DETECTED]");
        }
    }
}

// RequestTimeoutMiddleware removed due to implementation causing all requests to wait 60 seconds
// The API logging middleware still provides timeout monitoring and logging

// Helper function to create properly formatted JSON responses
crow::response createJsonResponse(const nlohmann::json& data, int status_code = 200) {
    crow::response res(status_code, data.dump(2));
    res.set_header("Content-Type", "application/json");
    return res;
}

// Helper function to convert LogLevel to string
std::string levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "trace";
        case LogLevel::DEBUG: return "debug";
        case LogLevel::INFO:  return "info";
        case LogLevel::WARN:  return "warn";
        case LogLevel::ERROR: return "error";
        case LogLevel::FATAL: return "fatal";
        case LogLevel::OFF:   return "off";
        default:              return "unknown";
    }
}

Api::Api(int port)
    : port_(port) {
    // Configure port from provided value or GlobalConfig
    if (port > 0) {
        app_.port(port);
    } else {
        // If port is not specified (0 or negative), use GlobalConfig
        app_.port(GlobalConfig::getInstance().getPort());
        port_ = GlobalConfig::getInstance().getPort();
    }
    
    // Configure Crow server for high concurrency and timeout handling
    configureServerConcurrency();
    app_.server_name("tAPI/1.0"); // Set server name
    
    // Set default config path to data directory in user's home path
    const char* homeDir = getenv("HOME");
    if (homeDir) {
        std::string homePath(homeDir);
        configDbPath_ = homePath + "/.tapi/config.db";
    } else {
        // Fallback to /tmp if HOME is not set
        configDbPath_ = "/tmp/tapi/config.db";
    }
    
    // Set up CORS
    setupCORS();
    
    // Set up API logging middleware
    setupApiLogging();
}

Api::~Api() {
}

bool Api::initialize(const std::string& licenseKey) {
    bool licenseValid = true;
    
    // First initialize the configuration database
    if (!initializeConfigDB()) {
        LOG_WARN("API", "Failed to initialize configuration database, continuing without persistence");
    } else {
        // Check if we have a stored license key in the config db, if so, use that instead
        auto storedLicense = ConfigManager::getInstance().getConfig("license_key");
        if (!storedLicense.empty() && storedLicense.is_string()) {
            LOG_INFO("API", "Using license key from configuration database");
            std::string licenseKeyFromDb = storedLicense.get<std::string>();
            
            // Initialize the camera manager with the stored license key
            if (!CameraManager::getInstance().initialize(licenseKeyFromDb)) {
                licenseValid = false;
                LOG_WARN("API", "Stored license validation failed, continuing with limited functionality");
                
                // Fall back to provided license key
                if (!CameraManager::getInstance().initialize(licenseKey)) {
                    LOG_WARN("API", "Provided license validation failed as well");
                }
            } else {
                // License is valid, also restore owner and email information
                auto& licenseManager = const_cast<LicenseManager&>(CameraManager::getInstance().getLicenseManager());
                
                // Get owner and email from config
                auto ownerConfig = ConfigManager::getInstance().getConfig("license_owner");
                auto emailConfig = ConfigManager::getInstance().getConfig("license_email");
                
                nlohmann::json licenseInfo;
                if (!ownerConfig.empty() && ownerConfig.is_string()) {
                    licenseInfo["owner"] = ownerConfig.get<std::string>();
                }
                if (!emailConfig.empty() && emailConfig.is_string()) {
                    licenseInfo["email"] = emailConfig.get<std::string>();
                }
                
                // Update license with the saved info if we have any
                if (!licenseInfo.empty()) {
                    licenseManager.updateLicense(licenseInfo);
                    LOG_INFO("API", "Restored license owner/email information from database");
                }
            }
        } else {
            // No stored license, initialize with provided key
            if (!CameraManager::getInstance().initialize(licenseKey)) {
                licenseValid = false;
                LOG_WARN("API", "License validation failed, continuing with limited functionality");
            } else {
                // Save the provided license key to the database
                ConfigManager::getInstance().setConfig("license_key", licenseKey);
                LOG_INFO("API", "Saved license key to configuration database");
            }
        }
    }
    
    // Always set up API routes, even if license is invalid
    setupRoutes();
    
    // After initialization with a valid license, enforce license restrictions on all cameras
    if (licenseValid) {
        int stoppedCameras = enforceLicenseRestrictions();
        if (stoppedCameras > 0) {
            LOG_WARN("API", "License enforcement stopped " + std::to_string(stoppedCameras) + 
                     " camera(s) that were using features not allowed by the current license");
        }
    }
    
    return licenseValid;
}

bool Api::initializeConfigDB() {
    // Create directory if it doesn't exist
    auto dir = std::filesystem::path(configDbPath_).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        try {
            std::filesystem::create_directories(dir);
        } catch (const std::exception& e) {
            LOG_ERROR("API", "Failed to create config directory: " + std::string(e.what()));
            return false;
        }
    }
    
    // Initialize config manager
    bool success = ConfigManager::getInstance().initialize(configDbPath_);
    if (success) {
        LOG_INFO("API", "Configuration database initialized at " + configDbPath_);
    } else {
        LOG_ERROR("API", "Failed to initialize configuration database at " + configDbPath_);
    }
    
    return success;
}

bool Api::loadSavedConfig() {
    if (!ConfigManager::getInstance().isReady()) {
        LOG_ERROR("API", "Configuration database is not initialized");
        return false;
    }
    
    try {
        LOG_INFO("API", "Loading saved configuration from database");
        
        // Get all saved camera configurations
        auto allCameras = ConfigManager::getInstance().getAllCameraConfigs();
        if (allCameras.empty()) {
            LOG_INFO("API", "No saved camera configurations found");
            return true;
        }
        
        // Load each camera configuration
        for (auto it = allCameras.begin(); it != allCameras.end(); ++it) {
            try {
                std::string cameraId = it.key();
                nlohmann::json cameraConfig = it.value();
                
                LOG_INFO("API", "Loading camera configuration for ID: " + cameraId);
                
                // Create or get the camera
                std::shared_ptr<Camera> camera;
                if (CameraManager::getInstance().cameraExists(cameraId)) {
                    camera = CameraManager::getInstance().getCamera(cameraId);
                } else {
                    // Get camera name, handle type errors
                    std::string name = cameraId;
                    if (cameraConfig.contains("name") && cameraConfig["name"].is_string()) {
                        name = cameraConfig["name"].get<std::string>();
                    } else if (cameraConfig.contains("name")) {
                        LOG_WARN("API", "Invalid name format for camera ID: " + cameraId + ", using ID as name");
                    }
                    
                    camera = CameraManager::getInstance().createCamera(cameraId, name);
                }
                
                if (!camera) {
                    LOG_ERROR("API", "Failed to create camera with ID: " + cameraId);
                    continue;
                }
                
                // Set camera properties
                if (cameraConfig.contains("name") && cameraConfig["name"].is_string()) {
                    camera->setName(cameraConfig["name"]);
                }
                
                // Load source component if present
                if (cameraConfig.contains("source") && !cameraConfig["source"].is_null()) {
                    try {
                        auto sourceConfig = cameraConfig["source"];
                        
                        // Check that required fields exist and have the correct type
                        if (!sourceConfig.contains("type") || !sourceConfig["type"].is_string()) {
                            LOG_ERROR("API", "Missing or invalid type field in source component for camera " + cameraId);
                            continue;
                        }
                        
                        if (!sourceConfig.contains("id") || !sourceConfig["id"].is_string()) {
                            LOG_ERROR("API", "Missing or invalid id field in source component for camera " + cameraId);
                            continue;
                        }
                        
                        std::string type = sourceConfig["type"];
                        std::string id = sourceConfig["id"];
                        
                        // Extract config, defaulting to empty if missing or wrong type
                        nlohmann::json config = nlohmann::json::object();
                        if (sourceConfig.contains("config") && sourceConfig["config"].is_object()) {
                            config = sourceConfig["config"];
                        }
                        
                        // Remove any existing source component
                        camera->setSourceComponent(nullptr);
                        
                        auto source = ComponentFactory::getInstance().createSourceComponent(
                            id, camera.get(), type, config);
                            
                        if (source) {
                            camera->setSourceComponent(source);
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("API", "Error loading source component for camera " + cameraId + ": " + e.what());
                    }
                }
                
                // Load processor components
                if (cameraConfig.contains("processors") && cameraConfig["processors"].is_array()) {
                    // Clear existing processors
                    for (const auto& proc : camera->getProcessorComponents()) {
                        camera->removeProcessorComponent(proc->getId());
                    }
                    
                    // Add new processors from config
                    for (const auto& procConfig : cameraConfig["processors"]) {
                        try {
                            // Check that required fields exist and have the correct type
                            if (!procConfig.contains("type") || !procConfig["type"].is_string()) {
                                LOG_ERROR("API", "Missing or invalid type field in processor component for camera " + cameraId);
                                continue;
                            }
                            
                            if (!procConfig.contains("id") || !procConfig["id"].is_string()) {
                                LOG_ERROR("API", "Missing or invalid id field in processor component for camera " + cameraId);
                                continue;
                            }
                            
                            std::string type = procConfig["type"];
                            std::string id = procConfig["id"];
                            
                            // Extract config, defaulting to empty if missing or wrong type
                            nlohmann::json config = nlohmann::json::object();
                            if (procConfig.contains("config") && procConfig["config"].is_object()) {
                                config = procConfig["config"];
                            }
                            
                            // Explicitly set shared memory from global setting
                            bool useSharedMemory = GlobalConfig::getInstance().getUseSharedMemory();
                            config["use_shared_memory"] = useSharedMemory;
                            LOG_INFO("API", "Explicitly setting shared memory=" + 
                                     std::string(useSharedMemory ? "true" : "false") + 
                                     " for processor " + id);
                            
                            auto processor = ComponentFactory::getInstance().createProcessorComponent(
                                id, camera.get(), type, config);
                                
                            if (processor) {
                                camera->addProcessorComponent(processor);
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("API", "Error loading processor component for camera " + cameraId + ": " + e.what());
                        }
                    }
                }
                
                // Load sink components
                if (cameraConfig.contains("sinks") && cameraConfig["sinks"].is_array()) {
                    // Clear existing sinks
                    for (const auto& sink : camera->getSinkComponents()) {
                        camera->removeSinkComponent(sink->getId());
                    }
                    
                    // Add new sinks from config
                    for (const auto& sinkConfig : cameraConfig["sinks"]) {
                        try {
                            // Check that required fields exist and have the correct type
                            if (!sinkConfig.contains("type") || !sinkConfig["type"].is_string()) {
                                LOG_ERROR("API", "Missing or invalid type field in sink component for camera " + cameraId);
                                continue;
                            }
                            
                            if (!sinkConfig.contains("id") || !sinkConfig["id"].is_string()) {
                                LOG_ERROR("API", "Missing or invalid id field in sink component for camera " + cameraId);
                                continue;
                            }
                            
                            std::string type = sinkConfig["type"];
                            std::string id = sinkConfig["id"];
                            
                            // Extract config, defaulting to empty if missing or wrong type
                            nlohmann::json config = nlohmann::json::object();
                            if (sinkConfig.contains("config") && sinkConfig["config"].is_object()) {
                                config = sinkConfig["config"];
                            }
                            
                            auto sink = ComponentFactory::getInstance().createSinkComponent(
                                id, camera.get(), type, config);
                                
                            if (sink) {
                                camera->addSinkComponent(sink);
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("API", "Error loading sink component for camera " + cameraId + ": " + e.what());
                        }
                    }
                }
                
                // Start camera if it was running
                if (cameraConfig.contains("running") && cameraConfig["running"].get<bool>()) {
                    camera->start();
                }
                
                LOG_INFO("API", "Successfully loaded configuration for camera: " + cameraId);
            } catch (const std::exception& e) {
                LOG_ERROR("API", "Error processing camera " + it.key() + ": " + e.what());
                // Continue with next camera, don't abort entire loading process
            }
        }
        
        LOG_INFO("API", "Successfully loaded all camera configurations");
        
        // After loading all cameras, enforce license restrictions
        int stoppedCameras = enforceLicenseRestrictions();
        if (stoppedCameras > 0) {
            LOG_WARN("API", "License enforcement stopped " + std::to_string(stoppedCameras) + 
                     " camera(s) that were using features not allowed by the current license");
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("API", "Failed to load saved configuration: " + std::string(e.what()));
        return false;
    }
}

void Api::setupCORS() {
    auto& cors = app_.get_middleware<crow::CORSHandler>();
    cors.global()
        .headers("*")
        .methods("GET"_method, "POST"_method, "DELETE"_method, "PUT"_method, "OPTIONS"_method)
        .origin("*")
        .allow_credentials();
}

void Api::setupApiLogging() {
    // Initialize API logging configuration from database
    g_apiLoggingConfig.loadFromConfig();
    
    // Set up the middleware for API logging
    auto& api_logging = app_.get_middleware<ApiLoggingMiddleware>();
    
    LOG_INFO("API", "API logging middleware initialized");
    LOG_INFO("API", "API logging enabled: " + std::string(g_apiLoggingConfig.enabled ? "true" : "false"));
    LOG_INFO("API", "API logging slow request threshold: " + std::to_string(g_apiLoggingConfig.slow_request_threshold_ms) + "ms");
    LOG_INFO("API", "API logging timeout threshold: " + std::to_string(g_apiLoggingConfig.timeout_threshold_ms) + "ms");
}



void Api::configureServerConcurrency() {
    auto& configManager = ConfigManager::getInstance();
    
    // Get concurrency settings from configuration (with sensible defaults)
    int workerThreads = 16; // Default for moderate load
    int maxConnections = 1000; // Maximum concurrent connections
    
    if (configManager.isReady()) {
        auto threadConfig = configManager.getConfig("api_worker_threads");
        if (!threadConfig.empty() && threadConfig.is_number_integer()) {
            int configThreads = threadConfig.get<int>();
            // Ensure threads are within reasonable bounds (4 to 64)
            workerThreads = std::max(4, std::min(64, configThreads));
        }
        
        auto connConfig = configManager.getConfig("api_max_connections");
        if (!connConfig.empty() && connConfig.is_number_integer()) {
            int configConns = connConfig.get<int>();
            // Ensure connections are within reasonable bounds (100 to 10000)
            maxConnections = std::max(100, std::min(10000, configConns));
        }
    }
    
    // Configure thread pool size
    app_.concurrency(workerThreads);
    
    // Note: Crow doesn't expose max connections directly, but we log the intended value
    LOG_INFO("API", "Server concurrency configured:");
    LOG_INFO("API", "- Worker threads: " + std::to_string(workerThreads));
    LOG_INFO("API", "- Target max connections: " + std::to_string(maxConnections));
    LOG_INFO("API", "- Request timeout enabled with configurable timeouts");
    LOG_INFO("API", "- Background task system available for long operations");
}

void Api::setupRoutes() {
    std::cout << "Setting up API routes..." << std::endl;
    LOG_INFO("API", "Setting up all API routes");
    
    // Add a health endpoint for monitoring
    CROW_ROUTE(app_, "/health")
        .methods("GET"_method, "HEAD"_method)
    ([](const crow::request& req) {
        // For HEAD requests, we return just the status code
        if (req.method == "HEAD"_method) {
            return crow::response(crow::status::OK);
        }
        
        // For GET requests, we return a JSON response
        nlohmann::json response;
        response["status"] = "ok";
        response["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        return createJsonResponse(response);
    });
    
    CROW_ROUTE(app_, "/")
    ([]() {
        return "tAPI - Computer Vision Pipeline API";
    });

    // Set up domain-specific routes
    setupLicenseRoutes();
    setupCameraRoutes();
    setupComponentRoutes();
    setupDatabaseRoutes();
    setupManagementRoutes();
    setupFrameRoutes();
    setupBackgroundTaskRoutes();
    setupConfigRoutes();
    setupApiLoggingRoutes();
    
    LOG_INFO("API", "Finished setting up all API routes");
    std::cout << "All API routes set up successfully" << std::endl;
}

void Api::setupFrameRoutes() {
    // Get latest frame from a camera as JPEG
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/frame")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        if (!camera->isRunning()) {
            return crow::response(400, "Camera is not running");
        }
        
        // No need to explicitly call processFrame() anymore as it happens automatically
        // when the camera is running
        
        // Get the JPEG data
        int quality = 90; // Default quality
        
        // Check if quality parameter is provided
        auto qualityParam = req.url_params.get("quality");
        if (qualityParam) {
            try {
                quality = std::stoi(qualityParam);
                quality = std::max(1, std::min(100, quality)); // Clamp between 1 and 100
            } catch (...) {
                // Ignore parsing errors, use default quality
            }
        }
        
        auto jpegData = camera->getLatestFrameJpeg(quality);
        
        if (jpegData.empty()) {
            return crow::response(404, "No frame available");
        }
        
        // Create response with JPEG data
        crow::response resp;
        resp.set_header("Content-Type", "image/jpeg");
        resp.body = std::string(jpegData.begin(), jpegData.end());
        return resp;
    });
    
    // Get latest raw (unannotated) frame from a camera as JPEG
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/raw-frame")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        if (!camera->isRunning()) {
            return crow::response(400, "Camera is not running");
        }
        
        // Get the JPEG data
        int quality = 90; // Default quality
        
        // Check if quality parameter is provided
        auto qualityParam = req.url_params.get("quality");
        if (qualityParam) {
            try {
                quality = std::stoi(qualityParam);
                quality = std::max(1, std::min(100, quality)); // Clamp between 1 and 100
            } catch (...) {
                // Ignore parsing errors, use default quality
            }
        }
        
        auto jpegData = camera->getRawFrameJpeg(quality);
        
        if (jpegData.empty()) {
            return crow::response(404, "No raw frame available");
        }
        
        // Create response with JPEG data
        crow::response resp;
        resp.set_header("Content-Type", "image/jpeg");
        resp.body = std::string(jpegData.begin(), jpegData.end());
        return resp;
    });
    
    // Get camera frame status (metadata)
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/frame/status")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Get frame information
        cv::Mat frame = camera->getLatestFrame();
        
        nlohmann::json status;
        status["camera_id"] = cameraId;
        status["camera_name"] = camera->getName();
        status["running"] = camera->isRunning();
        
        if (!frame.empty()) {
            status["has_frame"] = true;
            status["frame_width"] = frame.cols;
            status["frame_height"] = frame.rows;
            status["frame_channels"] = frame.channels();
            status["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        } else {
            status["has_frame"] = false;
        }
        
        return crow::response(status.dump(2));
    });
    
    // Download video file from a file sink component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/sinks/<string>/video")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId, const std::string& sinkId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        auto sink = camera->getSinkComponent(sinkId);
        if (!sink) {
            return crow::response(404, "Sink not found");
        }
        
        // Cast to FileSink
        auto fileSink = std::dynamic_pointer_cast<FileSink>(sink);
        if (!fileSink) {
            return crow::response(400, "Sink is not a file sink");
        }
        
        std::string filePath = fileSink->getFilePath();
        
        // Open file
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            return crow::response(404, "Video file not found or inaccessible: " + filePath);
        }
        
        // Get file size
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read file content
        std::vector<char> buffer(fileSize);
        if (!file.read(buffer.data(), fileSize)) {
            return crow::response(500, "Failed to read video file: " + filePath);
        }
        
        // Create response with video data
        crow::response resp;
        resp.set_header("Content-Type", "video/mp4");
        resp.set_header("Content-Disposition", "attachment; filename=\"" + 
                         std::filesystem::path(filePath).filename().string() + "\"");
        resp.body = std::string(buffer.begin(), buffer.end());
        
        return resp;
    });
}

void Api::setupLicenseRoutes() {
    std::cout << "Setting up license routes..." << std::endl;
    LOG_INFO("API", "Setting up license routes");
    
    // Get license status
    CROW_ROUTE(app_, "/api/v1/license")
        .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req) {
        std::cout << "Handling GET request for /api/v1/license" << std::endl;
        // Skip license check for license endpoints
        const auto& licenseManager = CameraManager::getInstance().getLicenseManager();
        
        // Get detailed license information
        auto licenseInfo = licenseManager.getLicenseInfo();
        
        crow::response res(licenseInfo.dump(2));
        res.set_header("Content-Type", "application/json");
        return res;
    });
    
    // Set or update license key
    CROW_ROUTE(app_, "/api/v1/license")
        .methods(crow::HTTPMethod::Post)
    ([this](const crow::request& req) {
        std::cout << "Handling POST request for /api/v1/license" << std::endl;
        // Skip license check for license endpoints
        auto& licenseManager = const_cast<LicenseManager&>(CameraManager::getInstance().getLicenseManager());
        
        try {
            auto body = nlohmann::json::parse(req.body);
            if (!body.contains("license_key")) {
                return crow::response(400, "Missing license_key field");
            }
            
            std::string licenseKey = body["license_key"];
            
            // Special case: Empty license key is treated as a deactivation request
            if (licenseKey.empty() || licenseKey == "none" || licenseKey == "null") {
                LOG_INFO("API", "Empty license key provided, treating as deactivation request");
                
                // Call deleteLicense method
                if (licenseManager.deleteLicense()) {
                    // Reinitialize the CameraManager with a demo license key
                    CameraManager::getInstance().initialize("demo-license-key");
                    
                    // Remove license from the configuration database
                    ConfigManager::getInstance().deleteConfig("license_key");
                    LOG_INFO("API", "Removed license key from configuration database");
                    
                    // Enforce license restrictions after downgrading to demo license
                    int stoppedCameras = enforceLicenseRestrictions();
                    if (stoppedCameras > 0) {
                        LOG_WARN("API", "License deactivation stopped " + 
                                 std::to_string(stoppedCameras) + 
                                 " camera(s) using features not allowed by demo license");
                    }
                    
                    auto licenseInfo = licenseManager.getLicenseInfo();
                    licenseInfo["message"] = "License deactivated successfully";
                    
                    crow::response res(licenseInfo.dump(2));
                    res.set_header("Content-Type", "application/json");
                    return res;
                } else {
                    nlohmann::json errorResponse;
                    errorResponse["valid"] = false;
                    errorResponse["message"] = "Failed to deactivate license";
                    return crow::response(500, errorResponse.dump(2));
                }
            }
            
            // Get current license tier before the change
            LicenseTier previousTier = licenseManager.getLicenseTier();
            
            bool valid = licenseManager.verifyLicense(licenseKey);
            
            // Initialize CameraManager if license is valid
            if (valid) {
                licenseManager.setLicenseKey(licenseKey);
                
                // Save the license key to the configuration database
                ConfigManager::getInstance().setConfig("license_key", licenseKey);
                LOG_INFO("API", "Saved license key to configuration database");
                
                // Initialize the CameraManager if not already initialized
                if (!CameraManager::getInstance().isInitialized()) {
                    CameraManager::getInstance().initialize(licenseKey);
                    LOG_INFO("API", "CameraManager initialized with license key");
                }
                
                // Add owner and email if provided
                if (body.contains("owner") || body.contains("email")) {
                    licenseManager.updateLicense(body);
                    
                    // Save owner and email to config as well
                    if (body.contains("owner") && body["owner"].is_string()) {
                        ConfigManager::getInstance().setConfig("license_owner", body["owner"]);
                    }
                    if (body.contains("email") && body["email"].is_string()) {
                        ConfigManager::getInstance().setConfig("license_email", body["email"]);
                    }
                }
                
                // Check if license tier has changed
                LicenseTier currentTier = licenseManager.getLicenseTier();
                if (currentTier != previousTier) {
                    LOG_INFO("API", "License tier changed. Checking cameras for compliance with new license tier.");
                    
                    // If license tier has changed, enforce restrictions
                    int stoppedCameras = enforceLicenseRestrictions();
                    if (stoppedCameras > 0) {
                        LOG_WARN("API", "License tier change stopped " + 
                                 std::to_string(stoppedCameras) + 
                                 " camera(s) using features not allowed by the new license tier");
                    }
                }
                
                // Return the updated license info
                auto licenseInfo = licenseManager.getLicenseInfo();
                licenseInfo["message"] = "License key accepted";
                
                crow::response res(licenseInfo.dump(2));
                res.set_header("Content-Type", "application/json");
                return res;
            } else {
                nlohmann::json errorResponse;
                errorResponse["valid"] = false;
                errorResponse["message"] = "Invalid license key";
                
                return crow::response(400, errorResponse.dump(2));
            }
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Update existing license information
    CROW_ROUTE(app_, "/api/v1/license")
        .methods(crow::HTTPMethod::Put)
    ([this](const crow::request& req) {
        // Skip license check for license endpoints
        auto& licenseManager = const_cast<LicenseManager&>(CameraManager::getInstance().getLicenseManager());
        
        // First check if we have a valid license
        if (!licenseManager.hasValidLicense()) {
            nlohmann::json errorResponse;
            errorResponse["valid"] = false;
            errorResponse["message"] = "No valid license to update";
            return crow::response(401, errorResponse.dump(2));
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            // Get current license tier before update
            LicenseTier previousTier = licenseManager.getLicenseTier();
            
            if (licenseManager.updateLicense(body)) {
                // Save updates to the database
                if (body.contains("key") && body["key"].is_string()) {
                    ConfigManager::getInstance().setConfig("license_key", body["key"]);
                }
                if (body.contains("owner") && body["owner"].is_string()) {
                    ConfigManager::getInstance().setConfig("license_owner", body["owner"]);
                }
                if (body.contains("email") && body["email"].is_string()) {
                    ConfigManager::getInstance().setConfig("license_email", body["email"]);
                }
                
                // Check if license tier has changed
                LicenseTier currentTier = licenseManager.getLicenseTier();
                if (currentTier != previousTier) {
                    LOG_INFO("API", "License tier changed during update. Checking cameras for compliance.");
                    
                    // If license tier has changed, enforce restrictions
                    int stoppedCameras = enforceLicenseRestrictions();
                    if (stoppedCameras > 0) {
                        LOG_WARN("API", "License update stopped " + 
                                 std::to_string(stoppedCameras) + 
                                 " camera(s) using features not allowed by the new license tier");
                    }
                }
                
                auto licenseInfo = licenseManager.getLicenseInfo();
                licenseInfo["message"] = "License information updated";
                
                crow::response res(licenseInfo.dump(2));
                res.set_header("Content-Type", "application/json");
                return res;
            } else {
                nlohmann::json errorResponse;
                errorResponse["valid"] = false;
                errorResponse["message"] = "Failed to update license information";
                return crow::response(400, errorResponse.dump(2));
            }
        } catch (const std::exception& e) {
            nlohmann::json errorResponse;
            errorResponse["valid"] = false;
            errorResponse["message"] = std::string("Invalid request: ") + e.what();
            return crow::response(400, errorResponse.dump(2));
        }
    });
    
    // Delete license
    CROW_ROUTE(app_, "/api/v1/license")
        .methods(crow::HTTPMethod::Delete)
    ([this](const crow::request& req) {
        // Skip license check for license endpoints
        auto& licenseManager = const_cast<LicenseManager&>(CameraManager::getInstance().getLicenseManager());
        
        if (licenseManager.deleteLicense()) {
            // Reinitialize the CameraManager with an empty/demo license key to update internal state
            LOG_INFO("API", "License deleted, reinitializing CameraManager");
            CameraManager::getInstance().initialize("demo-license-key");
            
            // Remove license from configuration database
            ConfigManager::getInstance().deleteConfig("license_key");
            ConfigManager::getInstance().deleteConfig("license_owner");
            ConfigManager::getInstance().deleteConfig("license_email");
            LOG_INFO("API", "Removed license information from configuration database");
            
            // Enforce license restrictions after downgrading to demo license
            int stoppedCameras = enforceLicenseRestrictions();
            if (stoppedCameras > 0) {
                LOG_WARN("API", "License deletion stopped " + 
                         std::to_string(stoppedCameras) + 
                         " camera(s) using features not allowed by demo license");
            }
            
            nlohmann::json response;
            response["success"] = true;
            response["message"] = "License deleted successfully";
            
            crow::response res(response.dump(2));
            res.set_header("Content-Type", "application/json");
            return res;
        } else {
            return crow::response(500, "Failed to delete license");
        }
    });
    
    // New per-camera licensing endpoints
    
    // Get camera license status
    CROW_ROUTE(app_, "/api/v1/license/cameras")
        .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req) {
        std::cout << "Handling GET request for /api/v1/license/cameras" << std::endl;
        // Skip license check for license endpoints
        
        // Get tenant_id from query parameters
        std::string tenant_id = "default";
        auto url_params = req.url_params;
        if (url_params.get("tenant_id") != nullptr) {
            tenant_id = url_params.get("tenant_id");
        }
        
        const auto& cameraLicenseManager = CameraManager::getInstance().getCameraLicenseManager();
        
        // Get all camera licenses for this tenant
        nlohmann::json response;
        response["camera_count"] = 0;
        response["trial_limit"] = 2; // From CameraLicenseManager::TRIAL_CAMERA_LIMIT
        response["trial_cameras"] = 0;
        response["is_trial_limit_exceeded"] = false;
        response["cameras"] = nlohmann::json::array();
        
        // Get all cameras and check their license status
        auto cameras = CameraManager::getInstance().getAllCameras();
        int camera_count = 0;
        int trial_cameras = 0;
        
        for (const auto& camera : cameras) {
            // For now, assume all cameras belong to the requested tenant
            // In a real implementation, you'd filter by tenant_id
            camera_count++;
            
            nlohmann::json cameraInfo;
            cameraInfo["camera_id"] = camera->getId();
            cameraInfo["tenant_id"] = tenant_id;
            cameraInfo["mode"] = "FREE_TRIAL"; // Default mode for now
            cameraInfo["is_trial"] = true;
            cameraInfo["start_date"] = "2024-01-01T00:00:00Z"; // Placeholder
            cameraInfo["end_date"] = "2024-04-01T00:00:00Z";   // Placeholder
            cameraInfo["enabled_growth_packs"] = nlohmann::json::array();
            
            response["cameras"].push_back(cameraInfo);
            trial_cameras++;
        }
        
        response["camera_count"] = camera_count;
        response["trial_cameras"] = trial_cameras;
        response["is_trial_limit_exceeded"] = (trial_cameras > 2);
        
        crow::response res(response.dump(2));
        res.set_header("Content-Type", "application/json");
        return res;
    });
    
    // Get tenant information
    CROW_ROUTE(app_, "/api/v1/license/tenant")
        .methods(crow::HTTPMethod::Get)
    ([this](const crow::request& req) {
        std::cout << "Handling GET request for /api/v1/license/tenant" << std::endl;
        
        // Get tenant_id from query parameters
        std::string tenant_id = "default";
        auto url_params = req.url_params;
        if (url_params.get("tenant_id") != nullptr) {
            tenant_id = url_params.get("tenant_id");
        }
        
        nlohmann::json response;
        response["tenant_id"] = tenant_id;
        response["name"] = "Default Tenant";
        response["type"] = "standard";
        
        crow::response res(response.dump(2));
        res.set_header("Content-Type", "application/json");
        return res;
    });
}

void Api::setupCameraRoutes() {
    // Get all cameras
    CROW_ROUTE(app_, "/api/v1/cameras")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        nlohmann::json response = nlohmann::json::array();
        auto cameras = CameraManager::getInstance().getAllCameras();
        
        for (const auto& camera : cameras) {
            nlohmann::json cameraJson;
            cameraJson["id"] = camera->getId();
            cameraJson["name"] = camera->getName();
            cameraJson["running"] = camera->isRunning();
            
            // Count components by type
            size_t sourceCount = camera->getSourceComponent() ? 1 : 0;
            size_t processorCount = camera->getProcessorComponents().size();
            size_t sinkCount = camera->getSinkComponents().size();
            
            cameraJson["components"] = {
                {"source", sourceCount},
                {"processors", processorCount},
                {"sinks", sinkCount}
            };
            
            response.push_back(cameraJson);
        }
        
        crow::response result(response.dump(2));
        result.set_header("Content-Type", "application/json");
        return result;
    });
    
    // Create a new camera
    CROW_ROUTE(app_, "/api/v1/cameras")
        .methods("POST"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            std::string id = body.contains("id") ? body["id"].get<std::string>() : "";
            std::string name = body.contains("name") ? body["name"].get<std::string>() : "";
            
            auto camera = CameraManager::getInstance().createCamera(id, name);
            if (!camera) {
                return crow::response(500, "Failed to create camera");
            }
            
            // Save camera configuration to database
            saveCameraConfigToDB(camera->getId());
            
            nlohmann::json response;
            response["id"] = camera->getId();
            response["name"] = camera->getName();
            response["running"] = camera->isRunning();
            
            return createJsonResponse(response, 201);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Get a specific camera
    CROW_ROUTE(app_, "/api/v1/cameras/<string>")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        return createJsonResponse(camera->getStatus());
    });
    
    // Update a camera
    CROW_ROUTE(app_, "/api/v1/cameras/<string>")
        .methods("PUT"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            if (body.contains("name")) {
                camera->setName(body["name"]);
            }
            
            // Start/stop camera if requested
            if (body.contains("running")) {
                bool shouldRun = body["running"].get<bool>();
                if (shouldRun && !camera->isRunning()) {
                    // REMOVED the check for AI server availability here - let the Camera::start() handle this
                    // The components now have retry logic to handle temporary AI server unavailability
                    
                    if (!camera->start()) {
                        return crow::response(500, "Failed to start camera");
                    }
                } else if (!shouldRun && camera->isRunning()) {
                    if (!camera->stop()) {
                        return crow::response(500, "Failed to stop camera");
                    }
                }
            }
            
            // Save camera configuration to database
            saveCameraConfigToDB(cameraId);
            
            auto status = camera->getStatus();
            crow::response res = createJsonResponse(status);
            res.set_header("Content-Type", "application/json");
            return res;
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Delete a camera
    CROW_ROUTE(app_, "/api/v1/cameras/<string>")
        .methods("DELETE"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        // Check if the camera exists
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Check for "async" query parameter
        bool useAsync = false;
        auto asyncParam = req.url_params.get("async");
        if (asyncParam && (std::string(asyncParam) == "true" || std::string(asyncParam) == "1")) {
            useAsync = true;
        }
        
        // For synchronous deletion, perform the operation inline
        if (!useAsync) {
            // First, delete all database data for this camera
            bool databaseCleaned = false;
            // Look for database sinks and delete data
            for (const auto& sink : camera->getSinkComponents()) {
                auto dbSink = std::dynamic_pointer_cast<DatabaseSink>(sink);
                if (dbSink) {
                    databaseCleaned = dbSink->deleteDataForCamera(cameraId);
                    break; // Assuming there's only one database sink per camera
                }
            }
            
            // Delete camera configuration from database
            ConfigManager::getInstance().deleteCameraConfig(cameraId);
            
            if (!CameraManager::getInstance().deleteCamera(cameraId)) {
                return crow::response(404, "Camera not found");
            }
            
            nlohmann::json response;
            response["success"] = true;
            response["message"] = "Camera deleted";
            response["database_cleaned"] = databaseCleaned;
            
            return createJsonResponse(response);
        }
        
        // For asynchronous deletion, create a background task
        std::string taskId = BackgroundTaskManager::getInstance().submitTask(
            "camera_deletion", 
            cameraId,
            [cameraId](std::function<void(double, std::string)> progressCallback) -> bool {
                try {
                    LOG_INFO("API", "Background task starting camera deletion for: " + cameraId);
                    
                    // First update progress to indicate we're starting
                    progressCallback(10.0, "Starting camera deletion");
                    
                    auto camera = CameraManager::getInstance().getCamera(cameraId);
                    if (!camera) {
                        LOG_ERROR("API", "Camera not found for deletion: " + cameraId);
                        progressCallback(100.0, "Camera not found");
                        return false;
                    }
                    
                    LOG_INFO("API", "Found camera for deletion: " + cameraId);
                
                    // First stop the camera if it's running
                    if (camera->isRunning()) {
                        LOG_INFO("API", "Stopping running camera before deletion: " + cameraId);
                        progressCallback(20.0, "Stopping camera");
                        camera->stop();
                        LOG_INFO("API", "Camera stopped successfully: " + cameraId);
                    } else {
                        LOG_INFO("API", "Camera is already stopped: " + cameraId);
                    }
                
                progressCallback(30.0, "Deleting database records");
                
                // Delete all database data for this camera
                bool databaseCleaned = false;
                for (const auto& sink : camera->getSinkComponents()) {
                    auto dbSink = std::dynamic_pointer_cast<DatabaseSink>(sink);
                    if (dbSink) {
                        // Use the enhanced progress callback version
                        auto dbProgressCallback = [&progressCallback](double dbProgress, const std::string& dbMessage) {
                            // Map database progress (0-100) to our deletion progress range (30-70)
                            double mappedProgress = 30.0 + (dbProgress * 0.40); // 40% of total progress
                            progressCallback(mappedProgress, "Database: " + dbMessage);
                        };
                        
                        databaseCleaned = dbSink->deleteDataForCamera(cameraId, dbProgressCallback);
                        
                        // Final status update for database operation
                        if (databaseCleaned) {
                            progressCallback(70.0, "Database records deleted successfully");
                        } else {
                            progressCallback(70.0, "Failed to delete database records, continuing with camera deletion");
                        }
                        break; // Assuming there's only one database sink per camera
                    }
                }
                
                    // Delete camera configuration from database
                    LOG_INFO("API", "Deleting camera configuration from database: " + cameraId);
                    progressCallback(75.0, "Deleting camera configuration");
                    bool configDeleted = ConfigManager::getInstance().deleteCameraConfig(cameraId);
                    if (configDeleted) {
                        LOG_INFO("API", "Camera configuration deleted successfully: " + cameraId);
                    } else {
                        LOG_WARN("API", "Failed to delete camera configuration: " + cameraId);
                    }
                    
                    // Delete the camera itself
                    LOG_INFO("API", "Deleting camera from system: " + cameraId);
                    progressCallback(80.0, "Deleting camera from system");
                    bool success = CameraManager::getInstance().deleteCamera(cameraId);
                    
                    if (success) {
                        std::string successMsg = "Camera deleted successfully";
                        if (databaseCleaned) {
                            successMsg += " with database records";
                        }
                        LOG_INFO("API", "Camera deletion task completed successfully: " + cameraId);
                        progressCallback(100.0, successMsg);
                        return true;
                    } else {
                        LOG_ERROR("API", "Failed to delete camera from system: " + cameraId);
                        progressCallback(90.0, "Failed to delete camera");
                        return false;
                    }
                    
                } catch (const std::exception& e) {
                    LOG_ERROR("API", "Exception during camera deletion task for " + cameraId + ": " + e.what());
                    progressCallback(100.0, "Camera deletion failed due to exception: " + std::string(e.what()));
                    return false;
                } catch (...) {
                    LOG_ERROR("API", "Unknown exception during camera deletion task for " + cameraId);
                    progressCallback(100.0, "Camera deletion failed due to unknown exception");
                    return false;
                }
            }
        );
        
        // Return task information
        nlohmann::json response;
        response["success"] = true;
        response["message"] = "Camera deletion started";
        response["task_id"] = taskId;
        response["async"] = true;
        
        return createJsonResponse(response, 202); // 202 Accepted
    });
}

void Api::setupComponentRoutes() {
    // Get component types
    CROW_ROUTE(app_, "/api/v1/component-types")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto& factory = ComponentFactory::getInstance();
        LicenseTier currentTier = CameraManager::getInstance().getLicenseManager().getLicenseTier();
        
        nlohmann::json response;
        response["sources"] = factory.getAvailableSourceTypes();
        response["processors"] = factory.getAvailableProcessorTypes();
        response["sinks"] = factory.getAvailableSinkTypes();
        
        // Add tier information
        response["current_tier"] = static_cast<int>(currentTier);
        response["current_tier_name"] = [&]() {
            switch (currentTier) {
                case LicenseTier::NONE: return "None";
                case LicenseTier::BASIC: return "Basic";
                case LicenseTier::STANDARD: return "Standard";
                case LicenseTier::PROFESSIONAL: return "Professional";
                default: return "Unknown";
            }
        }();
        
        // Add permissions information
        nlohmann::json permissions;
        auto& permHelper = ComponentPermissionHelper::getInstance();
        
        // Source permissions
        nlohmann::json sourcePermissions = nlohmann::json::object();
        for (const auto& sourceType : factory.getAvailableSourceTypes()) {
            sourcePermissions[sourceType] = permHelper.isComponentAllowed(
                ComponentCategory::SOURCE, sourceType, currentTier);
        }
        permissions["source"] = sourcePermissions;
        
        // Processor permissions
        nlohmann::json processorPermissions = nlohmann::json::object();
        for (const auto& processorType : factory.getAvailableProcessorTypes()) {
            processorPermissions[processorType] = permHelper.isComponentAllowed(
                ComponentCategory::PROCESSOR, processorType, currentTier);
        }
        permissions["processor"] = processorPermissions;
        
        // Sink permissions
        nlohmann::json sinkPermissions = nlohmann::json::object();
        for (const auto& sinkType : factory.getAvailableSinkTypes()) {
            sinkPermissions[sinkType] = permHelper.isComponentAllowed(
                ComponentCategory::SINK, sinkType, currentTier);
        }
        permissions["sink"] = sinkPermissions;
        
        response["permissions"] = permissions;
        
        // Add dependency information
        nlohmann::json dependencies;
        
        // Specify component dependencies
        // Key: component type, Value: array of required component types
        dependencies["object_tracking"] = {"object_detection"};  // Tracking requires detection
        dependencies["line_zone_manager"] = {"object_tracking"}; // Line crossing requires tracking
        dependencies["polygon_zone_manager"] = {"object_tracking"}; // Polygon zone requires tracking
        dependencies["object_classification"] = {}; // Classification doesn't require other processors
        
        // Add global dependency rules
        nlohmann::json rules;
        rules.push_back("All processors require a source component");
        rules.push_back("All sinks require a source component");
        
        response["dependencies"] = dependencies;
        response["dependency_rules"] = rules;
        
        return createJsonResponse(response);
    });

    // Get object detection models
    CROW_ROUTE(app_, "/api/v1/models/object-detection")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        try {
            // Get server URL from GlobalConfig
            std::string serverUrl = GlobalConfig::getInstance().getAiServerUrl();
            LOG_INFO("API", "Using AI server URL from GlobalConfig: " + serverUrl);

            // URL parameter can override for testing purposes
            auto urlParam = req.url_params.get("server_url");
            if (urlParam) {
                serverUrl = urlParam;
                LOG_INFO("API", "Using AI server URL from request parameter: " + serverUrl);
            }
            
            // Get available models
            auto modelHealth = ObjectDetectorProcessor::getModelHealth(serverUrl);
            
            return createJsonResponse(modelHealth);
        } catch (const std::exception& e) {
            nlohmann::json errorJson;
            errorJson["error"] = e.what();
            return createJsonResponse(errorJson, 500);
        }
    });

    // Get comprehensive model metadata with classes and configuration
    CROW_ROUTE(app_, "/api/v1/models/metadata")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        try {
            // Get server URL from GlobalConfig
            std::string serverUrl = GlobalConfig::getInstance().getAiServerUrl();
            LOG_INFO("API", "Using AI server URL from GlobalConfig: " + serverUrl);

            // URL parameter can override for testing purposes
            auto urlParam = req.url_params.get("server_url");
            if (urlParam) {
                serverUrl = urlParam;
                LOG_INFO("API", "Using AI server URL from request parameter: " + serverUrl);
            }
            
            // Create the comprehensive model metadata response
            nlohmann::json metadata;
            metadata["server_url"] = serverUrl;
            metadata["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            
            // Define our source of truth for model metadata
            nlohmann::json modelSourceOfTruth = nlohmann::json::object();
            
            // COCO 80 classes - source of truth
            std::vector<std::string> cocoClasses = {
                "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
                "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
                "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
                "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
                "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
                "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
                "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard",
                "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase",
                "scissors", "teddy bear", "hair drier", "toothbrush"
            };
            
            // YOLOv7 models configuration
            modelSourceOfTruth["yolov7"] = {
                {"type", "object_detection"},
                {"framework", "ONNX"},
                {"classes", cocoClasses},
                {"num_classes", 80},
                {"input_shape", nlohmann::json::array({1, 3, 640, 640})},
                {"input_format", "NCHW"},
                {"input_dtype", "FP32"},
                {"output_format", "yolo"},
                {"description", "YOLOv7 object detection model"},
                {"preprocessing", {
                    {"normalize", true},
                    {"mean", nlohmann::json::array({0.0, 0.0, 0.0})},
                    {"std", nlohmann::json::array({255.0, 255.0, 255.0})}
                }}
            };
            
            modelSourceOfTruth["yolov7_qat"] = {
                {"type", "object_detection"},
                {"framework", "ONNX"},
                {"classes", cocoClasses},
                {"num_classes", 80},
                {"input_shape", nlohmann::json::array({1, 3, 640, 640})},
                {"input_format", "NCHW"},
                {"input_dtype", "FP32"},
                {"output_format", "yolo"},
                {"description", "YOLOv7 quantized object detection model"},
                {"quantized", true},
                {"preprocessing", {
                    {"normalize", true},
                    {"mean", nlohmann::json::array({0.0, 0.0, 0.0})},
                    {"std", nlohmann::json::array({255.0, 255.0, 255.0})}
                }}
            };
            
            // Get available models from Triton
            std::vector<std::string> availableModels;
            bool tritonConnected = false;
            std::string tritonStatus = "unknown";
            
            try {
                availableModels = ObjectDetectorProcessor::getAvailableModels(serverUrl);
                tritonConnected = true;
                tritonStatus = "connected";
                LOG_INFO("API", "Successfully connected to Triton server, found " + std::to_string(availableModels.size()) + " models");
            } catch (const std::exception& e) {
                LOG_WARN("API", "Failed to connect to Triton server: " + std::string(e.what()));
                tritonStatus = "disconnected";
                tritonConnected = false;
            }
            
            metadata["triton_status"] = tritonStatus;
            metadata["triton_connected"] = tritonConnected;
            
            // Build the comprehensive models list
            nlohmann::json models = nlohmann::json::array();
            
            // Process each model in our source of truth
            for (auto& [modelId, modelConfig] : modelSourceOfTruth.items()) {
                nlohmann::json modelEntry = modelConfig;
                modelEntry["model_id"] = modelId;
                
                // Check if this model is available on Triton
                bool availableOnTriton = std::find(availableModels.begin(), availableModels.end(), modelId) != availableModels.end();
                modelEntry["available_on_triton"] = availableOnTriton;
                modelEntry["status"] = availableOnTriton ? "ready" : "not_available";
                
                // Add runtime information if available on Triton
                if (availableOnTriton && tritonConnected) {
                    modelEntry["runtime_status"] = "loaded";
                    modelEntry["server_url"] = serverUrl;
                } else {
                    modelEntry["runtime_status"] = "not_loaded";
                }
                
                models.push_back(modelEntry);
            }
            
            // Add any additional models found on Triton that aren't in our source of truth
            for (const auto& modelId : availableModels) {
                bool inSourceOfTruth = modelSourceOfTruth.contains(modelId);
                if (!inSourceOfTruth) {
                    nlohmann::json unknownModel = {
                        {"model_id", modelId},
                        {"type", "object_detection"},
                        {"framework", "unknown"},
                        {"description", "Model found on Triton server but not in local metadata"},
                        {"available_on_triton", true},
                        {"status", "ready"},
                        {"runtime_status", "loaded"},
                        {"server_url", serverUrl},
                        {"classes", nlohmann::json::array()},
                        {"num_classes", 0},
                        {"note", "Classes and configuration unknown - not in source of truth"}
                    };
                    models.push_back(unknownModel);
                }
            }
            
            metadata["models"] = models;
            metadata["total_models"] = models.size();
            metadata["models_available_on_triton"] = availableModels.size();
            
            return createJsonResponse(metadata);
        } catch (const std::exception& e) {
            nlohmann::json errorJson;
            errorJson["error"] = e.what();
            errorJson["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            return createJsonResponse(errorJson, 500);
        }
    });

    // Get all components for a camera
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/components")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        nlohmann::json response;
        
        // Get source component
        auto source = camera->getSourceComponent();
        if (source) {
            response["source"] = source->getStatus();
        } else {
            response["source"] = nullptr;
        }
        
        // Get processor components
        nlohmann::json processors = nlohmann::json::array();
        for (const auto& processor : camera->getProcessorComponents()) {
            processors.push_back(processor->getStatus());
        }
        response["processors"] = processors;
        
        // Get sink components
        nlohmann::json sinks = nlohmann::json::array();
        for (const auto& sink : camera->getSinkComponents()) {
            sinks.push_back(sink->getStatus());
        }
        response["sinks"] = sinks;
        
        return createJsonResponse(response);
    });
    
    // Create a source component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/source")
        .methods("POST"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            if (!body.contains("type")) {
                return crow::response(400, "Missing component type");
            }
            
            std::string type = body["type"];
            std::string id = body.contains("id") ? body["id"].get<std::string>() : "";
            
            // Generate component ID if not provided
            if (id.empty()) {
                uuid_t uuid;
                char uuid_str[37];
                uuid_generate(uuid);
                uuid_unparse_lower(uuid, uuid_str);
                id = std::string(uuid_str);
            }
            
            // Extract config if provided
            nlohmann::json config = body.contains("config") ? body["config"] : nlohmann::json();
            
            // Create the source component
            auto source = ComponentFactory::getInstance().createSourceComponent(
                id, camera.get(), type, config);
            
            if (!source) {
                return crow::response(500, "Failed to create source component");
            }
            
            // Add to camera
            if (!camera->setSourceComponent(source)) {
                return crow::response(500, "Failed to add source component to camera");
            }
            
            // Save camera configuration
            saveCameraConfigToDB(cameraId);
            
            return createJsonResponse(source->getStatus(), 201);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Update a source component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/source")
        .methods("PUT"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        auto source = camera->getSourceComponent();
        if (!source) {
            return crow::response(404, "Source component not found");
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            // Update config if provided
            if (body.contains("config")) {
                if (!source->updateConfig(body["config"])) {
                    return crow::response(500, "Failed to update source component config");
                }
            }
            
            // Save camera configuration
            saveCameraConfigToDB(cameraId);
            
            return createJsonResponse(source->getStatus());
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Delete a source component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/source")
        .methods("DELETE"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        if (!camera->getSourceComponent()) {
            return crow::response(404, "Source component not found");
        }
        
        camera->setSourceComponent(nullptr);
        
        // Save camera configuration
        saveCameraConfigToDB(cameraId);
        
        nlohmann::json response;
        response["success"] = true;
        response["message"] = "Source component deleted";
        
        return createJsonResponse(response);
    });
    
    // Create a processor component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/processors")
        .methods("POST"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            if (!body.contains("type")) {
                return crow::response(400, "Missing component type");
            }
            
            std::string type = body["type"];
            std::string id = body.contains("id") ? body["id"].get<std::string>() : "";
            
            // Generate component ID if not provided
            if (id.empty()) {
                uuid_t uuid;
                char uuid_str[37];
                uuid_generate(uuid);
                uuid_unparse_lower(uuid, uuid_str);
                id = std::string(uuid_str);
            }
            
            // Extract config if provided
            nlohmann::json config = body.contains("config") ? body["config"] : nlohmann::json();
            
            // Always use GlobalConfig for server URL for object_detection processors
            if (type == "object_detection") {
                // Server URL setting is handled by ComponentFactory using GlobalConfig
                LOG_INFO("API", "Server URL for processor will be set by ComponentFactory from GlobalConfig");
            }
            
            // Add shared memory setting for applicable processors
            if (type == "object_detection") {
                // Use GlobalConfig to get shared memory setting
                bool useSharedMemory = GlobalConfig::getInstance().getUseSharedMemory();
                config["use_shared_memory"] = useSharedMemory;
                LOG_INFO("API", "Using shared memory setting from GlobalConfig for processor: " + 
                         std::string(useSharedMemory ? "true" : "false"));
            }
            
            // Create the processor component
            auto processor = ComponentFactory::getInstance().createProcessorComponent(
                id, camera.get(), type, config);
            
            if (!processor) {
                return crow::response(500, "Failed to create processor component");
            }
            
            // Add to camera
            if (!camera->addProcessorComponent(processor)) {
                return crow::response(500, "Failed to add processor component to camera");
            }
            
            // Save camera configuration
            saveCameraConfigToDB(cameraId);
            
            return createJsonResponse(processor->getStatus(), 201);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Get a specific processor component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/processors/<string>")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId, const std::string& processorId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        auto processor = camera->getProcessorComponent(processorId);
        if (!processor) {
            return crow::response(404, "Processor component not found");
        }
        
        return createJsonResponse(processor->getStatus());
    });
    
    // Update a processor component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/processors/<string>")
        .methods("PUT"_method)
    ([this](const crow::request& req, const std::string& cameraId, const std::string& processorId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        auto processor = camera->getProcessorComponent(processorId);
        if (!processor) {
            return crow::response(404, "Processor component not found");
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            // Update config if provided
            if (body.contains("config")) {
                if (!processor->updateConfig(body["config"])) {
                    return crow::response(500, "Failed to update processor component config");
                }
            }
            
            // Save camera configuration
            saveCameraConfigToDB(cameraId);
            
            return createJsonResponse(processor->getStatus());
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Delete a processor component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/processors/<string>")
        .methods("DELETE"_method)
    ([this](const crow::request& req, const std::string& cameraId, const std::string& processorId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        if (!camera->removeProcessorComponent(processorId)) {
            return crow::response(404, "Processor component not found");
        }
        
        // Save camera configuration
        saveCameraConfigToDB(cameraId);
        
        nlohmann::json response;
        response["success"] = true;
        response["message"] = "Processor component deleted";
        
        return createJsonResponse(response);
    });
    
    // Create a sink component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/sinks")
        .methods("POST"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            if (!body.contains("type")) {
                return crow::response(400, "Missing component type");
            }
            
            std::string type = body["type"];
            std::string id = body.contains("id") ? body["id"].get<std::string>() : "";
            
            // Generate component ID if not provided
            if (id.empty()) {
                uuid_t uuid;
                char uuid_str[37];
                uuid_generate(uuid);
                uuid_unparse_lower(uuid, uuid_str);
                id = std::string(uuid_str);
            }
            
            // Extract config if provided
            nlohmann::json config = body.contains("config") ? body["config"] : nlohmann::json();
            
            // Create the sink component
            auto sink = ComponentFactory::getInstance().createSinkComponent(
                id, camera.get(), type, config);
            
            if (!sink) {
                return crow::response(500, "Failed to create sink component");
            }
            
            // Add to camera
            if (!camera->addSinkComponent(sink)) {
                return crow::response(500, "Failed to add sink component to camera");
            }
            
            // Save camera configuration
            saveCameraConfigToDB(cameraId);
            
            return createJsonResponse(sink->getStatus(), 201);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Get a specific sink component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/sinks/<string>")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId, const std::string& sinkId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        auto sink = camera->getSinkComponent(sinkId);
        if (!sink) {
            return crow::response(404, "Sink component not found");
        }
        
        return createJsonResponse(sink->getStatus());
    });
    
    // Update a sink component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/sinks/<string>")
        .methods("PUT"_method)
    ([this](const crow::request& req, const std::string& cameraId, const std::string& sinkId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        auto sink = camera->getSinkComponent(sinkId);
        if (!sink) {
            return crow::response(404, "Sink component not found");
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            // Update config if provided
            if (body.contains("config")) {
                if (!sink->updateConfig(body["config"])) {
                    return crow::response(500, "Failed to update sink component config");
                }
            }
            
            // Save camera configuration
            saveCameraConfigToDB(cameraId);
            
            return createJsonResponse(sink->getStatus());
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Delete a sink component
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/sinks/<string>")
        .methods("DELETE"_method)
    ([this](const crow::request& req, const std::string& cameraId, const std::string& sinkId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        if (!camera->removeSinkComponent(sinkId)) {
            return crow::response(404, "Sink component not found");
        }
        
        // Save camera configuration
        saveCameraConfigToDB(cameraId);
        
        nlohmann::json response;
        response["success"] = true;
        response["message"] = "Sink component deleted";
        
        return createJsonResponse(response);
    });
}

void Api::setupDatabaseRoutes() {

    // Get analytics data for visualizations
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/analytics")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received analytics data GET request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            std::cout << "License check failed for analytics data request" << std::endl;
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            std::cout << "Camera not found for analytics data request: " << cameraId << std::endl;
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            std::cout << "No database sink found for camera: " << cameraId << std::endl;
            return crow::response(404, "No database sink found for this camera");
        }
        
        // Default to sync for analytics - use async only when explicitly requested or for very large datasets
        bool useAsync = false;
        auto asyncParam = req.url_params.get("async");
        if (asyncParam && (std::string(asyncParam) == "true" || std::string(asyncParam) == "1")) {
            useAsync = true;
            LOG_INFO("API", "Asynchronous analytics query requested");
        }
        
        // Auto-detect if we should use async based on data size (optional future enhancement)
        // For now, keep it simple and default to sync
        
        if (useAsync) {
            // Create background task for analytics query
            std::string taskId = BackgroundTaskManager::getInstance().submitTask(
                "database_analytics", 
                cameraId,
                [dbSink, cameraId](std::function<void(double, std::string)> progressCallback) -> bool {
                    try {
                        progressCallback(10.0, "Starting analytics query");
                        
                        auto analyticsData = dbSink->getAnalytics(cameraId);
                        
                        progressCallback(100.0, "Analytics query completed");
                        return analyticsData.contains("success") ? 
                               analyticsData["success"].get<bool>() : true;
                    } catch (const std::exception& e) {
                        progressCallback(100.0, std::string("Analytics error: ") + e.what());
                        return false;
                    }
                }
            );
            
            nlohmann::json response;
            response["success"] = true;
            response["message"] = "Analytics query started";
            response["task_id"] = taskId;
            response["async"] = true;
            
            return createJsonResponse(response, 202);
        }
        
        try {
            // Get analytics data from the database sink
            nlohmann::json analyticsData = dbSink->getAnalytics(cameraId);
            
            return createJsonResponse(analyticsData);
        } catch (const std::exception& e) {
            std::cout << "Exception in analytics query: " << e.what() << std::endl;
            return crow::response(500, std::string("Analytics error: ") + e.what());
        }
    });

    // Get time series data for visualizations
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/time-series")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received time series data GET request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            std::cout << "License check failed for time series data request" << std::endl;
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            std::cout << "Camera not found for time series data request: " << cameraId << std::endl;
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            std::cout << "No database sink found for camera: " << cameraId << std::endl;
            return crow::response(404, "No database sink found for this camera");
        }
        
        // Parse optional time range parameters
        int64_t start_time = 0;
        int64_t end_time = 0;
        
        auto startParam = req.url_params.get("start_time");
        if (startParam) {
            try {
                start_time = std::stoll(startParam);
            } catch (...) {
                // Ignore parsing errors, use default
            }
        }
        
        auto endParam = req.url_params.get("end_time");
        if (endParam) {
            try {
                end_time = std::stoll(endParam);
            } catch (...) {
                // Ignore parsing errors, use default
            }
        }
        
        // Default to sync for time series - use async only when explicitly requested
        bool useAsync = false;
        auto asyncParam = req.url_params.get("async");
        if (asyncParam && (std::string(asyncParam) == "true" || std::string(asyncParam) == "1")) {
            useAsync = true;
            LOG_INFO("API", "Asynchronous time series query requested");
        }
        
        if (useAsync) {
            // Create background task for time series query
            std::string taskId = BackgroundTaskManager::getInstance().submitTask(
                "database_timeseries", 
                cameraId,
                [dbSink, cameraId, start_time, end_time](std::function<void(double, std::string)> progressCallback) -> bool {
                    try {
                        progressCallback(10.0, "Starting time series query");
                        
                        auto timeSeriesData = dbSink->getTimeSeriesData(cameraId, start_time, end_time);
                        
                        progressCallback(100.0, "Time series query completed");
                        return !timeSeriesData.empty();
                    } catch (const std::exception& e) {
                        progressCallback(100.0, std::string("Time series error: ") + e.what());
                        return false;
                    }
                }
            );
            
            nlohmann::json response;
            response["success"] = true;
            response["message"] = "Time series query started";
            response["task_id"] = taskId;
            response["async"] = true;
            
            return createJsonResponse(response, 202);
        }
        
        try {
            // Get time series data from the database sink
            nlohmann::json timeSeriesData = dbSink->getTimeSeriesData(cameraId, start_time, end_time);
            
            return createJsonResponse(timeSeriesData);
        } catch (const std::exception& e) {
            std::cout << "Exception in time series query: " << e.what() << std::endl;
            return crow::response(500, std::string("Time series error: ") + e.what());
        }
    });

    // Get dwell time analytics data for visualizations
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/dwell-time")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received dwell time analytics GET request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            std::cout << "License check failed for dwell time analytics request" << std::endl;
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            std::cout << "Camera not found for dwell time analytics request: " << cameraId << std::endl;
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            std::cout << "No database sink found for camera: " << cameraId << std::endl;
            return crow::response(404, "No database sink found for this camera");
        }
        
        // Parse optional time range parameters
        int64_t start_time = 0;
        int64_t end_time = 0;
        
        auto startParam = req.url_params.get("start_time");
        if (startParam) {
            try {
                start_time = std::stoll(startParam);
            } catch (...) {
                // Ignore parsing errors, use default
            }
        }
        
        auto endParam = req.url_params.get("end_time");
        if (endParam) {
            try {
                end_time = std::stoll(endParam);
            } catch (...) {
                // Ignore parsing errors, use default
            }
        }
        
        // Default to sync for dwell time analytics - use async only when explicitly requested
        bool useAsync = false;
        auto asyncParam = req.url_params.get("async");
        if (asyncParam && (std::string(asyncParam) == "true" || std::string(asyncParam) == "1")) {
            useAsync = true;
            LOG_INFO("API", "Asynchronous dwell time query requested");
        }
        
        if (useAsync) {
            // Create background task for dwell time query
            std::string taskId = BackgroundTaskManager::getInstance().submitTask(
                "database_dwelltime", 
                cameraId,
                [dbSink, cameraId, start_time, end_time](std::function<void(double, std::string)> progressCallback) -> bool {
                    try {
                        progressCallback(10.0, "Starting dwell time analytics query");
                        
                        auto dwellTimeData = dbSink->getDwellTimeAnalytics(cameraId, start_time, end_time);
                        
                        progressCallback(100.0, "Dwell time analytics query completed");
                        return !dwellTimeData.empty();
                    } catch (const std::exception& e) {
                        progressCallback(100.0, std::string("Dwell time error: ") + e.what());
                        return false;
                    }
                }
            );
            
            nlohmann::json response;
            response["success"] = true;
            response["message"] = "Dwell time analytics query started";
            response["task_id"] = taskId;
            response["async"] = true;
            
            return createJsonResponse(response, 202);
        }
        
        try {
            // Get dwell time analytics data from the database sink
            nlohmann::json dwellTimeData = dbSink->getDwellTimeAnalytics(cameraId, start_time, end_time);
            
            return createJsonResponse(dwellTimeData);
        } catch (const std::exception& e) {
            std::cout << "Exception in dwell time analytics query: " << e.what() << std::endl;
            return crow::response(500, std::string("Dwell time analytics error: ") + e.what());
        }
    });

    // Get zone line counts for visualization
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/zone-line-counts")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received zone line counts GET request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            return crow::response(404, "No database sink found for this camera");
        }
        
        try {
            // Parse optional time range parameters
            int64_t start_time = 0;
            int64_t end_time = 0;
            
            auto startParam = req.url_params.get("start_time");
            if (startParam) {
                try {
                    start_time = std::stoll(startParam);
                } catch (...) {
                    // Ignore parsing errors, use default
                }
            }
            
            auto endParam = req.url_params.get("end_time");
            if (endParam) {
                try {
                    end_time = std::stoll(endParam);
                } catch (...) {
                    // Ignore parsing errors, use default
                }
            }
            
            // Get zone line counts from database
            nlohmann::json lineCountsData = dbSink->getZoneLineCounts(cameraId, start_time, end_time);
            
            // Check if data is empty
            if (lineCountsData.empty()) {
                nlohmann::json errorResponse;
                errorResponse["error"] = "No zone line count data available";
                errorResponse["success"] = false;
                errorResponse["has_data"] = false;
                return createJsonResponse(errorResponse, 204); // 204 No Content
            }
            
            // Validate that data format includes direction field
            // This ensures backward compatibility if database doesn't have direction info yet
            for (auto& item : lineCountsData) {
                if (!item.contains("direction")) {
                    item["direction"] = "unknown"; // Default direction value for backwards compatibility
                }
            }
            
            // Create response wrapper
            nlohmann::json response;
            response["zone_line_counts"] = lineCountsData;
            response["success"] = true;
            response["has_data"] = true;
            
            return createJsonResponse(response);
        } catch (const std::exception& e) {
            std::cout << "Exception in zone line counts query: " << e.what() << std::endl;
            return crow::response(500, std::string("Zone line counts error: ") + e.what());
        }
    });

    // Get class-based heatmap data for visualization
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/class-heatmap")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received class heatmap GET request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            return crow::response(404, "No database sink found for this camera");
        }
        
        try {
            // Get class-based heatmap data from database
            nlohmann::json heatmapData = dbSink->getClassBasedHeatmapData(cameraId);
            
            // Create response wrapper
            nlohmann::json response;
            response["class_heatmap_data"] = heatmapData;
            
            return createJsonResponse(response);
        } catch (const std::exception& e) {
            std::cout << "Exception in class heatmap query: " << e.what() << std::endl;
            return crow::response(500, std::string("Class heatmap error: ") + e.what());
        }
    });

    // New endpoint: Get heatmap image
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/heatmap-image")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received heatmap image GET request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            return crow::response(404, "No database sink found for this camera");
        }
        
        try {
            // Get anchor parameter (default to CENTER if not specified)
            std::string anchorStr = "CENTER";
            auto anchorParam = req.url_params.get("anchor");
            if (anchorParam) {
                anchorStr = anchorParam;
            }
            
            // Parse anchor parameter
            DatabaseSink::BBoxAnchor anchor = dbSink->stringToAnchor(anchorStr);
            
            // Get quality parameter (default to 90 if not specified)
            int quality = 90;
            auto qualityParam = req.url_params.get("quality");
            if (qualityParam) {
                try {
                    quality = std::stoi(qualityParam);
                    quality = std::max(1, std::min(100, quality)); // Clamp to 1-100
                } catch (...) {
                    // Ignore parsing errors, use default
                }
            }
            
            // Get class filter parameter (optional)
            std::vector<std::string> classFilter;
            auto classFilterParam = req.url_params.get("class");
            if (classFilterParam) {
                // Parse comma-separated list of classes
                std::string classFilterStr = classFilterParam;
                std::istringstream classStream(classFilterStr);
                std::string className;
                
                while (std::getline(classStream, className, ',')) {
                    if (!className.empty()) {
                        classFilter.push_back(className);
                    }
                }
            }
            
            // Check if we have any detection data available
            nlohmann::json heatmapData = dbSink->getClassBasedHeatmapData(cameraId);
            if (heatmapData.empty()) {
                // Return a proper error response instead of an empty image
                nlohmann::json errorResponse;
                errorResponse["error"] = "No detection data available";
                errorResponse["success"] = false;
                errorResponse["has_data"] = false;
                // Create the response with 200 status instead of 204
                crow::response resp = createJsonResponse(errorResponse, 200);
                resp.set_header("Content-Type", "application/json");
                resp.set_header("Access-Control-Allow-Origin", "*");
                resp.set_header("Access-Control-Allow-Methods", "GET");
                resp.set_header("Access-Control-Allow-Headers", "*");
                return resp;
            }
            
            // Generate heatmap image
            std::vector<uchar> imageData = dbSink->generateHeatmapImage(
                cameraId, cv::Mat(), anchor, classFilter, quality);
            
            if (imageData.empty()) {
                // Return a more graceful error response that can be handled by the frontend
                nlohmann::json errorResponse;
                errorResponse["error"] = "Failed to generate heatmap image";
                errorResponse["success"] = false;
                errorResponse["has_data"] = false;
                // Create the response with 200 status instead of 204
                crow::response resp = createJsonResponse(errorResponse, 200);
                resp.set_header("Content-Type", "application/json");
                resp.set_header("Access-Control-Allow-Origin", "*");
                resp.set_header("Access-Control-Allow-Methods", "GET");
                resp.set_header("Access-Control-Allow-Headers", "*");
                return resp;
            }
            
            // Create response with image data
            crow::response resp;
            resp.set_header("Content-Type", "image/jpeg");
            resp.body = std::string(imageData.begin(), imageData.end());
            return resp;
        } catch (const std::exception& e) {
            std::cout << "Exception in heatmap image generation: " << e.what() << std::endl;
            // Return a more graceful error response that can be handled by the frontend
            nlohmann::json errorResponse;
            errorResponse["error"] = std::string("Heatmap image error: ") + e.what();
            errorResponse["success"] = false;
            errorResponse["has_data"] = false;
            // Create the response with 200 status instead of 204
            crow::response resp = createJsonResponse(errorResponse, 200);
            resp.set_header("Content-Type", "application/json");
            resp.set_header("Access-Control-Allow-Origin", "*");
            resp.set_header("Access-Control-Allow-Methods", "GET");
            resp.set_header("Access-Control-Allow-Headers", "*");
            return resp;
        }
    });

    // New endpoint: Get available class names for filtering heatmap
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/available-classes")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received available classes GET request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            return crow::response(404, "No database sink found for this camera");
        }
        
        try {
            // Get available class names
            std::vector<std::string> classNames = dbSink->getAvailableClasses(cameraId);
            
            // Convert to JSON array
            nlohmann::json classesArray = nlohmann::json::array();
            for (const auto& className : classNames) {
                classesArray.push_back(className);
            }
            
            // Create response
            nlohmann::json response;
            response["classes"] = classesArray;
            response["count"] = classNames.size();
            
            return createJsonResponse(response);
        } catch (const std::exception& e) {
            std::cout << "Exception in available classes query: " << e.what() << std::endl;
            return crow::response(500, std::string("Available classes error: ") + e.what());
        }
    });

    // Get database performance statistics
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/performance")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received database performance GET request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            return crow::response(404, "No database sink found for this camera");
        }
        
        try {
            // Get performance statistics
            nlohmann::json performanceStats = dbSink->getDatabasePerformanceStats(cameraId);
            
            return createJsonResponse(performanceStats);
        } catch (const std::exception& e) {
            std::cout << "Exception in database performance query: " << e.what() << std::endl;
            return crow::response(500, std::string("Database performance error: ") + e.what());
        }
    });

    // Explain query execution plan for optimization
    CROW_ROUTE(app_, "/api/v1/cameras/<string>/database/explain")
        .methods("POST"_method)
    ([this](const crow::request& req, const std::string& cameraId) {
        std::cout << "Received query explain POST request for camera: " << cameraId << std::endl;
        
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            return crow::response(404, "No database sink found for this camera");
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            if (!body.contains("query")) {
                return crow::response(400, "Missing query field");
            }
            
            std::string query = body["query"];
            
            // Basic query safety check
            std::string lowerQuery = query;
            std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
            
            // Only allow SELECT queries for security
            if (lowerQuery.substr(0, 6) != "select") {
                return crow::response(400, "Only SELECT queries are allowed for explanation");
            }
            
            // Prevent potentially dangerous operations
            if (lowerQuery.find("drop") != std::string::npos ||
                lowerQuery.find("delete") != std::string::npos ||
                lowerQuery.find("insert") != std::string::npos ||
                lowerQuery.find("update") != std::string::npos ||
                lowerQuery.find("create") != std::string::npos ||
                lowerQuery.find("alter") != std::string::npos) {
                return crow::response(400, "Query contains prohibited operations");
            }
            
            // Get query explanation
            nlohmann::json explanation = dbSink->explainQuery(query);
            
            return createJsonResponse(explanation);
        } catch (const nlohmann::json::parse_error& e) {
            return crow::response(400, std::string("Invalid JSON: ") + e.what());
        } catch (const std::exception& e) {
            std::cout << "Exception in query explain: " << e.what() << std::endl;
            return crow::response(500, std::string("Query explain error: ") + e.what());
        }
    });
}

void Api::setupManagementRoutes() {
    // Get current log level
    CROW_ROUTE(app_, "/api/v1/system/log-level")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        LogLevel currentLevel = Logger::getInstance().getLogLevel();
        std::string levelStr = levelToString(currentLevel);
        
        nlohmann::json response;
        response["level"] = levelStr;
        
        LOG_INFO("API", "Log level queried: " + levelStr);
        return createJsonResponse(response);
    });
    
    // Set log level
    CROW_ROUTE(app_, "/api/v1/system/log-level")
        .methods("PUT"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            if (!body.contains("level")) {
                return crow::response(400, "Missing level parameter");
            }
            
            std::string level = body["level"];
            LogLevel newLevel;
            
            // Convert string to LogLevel
            if (level == "trace") {
                newLevel = LogLevel::TRACE;
            } else if (level == "debug") {
                newLevel = LogLevel::DEBUG;
            } else if (level == "info") {
                newLevel = LogLevel::INFO;
            } else if (level == "warn") {
                newLevel = LogLevel::WARN;
            } else if (level == "error") {
                newLevel = LogLevel::ERROR;
            } else if (level == "fatal") {
                newLevel = LogLevel::FATAL;
            } else if (level == "off") {
                newLevel = LogLevel::OFF;
            } else {
                return crow::response(400, "Invalid log level. Valid values: trace, debug, info, warn, error, fatal, off");
            }
            
            // Get current level before changing
            LogLevel oldLevel = Logger::getInstance().getLogLevel();
            
            // Set new log level
            Logger::getInstance().setLogLevel(newLevel);
            
            // Create response
            nlohmann::json response;
            response["success"] = true;
            response["previous_level"] = levelToString(oldLevel);
            response["current_level"] = level;
            
            LOG_INFO("API", "Log level changed from " + levelToString(oldLevel) + " to " + level);
            
            return createJsonResponse(response);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
}

void Api::setupBackgroundTaskRoutes() {
    // Get all tasks
    CROW_ROUTE(app_, "/api/v1/tasks")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto tasks = BackgroundTaskManager::getInstance().getAllTasks();
        
        nlohmann::json tasksArray = nlohmann::json::array();
        for (const auto& task : tasks) {
            nlohmann::json taskJson;
            taskJson["id"] = task.taskId;
            taskJson["type"] = task.taskType;
            taskJson["target_id"] = task.targetId;
            taskJson["progress"] = task.progress;
            taskJson["message"] = task.message;
            
            // Convert state enum to string
            std::string stateStr;
            switch (task.state) {
                case BackgroundTaskManager::TaskStatus::State::PENDING:
                    stateStr = "pending";
                    break;
                case BackgroundTaskManager::TaskStatus::State::RUNNING:
                    stateStr = "running";
                    break;
                case BackgroundTaskManager::TaskStatus::State::COMPLETED:
                    stateStr = "completed";
                    break;
                case BackgroundTaskManager::TaskStatus::State::FAILED:
                    stateStr = "failed";
                    break;
                default:
                    stateStr = "unknown";
            }
            taskJson["state"] = stateStr;
            
            // Convert timestamps to milliseconds since epoch
            taskJson["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                task.createdAt.time_since_epoch()).count();
                
            taskJson["updated_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                task.updatedAt.time_since_epoch()).count();
                
            tasksArray.push_back(taskJson);
        }
        
        nlohmann::json response;
        response["tasks"] = tasksArray;
        
        return createJsonResponse(response);
    });
    
    // Get a specific task
    CROW_ROUTE(app_, "/api/v1/tasks/<string>")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& taskId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto task = BackgroundTaskManager::getInstance().getTaskStatus(taskId);
        
        // If task not found, return 404
        if (task.state == BackgroundTaskManager::TaskStatus::State::FAILED && 
            task.message == "Task not found") {
            return crow::response(404, "Task not found");
        }
        
        nlohmann::json taskJson;
        taskJson["id"] = task.taskId;
        taskJson["type"] = task.taskType;
        taskJson["target_id"] = task.targetId;
        taskJson["progress"] = task.progress;
        taskJson["message"] = task.message;
        
        // Convert state enum to string
        std::string stateStr;
        switch (task.state) {
            case BackgroundTaskManager::TaskStatus::State::PENDING:
                stateStr = "pending";
                break;
            case BackgroundTaskManager::TaskStatus::State::RUNNING:
                stateStr = "running";
                break;
            case BackgroundTaskManager::TaskStatus::State::COMPLETED:
                stateStr = "completed";
                break;
            case BackgroundTaskManager::TaskStatus::State::FAILED:
                stateStr = "failed";
                break;
            default:
                stateStr = "unknown";
        }
        taskJson["state"] = stateStr;
        
        // Convert timestamps to milliseconds since epoch
        taskJson["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            task.createdAt.time_since_epoch()).count();
            
        taskJson["updated_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            task.updatedAt.time_since_epoch()).count();
        
        return createJsonResponse(taskJson);
    });
    
    // Get results for database query tasks
    CROW_ROUTE(app_, "/api/v1/tasks/<string>/result")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& taskId) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto task = BackgroundTaskManager::getInstance().getTaskStatus(taskId);
        
        // If task not found, return 404
        if (task.state == BackgroundTaskManager::TaskStatus::State::FAILED && 
            task.message == "Task not found") {
            return crow::response(404, "Task not found");
        }
        
        // Only return results for completed database query tasks
        if (task.state != BackgroundTaskManager::TaskStatus::State::COMPLETED) {
            nlohmann::json response;
            response["success"] = false;
            response["message"] = "Task not completed yet";
            response["task_state"] = task.state == BackgroundTaskManager::TaskStatus::State::RUNNING ? "running" : 
                                     task.state == BackgroundTaskManager::TaskStatus::State::PENDING ? "pending" : "failed";
            return createJsonResponse(response, 202);
        }
        
        // Re-run the appropriate database query to get the result
        // This is a simplified approach - in a real system you'd cache results
        std::string cameraId = task.targetId;
        auto camera = CameraManager::getInstance().getCamera(cameraId);
        if (!camera) {
            return crow::response(404, "Camera not found");
        }
        
        // Find the database sink for this camera
        std::shared_ptr<DatabaseSink> dbSink = nullptr;
        for (const auto& sink : camera->getSinkComponents()) {
            auto db = std::dynamic_pointer_cast<DatabaseSink>(sink);
            if (db) {
                dbSink = db;
                break;
            }
        }
        
        if (!dbSink) {
            return crow::response(404, "No database sink found for this camera");
        }
        
        try {
            nlohmann::json result;
            
            if (task.taskType == "database_analytics") {
                result = dbSink->getAnalytics(cameraId);
            } else if (task.taskType == "database_timeseries") {
                result = dbSink->getTimeSeriesData(cameraId, 0, 0);
            } else {
                result["error"] = "Unknown task type";
                result["success"] = false;
            }
            
            return createJsonResponse(result);
        } catch (const std::exception& e) {
            nlohmann::json errorResponse;
            errorResponse["success"] = false;
            errorResponse["error"] = e.what();
            return createJsonResponse(errorResponse, 500);
        }
    });
}

bool Api::checkLicense(const crow::request& req, crow::response& res) {
    // Check if camera manager is initialized AND license is valid
    if (!CameraManager::getInstance().isInitialized()) {
        res = crow::response(500, "Camera manager not initialized");
        return false;
    }
    
    // Additionally check if we have a valid license
    const auto& licenseManager = CameraManager::getInstance().getLicenseManager();
    if (!licenseManager.isValid()) {
        nlohmann::json errorResponse;
        errorResponse["valid"] = false;
        errorResponse["message"] = "No valid license found";
        errorResponse["error"] = "license_required";
        
        res = crow::response(401, errorResponse.dump(2));
        res.set_header("Content-Type", "application/json");
        return false;
    }
    
    return true;
}

void Api::start(bool threaded) {
    std::cout << "Starting API server on port " << port_ << std::endl;
    if (threaded) {
        app_.multithreaded().run();
    } else {
        app_.run();
    }
}

void Api::stop() {
    std::cout << "Stopping API server..." << std::endl;
    app_.stop();
}

bool Api::saveCameraConfigToDB(const std::string& cameraId) {
    if (!ConfigManager::getInstance().isReady()) {
        LOG_ERROR("API", "Configuration database is not initialized");
        return false;
    }
    
    auto camera = CameraManager::getInstance().getCamera(cameraId);
    if (!camera) {
        LOG_ERROR("API", "Cannot save configuration for non-existent camera: " + cameraId);
        return false;
    }
    
    try {
        // Build the configuration JSON
        nlohmann::json cameraConfig;
        cameraConfig["id"] = camera->getId();
        cameraConfig["name"] = camera->getName();
        cameraConfig["running"] = camera->isRunning();
        
        // Save source component
        auto source = camera->getSourceComponent();
        if (source) {
            // Get the component status but sanitize it to ensure type consistency
            nlohmann::json sourceJson = source->getStatus();
            sanitizeComponentJson(sourceJson);
            cameraConfig["source"] = sourceJson;
        } else {
            cameraConfig["source"] = nullptr;
        }
        
        // Save processor components
        nlohmann::json processors = nlohmann::json::array();
        for (const auto& processor : camera->getProcessorComponents()) {
            nlohmann::json processorJson = processor->getStatus();
            sanitizeComponentJson(processorJson);
            processors.push_back(processorJson);
        }
        cameraConfig["processors"] = processors;
        
        // Save sink components
        nlohmann::json sinks = nlohmann::json::array();
        for (const auto& sink : camera->getSinkComponents()) {
            nlohmann::json sinkJson = sink->getStatus();
            sanitizeComponentJson(sinkJson);
            sinks.push_back(sinkJson);
        }
        cameraConfig["sinks"] = sinks;
        
        // Save to the database
        bool success = ConfigManager::getInstance().saveCameraConfig(cameraId, cameraConfig);
        if (success) {
            LOG_INFO("API", "Saved configuration for camera: " + cameraId);
        } else {
            LOG_ERROR("API", "Failed to save configuration for camera: " + cameraId);
        }
        
        return success;
    } catch (const std::exception& e) {
        LOG_ERROR("API", "Exception while saving camera config: " + std::string(e.what()));
        return false;
    }
}

// Helper method to sanitize component JSON to ensure consistent types
void Api::sanitizeComponentJson(nlohmann::json& componentJson) {
    // Ensure id is a string
    if (componentJson.contains("id") && !componentJson["id"].is_string()) {
        componentJson["id"] = componentJson["id"].dump();
    }
    
    // Handle type fields (both numeric and string formats)
    if (componentJson.contains("type")) {
        if (!componentJson["type"].is_string()) {
            // If we have a numeric type, convert it to string format
            std::string typeStr;
            int typeVal = -1;
            
            try {
                if (componentJson["type"].is_number()) {
                    typeVal = componentJson["type"].get<int>();
                } else {
                    typeStr = componentJson["type"].dump();
                    typeVal = std::stoi(typeStr);
                }
                
                // Map component type values to strings
                if (typeVal == 0) {
                    componentJson["type"] = "source";
                } else if (typeVal == 1) {
                    componentJson["type"] = "processor";
                } else if (typeVal == 2) {
                    componentJson["type"] = "sink";
                } else {
                    componentJson["type"] = "unknown";
                }
            } catch (const std::exception& e) {
                // If conversion fails, use a default
                componentJson["type"] = "unknown";
            }
        }
    }
    
    // Clean up any legacy type_name field that may exist
    // We now store the correct type directly in the type field
    if (componentJson.contains("type_name")) {
        componentJson.erase("type_name");
    }
    
    // Ensure running is a boolean
    if (componentJson.contains("running") && !componentJson["running"].is_boolean()) {
        if (componentJson["running"].is_number()) {
            componentJson["running"] = (componentJson["running"].get<int>() != 0);
        } else {
            componentJson["running"] = false;
        }
    }
    
    // Preserve URL field for source components
    if (componentJson.contains("url") && !componentJson["url"].is_string()) {
        if (componentJson["url"].is_null()) {
            componentJson["url"] = "";
        } else {
            componentJson["url"] = componentJson["url"].dump();
        }
    }
    
    // Preserve configuration data - this is critical for component setup
    if (!componentJson.contains("config")) {
        // Create a config object from component properties if missing
        componentJson["config"] = nlohmann::json::object();
        
        // Extract likely config fields based on component type
        if (componentJson.contains("type")) {
            std::string type = componentJson["type"];
            
            if (type == "rtsp" || type == "file") {
                // For source components
                if (componentJson.contains("url")) {
                    componentJson["config"]["url"] = componentJson["url"];
                }
                if (componentJson.contains("width")) {
                    componentJson["config"]["width"] = componentJson["width"];
                }
                if (componentJson.contains("height")) {
                    componentJson["config"]["height"] = componentJson["height"];
                }
                if (componentJson.contains("fps")) {
                    componentJson["config"]["fps"] = componentJson["fps"];
                }
                if (componentJson.contains("hardware_acceleration")) {
                    bool useHwAccel = (componentJson["hardware_acceleration"] == "enabled");
                    componentJson["config"]["use_hw_accel"] = useHwAccel;
                }
            } else if (type == "object_detection") {
                // For object detection processor components
                if (componentJson.contains("confidence_threshold")) {
                    componentJson["config"]["confidence_threshold"] = componentJson["confidence_threshold"];
                }
                if (componentJson.contains("model_id")) {
                    componentJson["config"]["model_id"] = componentJson["model_id"];
                }
                if (componentJson.contains("classes") && componentJson["classes"].is_array()) {
                    componentJson["config"]["classes"] = componentJson["classes"];
                }
                if (componentJson.contains("draw_bounding_boxes")) {
                    componentJson["config"]["draw_bounding_boxes"] = componentJson["draw_bounding_boxes"];
                }
                if (componentJson.contains("label_font_scale")) {
                    componentJson["config"]["label_font_scale"] = componentJson["label_font_scale"];
                }
                if (componentJson.contains("protocol")) {
                    componentJson["config"]["protocol"] = componentJson["protocol"];
                }
                if (componentJson.contains("use_shared_memory")) {
                    componentJson["config"]["use_shared_memory"] = componentJson["use_shared_memory"];
                }
                if (componentJson.contains("verbose_logging")) {
                    componentJson["config"]["verbose_logging"] = componentJson["verbose_logging"];
                }
                // Do not copy server_url from component - always use GlobalConfig
            } else if (type == "object_classification") {
                // For object classification processor components
                if (componentJson.contains("model_id")) {
                    componentJson["config"]["model_id"] = componentJson["model_id"];
                }
                if (componentJson.contains("model_type")) {
                    componentJson["config"]["model_type"] = componentJson["model_type"];
                }
                if (componentJson.contains("confidence_threshold")) {
                    componentJson["config"]["confidence_threshold"] = componentJson["confidence_threshold"];
                }
                if (componentJson.contains("draw_classification")) {
                    componentJson["config"]["draw_classification"] = componentJson["draw_classification"];
                }
                if (componentJson.contains("use_shared_memory")) {
                    componentJson["config"]["use_shared_memory"] = componentJson["use_shared_memory"];
                }
                if (componentJson.contains("text_font_scale")) {
                    componentJson["config"]["text_font_scale"] = componentJson["text_font_scale"];
                }
                if (componentJson.contains("classes") && componentJson["classes"].is_array()) {
                    componentJson["config"]["classes"] = componentJson["classes"];
                }
                // Do not copy server_url from component - always use GlobalConfig
            } else if (type == "age_gender_detection") {
                // For age gender detection processor components
                if (componentJson.contains("model_id")) {
                    componentJson["config"]["model_id"] = componentJson["model_id"];
                }
                if (componentJson.contains("confidence_threshold")) {
                    componentJson["config"]["confidence_threshold"] = componentJson["confidence_threshold"];
                }
                if (componentJson.contains("draw_detections")) {
                    componentJson["config"]["draw_detections"] = componentJson["draw_detections"];
                }
                if (componentJson.contains("use_shared_memory")) {
                    componentJson["config"]["use_shared_memory"] = componentJson["use_shared_memory"];
                }
                if (componentJson.contains("text_font_scale")) {
                    componentJson["config"]["text_font_scale"] = componentJson["text_font_scale"];
                }
                // Do not copy server_url from component - always use GlobalConfig
            } else if (type == "object_tracking") {
                // For object tracking component
                if (componentJson.contains("track_thresh")) {
                    componentJson["config"]["track_thresh"] = componentJson["track_thresh"];
                }
                if (componentJson.contains("high_thresh")) {
                    componentJson["config"]["high_thresh"] = componentJson["high_thresh"];
                }
                if (componentJson.contains("match_thresh")) {
                    componentJson["config"]["match_thresh"] = componentJson["match_thresh"];
                }
                if (componentJson.contains("frame_rate")) {
                    componentJson["config"]["frame_rate"] = componentJson["frame_rate"];
                }
                if (componentJson.contains("track_buffer")) {
                    componentJson["config"]["track_buffer"] = componentJson["track_buffer"];
                }
                if (componentJson.contains("draw_tracking")) {
                    componentJson["config"]["draw_tracking"] = componentJson["draw_tracking"];
                }
                if (componentJson.contains("draw_track_trajectory")) {
                    componentJson["config"]["draw_track_trajectory"] = componentJson["draw_track_trajectory"];
                }
                if (componentJson.contains("draw_track_id")) {
                    componentJson["config"]["draw_track_id"] = componentJson["draw_track_id"];
                }
                if (componentJson.contains("draw_semi_transparent_boxes")) {
                    componentJson["config"]["draw_semi_transparent_boxes"] = componentJson["draw_semi_transparent_boxes"];
                }
                if (componentJson.contains("label_font_scale")) {
                    componentJson["config"]["label_font_scale"] = componentJson["label_font_scale"];
                }
            } else if (type == "line_zone_manager") {
                // For line zone manager component
                if (componentJson.contains("draw_zones")) {
                    componentJson["config"]["draw_zones"] = componentJson["draw_zones"];
                }
                if (componentJson.contains("line_color") && componentJson["line_color"].is_array()) {
                    componentJson["config"]["line_color"] = componentJson["line_color"];
                }
                if (componentJson.contains("line_thickness")) {
                    componentJson["config"]["line_thickness"] = componentJson["line_thickness"];
                }
                if (componentJson.contains("draw_counts")) {
                    componentJson["config"]["draw_counts"] = componentJson["draw_counts"];
                }
                if (componentJson.contains("text_color") && componentJson["text_color"].is_array()) {
                    componentJson["config"]["text_color"] = componentJson["text_color"];
                }
                if (componentJson.contains("text_scale")) {
                    componentJson["config"]["text_scale"] = componentJson["text_scale"];
                }
                if (componentJson.contains("text_thickness")) {
                    componentJson["config"]["text_thickness"] = componentJson["text_thickness"];
                }
                if (componentJson.contains("zones") && componentJson["zones"].is_array()) {
                    componentJson["config"]["zones"] = componentJson["zones"];
                }
            } else if (type == "polygon_zone_manager") {
                // For polygon zone manager component
                if (componentJson.contains("draw_zones")) {
                    componentJson["config"]["draw_zones"] = componentJson["draw_zones"];
                }
                if (componentJson.contains("line_color") && componentJson["line_color"].is_array()) {
                    componentJson["config"]["line_color"] = componentJson["line_color"];
                }
                if (componentJson.contains("line_thickness")) {
                    componentJson["config"]["line_thickness"] = componentJson["line_thickness"];
                }
                if (componentJson.contains("draw_counts")) {
                    componentJson["config"]["draw_counts"] = componentJson["draw_counts"];
                }
                if (componentJson.contains("text_color") && componentJson["text_color"].is_array()) {
                    componentJson["config"]["text_color"] = componentJson["text_color"];
                }
                if (componentJson.contains("text_scale")) {
                    componentJson["config"]["text_scale"] = componentJson["text_scale"];
                }
                if (componentJson.contains("text_thickness")) {
                    componentJson["config"]["text_thickness"] = componentJson["text_thickness"];
                }
                if (componentJson.contains("zones") && componentJson["zones"].is_array()) {
                    componentJson["config"]["zones"] = componentJson["zones"];
                }
                if (componentJson.contains("fill_color") && componentJson["fill_color"].is_array()) {
                    componentJson["config"]["fill_color"] = componentJson["fill_color"];
                }
                if (componentJson.contains("fill_opacity")) {
                    componentJson["config"]["fill_opacity"] = componentJson["fill_opacity"];
                }
            } else if (type == "database") {
                // For database sink
                if (componentJson.contains("store_thumbnails")) {
                    componentJson["config"]["store_thumbnails"] = componentJson["store_thumbnails"];
                }
                if (componentJson.contains("thumbnail_width")) {
                    componentJson["config"]["thumbnail_width"] = componentJson["thumbnail_width"];
                }
                if (componentJson.contains("thumbnail_height")) {
                    componentJson["config"]["thumbnail_height"] = componentJson["thumbnail_height"];
                }
            }
        }
    } else if (componentJson["config"].is_object()) {
        // If config exists and is an object, sanitize it
        sanitizeConfigJson(componentJson["config"]);
    } else if (!componentJson["config"].is_object()) {
        // Ensure config is an object
        componentJson["config"] = nlohmann::json::object();
    }
}

// Helper method to sanitize configuration JSON objects
void Api::sanitizeConfigJson(nlohmann::json& configJson) {
    for (auto it = configJson.begin(); it != configJson.end(); ++it) {
        // Recursively process nested objects
        if (it.value().is_object()) {
            sanitizeConfigJson(it.value());
        }
        // Process arrays - sanitize each object in the array
        else if (it.value().is_array()) {
            for (auto& item : it.value()) {
                if (item.is_object()) {
                    sanitizeConfigJson(item);
                }
            }
        }
    }
}

bool Api::loadCameraConfigFromDB(const std::string& cameraId) {
    if (!ConfigManager::getInstance().isReady()) {
        LOG_ERROR("API", "Configuration database is not initialized");
        return false;
    }
    
    try {
        // Get the camera configuration
        auto config = ConfigManager::getInstance().getCameraConfig(cameraId);
        if (config.empty()) {
            LOG_WARN("API", "No configuration found for camera: " + cameraId);
            return false;
        }
        
        // Check if the camera exists, if not create it
        std::shared_ptr<Camera> camera;
        if (CameraManager::getInstance().cameraExists(cameraId)) {
            camera = CameraManager::getInstance().getCamera(cameraId);
        } else {
            std::string name = config.contains("name") ? 
                              config["name"].get<std::string>() : cameraId;
            camera = CameraManager::getInstance().createCamera(cameraId, name);
        }
        
        if (!camera) {
            LOG_ERROR("API", "Failed to create or get camera with ID: " + cameraId);
            return false;
        }
        
        // Set camera properties
        if (config.contains("name")) {
            camera->setName(config["name"]);
        }
        
        // Load source component if present
        if (config.contains("source") && !config["source"].is_null()) {
            auto sourceConfig = config["source"];
            std::string type = sourceConfig["type"];
            std::string id = sourceConfig["id"];
            
            // Remove any existing source component
            camera->setSourceComponent(nullptr);
            
            auto source = ComponentFactory::getInstance().createSourceComponent(
                id, camera.get(), type, sourceConfig["config"]);
                
            if (source) {
                camera->setSourceComponent(source);
            }
        }
        
        // Load processor components
        if (config.contains("processors") && config["processors"].is_array()) {
            // Clear existing processors
            for (const auto& proc : camera->getProcessorComponents()) {
                camera->removeProcessorComponent(proc->getId());
            }
            
            // Add new processors from config
            for (const auto& procConfig : config["processors"]) {
                std::string type = procConfig["type"];
                std::string id = procConfig["id"];
                
                auto processor = ComponentFactory::getInstance().createProcessorComponent(
                    id, camera.get(), type, procConfig["config"]);
                    
                if (processor) {
                    camera->addProcessorComponent(processor);
                }
            }
        }
        
        // Load sink components
        if (config.contains("sinks") && config["sinks"].is_array()) {
            // Clear existing sinks
            for (const auto& sink : camera->getSinkComponents()) {
                camera->removeSinkComponent(sink->getId());
            }
            
            // Add new sinks from config
            for (const auto& sinkConfig : config["sinks"]) {
                std::string type = sinkConfig["type"];
                std::string id = sinkConfig["id"];
                
                auto sink = ComponentFactory::getInstance().createSinkComponent(
                    id, camera.get(), type, sinkConfig["config"]);
                    
                if (sink) {
                    camera->addSinkComponent(sink);
                }
            }
        }
        
        // Start camera if it was running
        if (config.contains("running") && config["running"].get<bool>()) {
            bool hasAiComponents = false;
            
            // Check if the camera has any components that require AI server
            for (const auto& processor : camera->getProcessorComponents()) {
                auto status = processor->getStatus();
                if (status["type"] == "object_detection" || 
                    status["type"] == "object_classification" || 
                    status["type"] == "age_gender_detection") {
                    hasAiComponents = true;
                    break;
                }
            }
            
            // If the camera has AI components, initialize them to check server availability
            if (hasAiComponents) {
                bool aiServerAvailable = true;
                
                for (const auto& processor : camera->getProcessorComponents()) {
                    if (!processor->initialize()) {
                        auto status = processor->getStatus();
                        if (status.contains("last_error") && !status["last_error"].is_null()) {
                            std::string lastError = status["last_error"].get<std::string>();
                            if (lastError.find("server is not available") != std::string::npos || 
                                lastError.find("connect to server") != std::string::npos) {
                                aiServerAvailable = false;
                                LOG_WARN("API", "Camera " + cameraId + " requires AI server but it's unavailable. Camera will not be started automatically.");
                                break;
                            }
                        }
                    }
                }
                
                if (aiServerAvailable) {
                    camera->start();
                }
            } else {
                // No AI components, safe to start
                camera->start();
            }
        }
        
        LOG_INFO("API", "Successfully loaded configuration for camera: " + cameraId);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("API", "Exception while loading camera config: " + std::string(e.what()));
        return false;
    }
}

void Api::setupConfigRoutes() {
    std::cout << "Setting up configuration routes..." << std::endl;
    LOG_INFO("API", "Setting up configuration routes");
    
    // Get all configuration
    CROW_ROUTE(app_, "/api/v1/config")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        std::cout << "Handling GET request for /api/v1/config" << std::endl;
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        // Get all configuration
        auto config = ConfigManager::getInstance().getAllConfig();
        
        return createJsonResponse(config);
    });
    
    // Set a configuration value
    CROW_ROUTE(app_, "/api/v1/config/<string>")
        .methods("PUT"_method)
    ([this](const crow::request& req, const std::string& key) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        try {
            // Parse the request
            auto body = nlohmann::json::parse(req.body);
            
            // Set the configuration
            if (!ConfigManager::getInstance().setConfig(key, body)) {
                return crow::response(500, "Failed to set configuration");
            }
            
            // Update GlobalConfig if the key is relevant to global configuration
            if (key == "ai_server_url" && body.is_string()) {
                std::string newUrl = body.get<std::string>();
                GlobalConfig::getInstance().setAiServerUrl(newUrl);
                LOG_INFO("API", "Updated GlobalConfig with new AI server URL: " + newUrl);
            }
            else if (key == "use_shared_memory" && body.is_boolean()) {
                bool useSharedMem = body.get<bool>();
                GlobalConfig::getInstance().setUseSharedMemory(useSharedMem);
                LOG_INFO("API", "Updated GlobalConfig with new shared memory setting: " + 
                        std::string(useSharedMem ? "true" : "false"));
            }
            else if (key == "port" && body.is_number_integer()) {
                int newPort = body.get<int>();
                GlobalConfig::getInstance().setPort(newPort);
                LOG_INFO("API", "Updated GlobalConfig with new port: " + std::to_string(newPort));
            }
            
            // Return the updated configuration
            auto config = ConfigManager::getInstance().getConfig(key);
            
            return createJsonResponse(config);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Get a configuration value
    CROW_ROUTE(app_, "/api/v1/config/<string>")
        .methods("GET"_method)
    ([this](const crow::request& req, const std::string& key) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        // Check if this is a special route for export
        if (key == "export") {
            nlohmann::json exportData;
            
            // Get all general configuration
            exportData["config"] = ConfigManager::getInstance().getAllConfig();
            
            // Get all camera configurations
            exportData["cameras"] = ConfigManager::getInstance().getAllCameraConfigs();
            
            return createJsonResponse(exportData);
        }
        
        // Get the configuration
        auto config = ConfigManager::getInstance().getConfig(key);
        
        if (config.empty()) {
            return crow::response(404, "Configuration key not found");
        }
        
        return createJsonResponse(config);
    });
    
    // Delete a configuration value
    CROW_ROUTE(app_, "/api/v1/config/<string>")
        .methods("DELETE"_method)
    ([this](const crow::request& req, const std::string& key) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        // Delete the configuration
        if (!ConfigManager::getInstance().deleteConfig(key)) {
            return crow::response(404, "Configuration key not found");
        }
        
        nlohmann::json response;
        response["success"] = true;
        response["message"] = "Configuration deleted";
        
        return createJsonResponse(response);
    });
    
    // Import configuration (handle in the POST method for /api/v1/config)
    CROW_ROUTE(app_, "/api/v1/config")
        .methods("POST"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        // Check if the URL contains "import" in the query parameters
        bool isImport = false;
        auto queryParams = req.url_params;
        if (queryParams.get("action") && std::string(queryParams.get("action")) == "import") {
            isImport = true;
        }
        
        if (isImport) {
            try {
                // Parse the request
                auto body = nlohmann::json::parse(req.body);
                
                // Import general configuration
                if (body.contains("config") && body["config"].is_object()) {
                    for (auto it = body["config"].begin(); it != body["config"].end(); ++it) {
                        ConfigManager::getInstance().setConfig(it.key(), it.value());
                    }
                }
                
                // Import camera configurations
                if (body.contains("cameras") && body["cameras"].is_object()) {
                    for (auto it = body["cameras"].begin(); it != body["cameras"].end(); ++it) {
                        ConfigManager::getInstance().saveCameraConfig(it.key(), it.value());
                    }
                }
                
                // Reload the configurations
                loadSavedConfig();
                
                nlohmann::json response;
                response["success"] = true;
                response["message"] = "Configuration imported successfully";
                
                return createJsonResponse(response);
            } catch (const std::exception& e) {
                return crow::response(400, std::string("Invalid request: ") + e.what());
            }
        } else {
            // Regular POST to /api/v1/config - create a new config entry
            try {
                auto body = nlohmann::json::parse(req.body);
                
                if (!body.contains("key") || !body.contains("value")) {
                    return crow::response(400, "Missing key or value field");
                }
                
                std::string key = body["key"];
                auto value = body["value"];
                
                if (ConfigManager::getInstance().setConfig(key, value)) {
                    nlohmann::json response;
                    response["success"] = true;
                    response["message"] = "Configuration created successfully";
                    response["key"] = key;
                    return createJsonResponse(response, 201);
                } else {
                    return crow::response(500, "Failed to create configuration");
                }
            } catch (const std::exception& e) {
                return crow::response(400, std::string("Invalid request: ") + e.what());
            }
        }
    });
    
    // Additional route specifically for export
    CROW_ROUTE(app_, "/api/v1/config/export")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        nlohmann::json exportData;
        
        // Get all general configuration
        exportData["config"] = ConfigManager::getInstance().getAllConfig();
        
        // Get all camera configurations
        exportData["cameras"] = ConfigManager::getInstance().getAllCameraConfigs();
        
        return createJsonResponse(exportData);
    });
    
    // Additional route specifically for import
    CROW_ROUTE(app_, "/api/v1/config/import")
        .methods("POST"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        try {
            // Parse the request
            auto body = nlohmann::json::parse(req.body);
            
            // Import general configuration
            if (body.contains("config") && body["config"].is_object()) {
                for (auto it = body["config"].begin(); it != body["config"].end(); ++it) {
                    ConfigManager::getInstance().setConfig(it.key(), it.value());
                }
            }
            
            // Import camera configurations
            if (body.contains("cameras") && body["cameras"].is_object()) {
                for (auto it = body["cameras"].begin(); it != body["cameras"].end(); ++it) {
                    ConfigManager::getInstance().saveCameraConfig(it.key(), it.value());
                }
            }
            
            // Reload the configurations
            loadSavedConfig();
            
            nlohmann::json response;
            response["success"] = true;
            response["message"] = "Configuration imported successfully";
            
            return createJsonResponse(response);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
}

int Api::enforceLicenseRestrictions() {
    LOG_INFO("API", "Enforcing license restrictions on cameras");
    
    int stoppedCamerasCount = 0;
    auto cameras = CameraManager::getInstance().getAllCameras();
    LicenseTier currentTier = CameraManager::getInstance().getLicenseManager().getLicenseTier();
    auto& permHelper = ComponentPermissionHelper::getInstance();
    
    for (const auto& camera : cameras) {
        bool hasViolation = false;
        std::vector<std::string> violatingComponents;
        
        // Check source component
        auto source = camera->getSourceComponent();
        if (source) {
            // Get the component's status which includes its string-based type
            auto status = source->getStatus();
            if (status.contains("type") && status["type"].is_string()) {
                std::string sourceType = status["type"];
                
                if (!permHelper.isComponentAllowed(ComponentCategory::SOURCE, sourceType, currentTier)) {
                    hasViolation = true;
                    violatingComponents.push_back("source:" + sourceType);
                }
            }
        }
        
        // Check processor components
        for (const auto& processor : camera->getProcessorComponents()) {
            // Get the component's status which includes its string-based type
            auto status = processor->getStatus();
            if (status.contains("type") && status["type"].is_string()) {
                std::string processorType = status["type"];
                
                if (!permHelper.isComponentAllowed(ComponentCategory::PROCESSOR, processorType, currentTier)) {
                    hasViolation = true;
                    violatingComponents.push_back("processor:" + processorType);
                }
            }
        }
        
        // Check sink components
        for (const auto& sink : camera->getSinkComponents()) {
            // Get the component's status which includes its string-based type
            auto status = sink->getStatus();
            if (status.contains("type") && status["type"].is_string()) {
                std::string sinkType = status["type"];
                
                if (!permHelper.isComponentAllowed(ComponentCategory::SINK, sinkType, currentTier)) {
                    hasViolation = true;
                    violatingComponents.push_back("sink:" + sinkType);
                }
            }
        }
        
        // If violations were found and camera is running, stop it
        if (hasViolation && camera->isRunning()) {
            LOG_WARN("API", "Stopping camera '" + camera->getId() + "' due to license restrictions");
            LOG_INFO("API", "Camera '" + camera->getId() + "' using restricted components: " + 
                     [&violatingComponents]() {
                         std::string result;
                         for (size_t i = 0; i < violatingComponents.size(); ++i) {
                             result += violatingComponents[i];
                             if (i < violatingComponents.size() - 1) {
                                 result += ", ";
                             }
                         }
                         return result;
                     }());
            
            camera->stop();
            stoppedCamerasCount++;
            
            // Update camera configuration in database to reflect stopped state
            saveCameraConfigToDB(camera->getId());
        }
    }
    
    if (stoppedCamerasCount > 0) {
        LOG_WARN("API", "Stopped " + std::to_string(stoppedCamerasCount) + 
                 " camera(s) due to license restrictions");
    } else {
        LOG_INFO("API", "All cameras comply with current license restrictions");
    }
    
    return stoppedCamerasCount;
}

void Api::setupApiLoggingRoutes() {
    std::cout << "Setting up API logging management routes..." << std::endl;
    LOG_INFO("API", "Setting up API logging management routes");
    
    // Get current API logging configuration
    CROW_ROUTE(app_, "/api/v1/system/api-logging")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        // Reload configuration to get latest values
        g_apiLoggingConfig.loadFromConfig();
        
        nlohmann::json response;
        response["enabled"] = g_apiLoggingConfig.enabled;
        response["log_request_body"] = g_apiLoggingConfig.log_request_body;
        response["log_response_body"] = g_apiLoggingConfig.log_response_body;
        response["slow_request_threshold_ms"] = g_apiLoggingConfig.slow_request_threshold_ms;
        response["timeout_threshold_ms"] = g_apiLoggingConfig.timeout_threshold_ms;
        response["log_only_slow_requests"] = g_apiLoggingConfig.log_only_slow_requests;
        response["include_request_headers"] = g_apiLoggingConfig.include_request_headers;
        response["include_response_headers"] = g_apiLoggingConfig.include_response_headers;
        
        LOG_INFO("API", "API logging configuration queried");
        return createJsonResponse(response);
    });
    
    // Update API logging configuration
    CROW_ROUTE(app_, "/api/v1/system/api-logging")
        .methods("PUT"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            
            // Update configuration in database
            if (body.contains("enabled")) {
                ConfigManager::getInstance().setConfig("api_logging_enabled", body["enabled"]);
            }
            if (body.contains("log_request_body")) {
                ConfigManager::getInstance().setConfig("api_logging_log_request_body", body["log_request_body"]);
            }
            if (body.contains("log_response_body")) {
                ConfigManager::getInstance().setConfig("api_logging_log_response_body", body["log_response_body"]);
            }
            if (body.contains("slow_request_threshold_ms")) {
                ConfigManager::getInstance().setConfig("api_logging_slow_threshold_ms", body["slow_request_threshold_ms"]);
            }
            if (body.contains("timeout_threshold_ms")) {
                ConfigManager::getInstance().setConfig("api_logging_timeout_threshold_ms", body["timeout_threshold_ms"]);
            }
            if (body.contains("log_only_slow_requests")) {
                ConfigManager::getInstance().setConfig("api_logging_log_only_slow", body["log_only_slow_requests"]);
            }
            if (body.contains("include_request_headers")) {
                ConfigManager::getInstance().setConfig("api_logging_include_request_headers", body["include_request_headers"]);
            }
            if (body.contains("include_response_headers")) {
                ConfigManager::getInstance().setConfig("api_logging_include_response_headers", body["include_response_headers"]);
            }
            
            // Reload configuration to apply changes immediately
            g_apiLoggingConfig.loadFromConfig();
            
            // Create response with current configuration
            nlohmann::json response;
            response["success"] = true;
            response["message"] = "API logging configuration updated";
            response["current_config"] = {
                {"enabled", g_apiLoggingConfig.enabled},
                {"log_request_body", g_apiLoggingConfig.log_request_body},
                {"log_response_body", g_apiLoggingConfig.log_response_body},
                {"slow_request_threshold_ms", g_apiLoggingConfig.slow_request_threshold_ms},
                {"timeout_threshold_ms", g_apiLoggingConfig.timeout_threshold_ms},
                {"log_only_slow_requests", g_apiLoggingConfig.log_only_slow_requests},
                {"include_request_headers", g_apiLoggingConfig.include_request_headers},
                {"include_response_headers", g_apiLoggingConfig.include_response_headers}
            };
            
            LOG_INFO("API", "API logging configuration updated - enabled: " + 
                     std::string(g_apiLoggingConfig.enabled ? "true" : "false"));
            
            return createJsonResponse(response);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
    
    // Quick enable/disable API logging
    CROW_ROUTE(app_, "/api/v1/system/api-logging/toggle")
        .methods("POST"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        // Toggle the enabled state
        g_apiLoggingConfig.loadFromConfig();
        bool newState = !g_apiLoggingConfig.enabled;
        
        ConfigManager::getInstance().setConfig("api_logging_enabled", newState);
        g_apiLoggingConfig.loadFromConfig();
        
        nlohmann::json response;
        response["success"] = true;
        response["enabled"] = newState;
        response["message"] = std::string("API logging ") + (newState ? "enabled" : "disabled");
        
        LOG_INFO("API", "API logging toggled - now " + std::string(newState ? "enabled" : "disabled"));
        
        return createJsonResponse(response);
    });
    
    // Reset API logging configuration to defaults
    CROW_ROUTE(app_, "/api/v1/system/api-logging/reset")
        .methods("POST"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        // Set default values
        ConfigManager::getInstance().setConfig("api_logging_enabled", false);
        ConfigManager::getInstance().setConfig("api_logging_log_request_body", false);
        ConfigManager::getInstance().setConfig("api_logging_log_response_body", false);
        ConfigManager::getInstance().setConfig("api_logging_slow_threshold_ms", 1000);
        ConfigManager::getInstance().setConfig("api_logging_timeout_threshold_ms", 30000);
        ConfigManager::getInstance().setConfig("api_logging_log_only_slow", false);
        ConfigManager::getInstance().setConfig("api_logging_include_request_headers", false);
        ConfigManager::getInstance().setConfig("api_logging_include_response_headers", false);
        
        // Reload configuration
        g_apiLoggingConfig.loadFromConfig();
        
        nlohmann::json response;
        response["success"] = true;
        response["message"] = "API logging configuration reset to defaults";
        response["current_config"] = {
            {"enabled", g_apiLoggingConfig.enabled},
            {"log_request_body", g_apiLoggingConfig.log_request_body},
            {"log_response_body", g_apiLoggingConfig.log_response_body},
            {"slow_request_threshold_ms", g_apiLoggingConfig.slow_request_threshold_ms},
            {"timeout_threshold_ms", g_apiLoggingConfig.timeout_threshold_ms},
            {"log_only_slow_requests", g_apiLoggingConfig.log_only_slow_requests},
            {"include_request_headers", g_apiLoggingConfig.include_request_headers},
            {"include_response_headers", g_apiLoggingConfig.include_response_headers}
        };
        
        LOG_INFO("API", "API logging configuration reset to defaults");
        
        return createJsonResponse(response);
    });
    
    // Get API performance statistics (if we want to add this later)
    CROW_ROUTE(app_, "/api/v1/system/api-logging/stats")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        nlohmann::json response;
        response["message"] = "API performance statistics not yet implemented";
        response["note"] = "Check your logs for [API-PERFORMANCE] entries to see slow/timeout requests";
        response["config"] = {
            {"enabled", g_apiLoggingConfig.enabled},
            {"slow_request_threshold_ms", g_apiLoggingConfig.slow_request_threshold_ms},
            {"timeout_threshold_ms", g_apiLoggingConfig.timeout_threshold_ms}
        };
        
        return createJsonResponse(response);
    });
    
    // Request timeout routes removed - functionality moved to API logging monitoring
    
    // Server concurrency configuration routes
    CROW_ROUTE(app_, "/api/v1/system/concurrency")
        .methods("GET"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        auto& configManager = ConfigManager::getInstance();
        
        // Get current settings
        int current_threads = 16; // default
        int current_max_connections = 1000; // default
        
        if (configManager.isReady()) {
            auto threadConfig = configManager.getConfig("api_worker_threads");
            if (!threadConfig.empty() && threadConfig.is_number_integer()) {
                current_threads = threadConfig.get<int>();
            }
            
            auto connConfig = configManager.getConfig("api_max_connections");
            if (!connConfig.empty() && connConfig.is_number_integer()) {
                current_max_connections = connConfig.get<int>();
            }
        }
        
        nlohmann::json response;
        response["worker_threads"] = current_threads;
        response["max_connections"] = current_max_connections;
        response["min_worker_threads"] = 4;
        response["max_worker_threads"] = 64;
        response["min_connections"] = 100;
        response["max_connections_limit"] = 10000;
        response["note"] = "Changes require server restart to take full effect";
        
        return createJsonResponse(response);
    });
    
    CROW_ROUTE(app_, "/api/v1/system/concurrency")
        .methods("PUT"_method)
    ([this](const crow::request& req) {
        crow::response res;
        if (!checkLicense(req, res)) {
            return res;
        }
        
        try {
            auto body = nlohmann::json::parse(req.body);
            bool updated = false;
            
            if (body.contains("worker_threads") && body["worker_threads"].is_number_integer()) {
                int threads = body["worker_threads"].get<int>();
                if (threads < 4 || threads > 64) {
                    return crow::response(400, "Worker threads must be between 4 and 64");
                }
                ConfigManager::getInstance().setConfig("api_worker_threads", threads);
                updated = true;
            }
            
            if (body.contains("max_connections") && body["max_connections"].is_number_integer()) {
                int connections = body["max_connections"].get<int>();
                if (connections < 100 || connections > 10000) {
                    return crow::response(400, "Max connections must be between 100 and 10000");
                }
                ConfigManager::getInstance().setConfig("api_max_connections", connections);
                updated = true;
            }
            
            if (!updated) {
                return crow::response(400, "No valid configuration provided");
            }
            
            nlohmann::json response;
            response["success"] = true;
            response["message"] = "Concurrency configuration updated";
            response["note"] = "Restart the server for changes to take full effect";
            
            // Get updated values for response
            if (body.contains("worker_threads")) {
                response["worker_threads"] = body["worker_threads"];
            }
            if (body.contains("max_connections")) {
                response["max_connections"] = body["max_connections"];
            }
            
            LOG_INFO("API", "Concurrency configuration updated - restart required for full effect");
            
            return createJsonResponse(response);
        } catch (const std::exception& e) {
            return crow::response(400, std::string("Invalid request: ") + e.what());
        }
    });
}

} // namespace tapi 
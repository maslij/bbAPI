#include <iostream>
#include <memory>
#include <csignal>
#include <boost/program_options.hpp>
#include "api.h"
#include "logger.h"
#include "config_manager.h"
#include "global_config.h"

namespace po = boost::program_options;
using namespace tapi;

// Global API object for signal handling
std::unique_ptr<Api> apiServer;

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    std::cout << "\n===================================================" << std::endl;
    std::cout << "Received signal " << signal << " (" << 
        (signal == SIGINT ? "SIGINT/Ctrl+C" : 
         signal == SIGTERM ? "SIGTERM" : "Unknown") << 
        "), shutting down..." << std::endl;
    
    // Set a flag to indicate we're shutting down
    static bool shutdownInProgress = false;
    
    if (shutdownInProgress) {
        std::cout << "Shutdown already in progress. Press Ctrl+C again to force exit." << std::endl;
        // If pressed again, use the default handler which will terminate the process
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
        return;
    }
    
    shutdownInProgress = true;
    
    if (apiServer) {
        Logger::getInstance().info("Main", "Stopping API server gracefully...");
        apiServer->stop();
        Logger::getInstance().info("Main", "API server stopped.");
    } else {
        Logger::getInstance().warn("Main", "API server not initialized, nothing to stop.");
    }
    
    Logger::getInstance().info("Main", "Shutdown complete.");
    std::cout << "===================================================" << std::endl;
}

// Helper function to convert string to LogLevel
LogLevel stringToLogLevel(const std::string& level) {
    if (level == "trace") return LogLevel::TRACE;
    if (level == "debug") return LogLevel::DEBUG;
    if (level == "info") return LogLevel::INFO;
    if (level == "warn") return LogLevel::WARN;
    if (level == "error") return LogLevel::ERROR;
    if (level == "fatal") return LogLevel::FATAL;
    if (level == "off") return LogLevel::OFF;
    return LogLevel::INFO; // Default
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Parse command line options
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "Show help message")
        ("port,p", po::value<int>()->default_value(8080), "Port to listen on")
        ("license-key,l", po::value<std::string>()->default_value("demo-license-key"), "License key for the edge device")
        ("threads,t", po::value<int>()->default_value(4), "Number of worker threads")
        ("log-level", po::value<std::string>()->default_value("info"), "Log level (trace, debug, info, warn, error, fatal, off)")
        ("log-file", po::value<std::string>(), "Log file path")
        ("ai-server-url", po::value<std::string>()->default_value("http://localhost:8000"), "URL for the AI server")
        ("use-shared-memory", po::value<bool>()->default_value(false), "Use shared memory for communicating with Triton server (requires shared memory configuration)");
    
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
        
        if (vm.count("help")) {
            std::cout << "tAPI - Computer Vision Pipeline API" << std::endl;
            std::cout << desc << std::endl;
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cout << desc << std::endl;
        return 1;
    }
    
    // Get options
    int port = vm["port"].as<int>();
    std::string licenseKey = vm["license-key"].as<std::string>();
    std::string aiServerUrl = vm["ai-server-url"].as<std::string>();
    bool useSharedMemory = vm["use-shared-memory"].as<bool>();
    
    // Configure logger
    LogLevel logLevel = stringToLogLevel(vm["log-level"].as<std::string>());
    Logger::getInstance().setLogLevel(logLevel);
    
    if (vm.count("log-file")) {
        std::string logFilePath = vm["log-file"].as<std::string>();
        if (!Logger::getInstance().setOutputFile(logFilePath)) {
            std::cerr << "Failed to open log file: " << logFilePath << std::endl;
            return 1;
        }
        LOG_INFO("Main", "Logging to file: " + logFilePath);
    }
    
    LOG_INFO("Main", "Starting tAPI on port " + std::to_string(port));
    LOG_INFO("Main", "Log level set to: " + vm["log-level"].as<std::string>());
    
    try {
        // Initialize global configuration
        if (!GlobalConfig::getInstance().initialize(aiServerUrl, useSharedMemory, port)) {
            LOG_ERROR("Main", "Failed to initialize global configuration");
            return 1;
        }
        
        // Create API server with port from GlobalConfig
        apiServer = std::make_unique<Api>(GlobalConfig::getInstance().getPort());
        
        // Initialize with license key - will run with or without a valid license
        if (!apiServer->initialize(licenseKey)) {
            LOG_WARN("Main", "Failed to initialize API with provided license key. Running in unlicensed mode.");
            LOG_INFO("Main", "You can set a valid license key using the /api/v1/license endpoint.");
            // Continue execution anyway instead of returning with error
        } else {
            LOG_INFO("Main", "API initialized successfully with license key");
        }
        
        // Load saved configurations from the database
        if (apiServer->loadSavedConfig()) {
            LOG_INFO("Main", "Successfully loaded saved configurations from database");
        } else {
            LOG_WARN("Main", "Failed to load saved configurations, starting with empty state");
        }
        
        LOG_INFO("Main", "Vision pipeline system initialized and ready");

        // Start the server in multithreaded mode for concurrent request handling
        apiServer->start(true);
        
    } catch (const std::exception& e) {
        LOG_FATAL("Main", std::string("Fatal error: ") + e.what());
        return 1;
    }
    
    LOG_INFO("Main", "tAPI shut down successfully");
    return 0;
} 
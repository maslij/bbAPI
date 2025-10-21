#include "logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace tapi {

Logger::Logger() 
    : currentLevel_(LogLevel::INFO),
      consoleLogging_(true) {
}

Logger::~Logger() {
    closeLogFile();
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLogLevel(LogLevel level) {
    currentLevel_.store(level, std::memory_order_relaxed);
}

LogLevel Logger::getLogLevel() const {
    return currentLevel_.load(std::memory_order_relaxed);
}

bool Logger::setOutputFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(logMutex_);
    
    // Close any existing log file
    if (logFile_.is_open()) {
        logFile_.close();
    }
    
    // Create directory if it doesn't exist
    std::filesystem::path filePath(filename);
    std::filesystem::path directory = filePath.parent_path();
    
    if (!directory.empty()) {
        std::filesystem::create_directories(directory);
    }
    
    // Open new log file
    logFile_.open(filename, std::ios::out | std::ios::app);
    if (!logFile_.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
        return false;
    }
    
    return true;
}

void Logger::closeLogFile() {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void Logger::enableConsoleLogging(bool enable) {
    consoleLogging_.store(enable, std::memory_order_relaxed);
}

void Logger::log(LogLevel level, const std::string& source, const std::string& message) {
    // Skip if message level is below current log level
    if (level < currentLevel_) {
        return;
    }
    
    // Format the log message
    std::stringstream logStream;
    logStream << getCurrentTimestamp() << " ["
              << levelToString(level) << "] ["
              << source << "] "
              << message;
    
    std::string logMessage = logStream.str();
    
    // Acquire lock for thread safety during writing
    std::lock_guard<std::mutex> lock(logMutex_);
    
    // Write to console if enabled
    if (consoleLogging_.load(std::memory_order_relaxed)) {
        // Use different output streams based on level
        if (level >= LogLevel::ERROR) {
            std::cerr << logMessage << std::endl;
        } else {
            std::cout << logMessage << std::endl;
        }
    }
    
    // Write to file if open
    if (logFile_.is_open()) {
        logFile_ << logMessage << std::endl;
        logFile_.flush();
    }
}

void Logger::trace(const std::string& source, const std::string& message) {
    log(LogLevel::TRACE, source, message);
}

void Logger::debug(const std::string& source, const std::string& message) {
    log(LogLevel::DEBUG, source, message);
}

void Logger::info(const std::string& source, const std::string& message) {
    log(LogLevel::INFO, source, message);
}

void Logger::warn(const std::string& source, const std::string& message) {
    log(LogLevel::WARN, source, message);
}

void Logger::error(const std::string& source, const std::string& message) {
    log(LogLevel::ERROR, source, message);
}

void Logger::fatal(const std::string& source, const std::string& message) {
    log(LogLevel::FATAL, source, message);
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        case LogLevel::OFF:   return "OFF  ";
        default:              return "UNKN ";
    }
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << now_ms.count();
    
    return ss.str();
}

} // namespace tapi 
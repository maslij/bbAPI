#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <atomic>
#include <memory>
#include <sstream>

namespace tapi {

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
    OFF
};

class Logger {
public:
    /**
     * @brief Get the singleton instance of the Logger
     * 
     * @return Logger& The logger instance
     */
    static Logger& getInstance();

    /**
     * @brief Set the global log level
     * 
     * @param level The new log level
     */
    void setLogLevel(LogLevel level);

    /**
     * @brief Get the current log level
     * 
     * @return LogLevel The current log level
     */
    LogLevel getLogLevel() const;

    /**
     * @brief Set the output file for logging
     * 
     * @param filename Path to the log file
     * @return true if file was opened successfully, false otherwise
     */
    bool setOutputFile(const std::string& filename);

    /**
     * @brief Close the current log file if open
     */
    void closeLogFile();

    /**
     * @brief Enable or disable console logging
     * 
     * @param enable True to enable console logging, false to disable
     */
    void enableConsoleLogging(bool enable);

    /**
     * @brief Log a message at the specified level
     * 
     * @param level The log level for this message
     * @param source The source of the log message (e.g., class name)
     * @param message The message to log
     */
    void log(LogLevel level, const std::string& source, const std::string& message);

    // Convenience methods for different log levels
    void trace(const std::string& source, const std::string& message);
    void debug(const std::string& source, const std::string& message);
    void info(const std::string& source, const std::string& message);
    void warn(const std::string& source, const std::string& message);
    void error(const std::string& source, const std::string& message);
    void fatal(const std::string& source, const std::string& message);

    /**
     * @brief Destructor - ensures log file is closed
     */
    ~Logger();

private:
    // Private constructor for singleton
    Logger();
    
    // Delete copy and move constructors and assignment operators
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    /**
     * @brief Convert LogLevel to string
     * 
     * @param level The log level
     * @return std::string The string representation
     */
    static std::string levelToString(LogLevel level);

    /**
     * @brief Get current timestamp as string
     * 
     * @return std::string Formatted timestamp
     */
    static std::string getCurrentTimestamp();

    std::atomic<LogLevel> currentLevel_;
    std::atomic<bool> consoleLogging_;
    std::ofstream logFile_;
    std::mutex logMutex_;
};

// Macro for easy logging
#define LOG_TRACE(source, message) tapi::Logger::getInstance().trace(source, message)
#define LOG_DEBUG(source, message) tapi::Logger::getInstance().debug(source, message)
#define LOG_INFO(source, message) tapi::Logger::getInstance().info(source, message)
#define LOG_WARN(source, message) tapi::Logger::getInstance().warn(source, message)
#define LOG_ERROR(source, message) tapi::Logger::getInstance().error(source, message)
#define LOG_FATAL(source, message) tapi::Logger::getInstance().fatal(source, message)

} // namespace tapi 
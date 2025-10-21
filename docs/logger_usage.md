# tAPI Logger Usage Guide

The tAPI logger provides a flexible, thread-safe logging system with multiple log levels and output options.

## Basic Usage

To use the logger in your component:

```cpp
#include "logger.h"

using namespace tapi;

// In your function:
LOG_INFO("MyComponent", "This is an info message");
LOG_ERROR("MyComponent", "An error occurred: " + errorMessage);
```

## Log Levels

The logger supports the following log levels (from most to least verbose):

1. `TRACE`: Very detailed information, useful for debugging
2. `DEBUG`: Debugging information
3. `INFO`: General information (default)
4. `WARN`: Warning messages
5. `ERROR`: Error messages
6. `FATAL`: Critical errors that cause the application to abort
7. `OFF`: Disable all logging

## Setting Log Level

### At Application Start

You can set the log level when starting the application:

```bash
./tAPI --log-level=debug
```

### At Runtime via Code

You can change the log level at runtime from C++ code:

```cpp
#include "logger.h"

using namespace tapi;

// Set log level to debug
Logger::getInstance().setLogLevel(LogLevel::DEBUG);

// Get current log level
LogLevel currentLevel = Logger::getInstance().getLogLevel();
```

### At Runtime via REST API

You can also change the log level at runtime using the REST API:

#### Get current log level

```
GET /api/v1/system/log-level
```

Response:
```json
{
  "level": "info"
}
```

#### Change log level

```
PUT /api/v1/system/log-level
Content-Type: application/json

{
  "level": "debug"
}
```

Response:
```json
{
  "success": true,
  "previous_level": "info",
  "current_level": "debug"
}
```

Valid log level values are: `trace`, `debug`, `info`, `warn`, `error`, `fatal`, and `off`.

Example using curl:
```bash
# Get current log level
curl -X GET http://localhost:8080/api/v1/system/log-level

# Change log level to debug
curl -X PUT -H "Content-Type: application/json" \
  -d '{"level": "debug"}' \
  http://localhost:8080/api/v1/system/log-level
```

## File Logging

### At Application Start

You can specify a log file when starting the application:

```bash
./tAPI --log-file=/path/to/log/file.log
```

### At Runtime

You can set or change the log file at runtime:

```cpp
Logger::getInstance().setOutputFile("/path/to/log/file.log");
```

## Console Logging

Console logging is enabled by default. You can disable it if needed:

```cpp
Logger::getInstance().enableConsoleLogging(false);
```

## Using Log Macros

The following macros are available for logging:

```cpp
LOG_TRACE("Component", "Message");  // Trace level
LOG_DEBUG("Component", "Message");  // Debug level
LOG_INFO("Component", "Message");   // Info level
LOG_WARN("Component", "Message");   // Warning level
LOG_ERROR("Component", "Message");  // Error level
LOG_FATAL("Component", "Message");  // Fatal level
```

## Direct API Access

You can also use the Logger API directly:

```cpp
Logger::getInstance().log(LogLevel::DEBUG, "Component", "Message");
Logger::getInstance().debug("Component", "Message");
Logger::getInstance().info("Component", "Message");
// ...
```

## Example

```cpp
#include "logger.h"

void MyFunction() {
    // Get logger instance
    tapi::Logger& logger = tapi::Logger::getInstance();
    
    // Set log level
    logger.setLogLevel(tapi::LogLevel::DEBUG);
    
    // Log messages
    logger.debug("MyComponent", "Debug message");
    logger.info("MyComponent", "Info message");
    
    // Using macros
    LOG_INFO("MyComponent", "Another info message");
    
    try {
        // Some operation that might fail
        throw std::runtime_error("Something went wrong");
    } catch (const std::exception& e) {
        LOG_ERROR("MyComponent", std::string("Exception caught: ") + e.what());
    }
}
``` 
# tAPI: Adding New Components

This guide explains how to add new components to the tAPI system, such as new processor types, source types, or sink types. By following these steps, you can extend the system's functionality to support new algorithms, data sources, or output methods.

## Table of Contents

1. [Understanding the Component Architecture](#understanding-the-component-architecture)
2. [Adding a New Processor Component](#adding-a-new-processor-component)
3. [Adding a New Source Component](#adding-a-new-source-component)
4. [Adding a New Sink Component](#adding-a-new-sink-component)
5. [Integration with the UI](#integration-with-the-ui)
6. [Testing Your Component](#testing-your-component)

## Understanding the Component Architecture

The tAPI system follows a modular architecture with three main component types:

- **Source Components**: Handle video input (e.g., RTSP streams, file input)
- **Processor Components**: Process frames with computer vision algorithms (e.g., object detection, tracking)
- **Sink Components**: Handle output of processed data (e.g., file output, database storage)

All components inherit from the base `Component` class with specific subclasses for each component type.

## Adding a New Processor Component

This section walks through the steps of adding a new processor component, using the `ObjectClassificationProcessor` as an example.

### 1. Define the Component Type

First, add your new processor type to the `ProcessorType` enum in `tAPI/include/license.h`:

```cpp
enum class ProcessorType {
    OBJECT_DETECTION,
    OBJECT_TRACKING,
    LINE_ZONE_MANAGER,
    FACE_RECOGNITION,
    MOTION_DETECTION,
    OBJECT_CLASSIFICATION,  // New processor type
    YOUR_NEW_PROCESSOR      // Add your new processor here
};
```

### 2. Create Header File

Create a header file in `tAPI/include/components/processor/` with the class declaration:

```cpp
// your_new_processor.h
#pragma once

#include "component.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>

namespace tapi {

class YourNewProcessor : public ProcessorComponent {
public:
    // Define any needed data structures (e.g., for results)
    struct Result {
        // Fields specific to your processor
    };
    
    // Constructor & Destructor
    YourNewProcessor(const std::string& id, Camera* camera, 
                     const std::string& type, const nlohmann::json& config);
    ~YourNewProcessor() override;
    
    // Override required virtual methods
    bool initialize() override;
    bool start() override;
    bool stop() override;
    bool updateConfig(const nlohmann::json& config) override;
    nlohmann::json getConfig() const override;
    nlohmann::json getStatus() const override;
    
    // Main processing method
    std::pair<cv::Mat, std::vector<Result>> processFrame(const cv::Mat& frame);
    
private:
    // Private helper methods
    
    // Component state
    std::string type_;
    mutable std::mutex mutex_;
    std::string lastError_;
    int processedFrames_;
    
    // Configuration parameters
    float someThreshold_;
    bool drawVisualizations_;
};

} // namespace tapi
```

### 3. Create Implementation File

Create an implementation file in `tAPI/src/components/processor/`:

```cpp
// your_new_processor.cpp
#include "components/processor/your_new_processor.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include "logger.h"

namespace tapi {

YourNewProcessor::YourNewProcessor(
    const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
    : ProcessorComponent(id, camera),
      type_(type),
      processedFrames_(0),
      someThreshold_(0.5f),
      drawVisualizations_(true) {
    
    // Apply initial configuration
    updateConfig(config);
}

YourNewProcessor::~YourNewProcessor() {
    stop();
}

bool YourNewProcessor::initialize() {
    std::cout << "Initializing YourNewProcessor: " << getId() << std::endl;
    
    try {
        // Initialization code here
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("Initialization error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return false;
    }
}

bool YourNewProcessor::start() {
    if (running_) {
        return true; // Already running
    }
    
    if (!initialize()) {
        return false;
    }
    
    running_ = true;
    std::cout << "YourNewProcessor started: " << getId() << std::endl;
    return true;
}

bool YourNewProcessor::stop() {
    if (!running_) {
        return true; // Already stopped
    }
    
    running_ = false;
    std::cout << "YourNewProcessor stopped: " << getId() << std::endl;
    return true;
}

bool YourNewProcessor::updateConfig(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (config.contains("some_threshold")) {
        someThreshold_ = config["some_threshold"];
    }
    
    if (config.contains("draw_visualizations")) {
        drawVisualizations_ = config["draw_visualizations"];
    }
    
    // Save the configuration
    config_ = config;
    
    return true;
}

nlohmann::json YourNewProcessor::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

nlohmann::json YourNewProcessor::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto status = Component::getStatus();
    
    // Override the generic "processor" type with the specific processor type
    status["type"] = "your_new_processor_type";
    
    status["processed_frames"] = processedFrames_;
    
    if (!lastError_.empty()) {
        status["last_error"] = lastError_;
    }
    
    return status;
}

std::pair<cv::Mat, std::vector<Result>> YourNewProcessor::processFrame(const cv::Mat& frame) {
    if (!running_ || frame.empty()) {
        return {frame, {}};
    }
    
    try {
        // Process the frame
        cv::Mat outputFrame = frame.clone();
        std::vector<Result> results;
        
        // Your processing logic here
        // ...
        
        // Draw visualizations if enabled
        if (drawVisualizations_) {
            // Draw on outputFrame
        }
        
        // Update statistics
        processedFrames_++;
        
        return {outputFrame, results};
        
    } catch (const std::exception& e) {
        lastError_ = std::string("Processing error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return {frame, {}};
    }
}

} // namespace tapi
```

### 4. Update String Conversion Functions

Update the string conversion functions in `tAPI/src/license.cpp`:

```cpp
// Add to processorTypeToString function
std::string processorTypeToString(ProcessorType type) {
    switch (type) {
        // Existing cases...
        case ProcessorType::YOUR_NEW_PROCESSOR:
            return "your_new_processor_type";
        default:
            return "unknown";
    }
}

// Add to stringToProcessorType function
ProcessorType stringToProcessorType(const std::string& typeStr) {
    // Existing cases...
    if (typeStr == "your_new_processor_type") {
        return ProcessorType::YOUR_NEW_PROCESSOR;
    }
    
    // IMPORTANT: Always use quotes around the string in error logs to diagnose string comparison issues
    // This can help detect whitespace or invisible character problems
    LOG_ERROR("License", "Unknown processor type: '" + typeStr + "'");
    throw std::invalid_argument("Unknown processor type: " + typeStr);
}
```

### 5. Update the Permission Helper

Update the `ComponentPermissionHelper` constructor in `tAPI/src/license.cpp` to set permission levels for your new component:

```cpp
ComponentPermissionHelper::ComponentPermissionHelper() {
    // ...
    
    // Initialize processor permissions for each license tier
    // [NONE, BASIC, STANDARD, PROFESSIONAL]
    processorPermissions_[ProcessorType::YOUR_NEW_PROCESSOR] = {false, false, false, true};
    
    // ...
}
```

### 6. Update Component Factory

Update `tAPI/src/component_factory.cpp` to register and create your new processor type:

```cpp
// Add include for your new processor
#include "components/processor/your_new_processor.h"

// In registerComponentTypes method
void ComponentFactory::registerComponentTypes() {
    // ...
    
    // Register processor types
    processorTypes_ = {
        // Existing processors...
        processorTypeToString(ProcessorType::YOUR_NEW_PROCESSOR),
    };
    
    // ...
}

// In createProcessorComponent method
std::shared_ptr<ProcessorComponent> ComponentFactory::createProcessorComponent(
    const std::string& id,
    Camera* camera,
    const std::string& type,
    const nlohmann::json& config) {
    
    // ...
    
    // Create the specific type of processor
    if (effectiveType == "your_new_processor_type") {
        return std::make_shared<YourNewProcessor>(id, camera, effectiveType, config);
    } else {
        // ...
    }
}
```

### 7. Update Camera Class (if needed)

If your processor produces unique types of results that need special handling, update the `processFrame` method in `tAPI/src/camera.cpp`. For example, if you add a new processor that produces unique telemetry events:

```cpp
// Process your new processor components
for (const auto& processor : processors) {
    if (processor->isRunning()) {
        // If it's your new processor type, use its specialized method
        auto yourProcessor = std::dynamic_pointer_cast<YourNewProcessor>(processor);
        if (yourProcessor) {
            auto result = yourProcessor->processFrame(processedFrame);
            processedFrame = result.first;
            
            // Convert each result to a standardized telemetry event
            for (const auto& item : result.second) {
                TelemetryEvent event = TelemetryFactory::createYourEvent(
                    processor->getId(),
                    // Other parameters specific to your event
                    currentTimestamp
                );
                event.setCameraId(id_);
                telemetryEvents.push_back(event);
            }
        }
    }
}
```

### 8. Add to Telemetry System (if needed)

If your processor creates a new type of telemetry event, update `tAPI/include/components/telemetry.h`:

```cpp
// Add a new event type
enum class TelemetryEventType {
    DETECTION,       // Object detection event
    TRACKING,        // Object tracking event
    CROSSING,        // Line crossing event
    CLASSIFICATION,  // Image classification event
    YOUR_NEW_EVENT,  // Your new event type
    CUSTOM           // Custom event type for extensions
};

// Add a new factory method for your event type
class TelemetryFactory {
public:
    // Existing methods...
    
    /**
     * @brief Create your new event type
     * 
     * @param sourceId Component that generated the event
     * @param yourParam Your specific parameters
     * @param timestamp Event timestamp (0 for current time)
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent createYourEvent(
        const std::string& sourceId,
        // Your params
        int64_t timestamp = 0) {
        
        TelemetryEvent event(TelemetryEventType::YOUR_NEW_EVENT, sourceId, timestamp);
        // Set event properties
        return event;
    }
};
```

### 9. Add to CMakeLists.txt

Add your implementation file to the build system in `tAPI/CMakeLists.txt`:

```cmake
# Processor component sources
set(PROCESSOR_COMPONENT_SOURCES
  # Existing processor files...
  src/components/processor/your_new_processor.cpp
)
```

## Adding a New Source Component

The process for adding a new source component is similar to adding a processor component:

1. Add a new enum value to `SourceType` in `license.h`
2. Create header and implementation files
3. Update string conversion functions in `license.cpp`
4. Update component factory in `component_factory.cpp`
5. Add the source file to CMakeLists.txt

Source components should focus on acquiring images/frames from external sources.

## Adding a New Sink Component

Similar to processors and sources:

1. Add a new enum value to `SinkType` in `license.h`
2. Create header and implementation files
3. Update string conversion functions
4. Update component factory
5. Add to CMakeLists.txt

Sink components handle the output of processed data, either as frames or metadata.

## Integration with the UI

To make your component usable from the UI, update the component mapping in `tWeb/src/components/pipeline/ComponentTypeMapping.ts`:

```typescript
export const processorTypeMapping: ComponentTypeMapping = {
  // Existing mappings...
  "your_new_processor_type": {
    name: "Human-Readable Name",
    description: "Short description of what your processor does",
    icon: React.createElement(MemoryIcon)
  }
};
```

## Testing Your Component

1. **Build the Project**: Ensure your component builds successfully.
   ```
   cd tAPI && ./scripts/build.sh
   ```

2. **Run the API Server**: Start the API server with your new component.
   ```
   ./build/tAPI --port 8090
   ```

3. **Test via UI**: Add your component to a camera pipeline using the web UI.

4. **Test via API**: Use the REST API to add your component to a camera pipeline.
   ```
   curl -X POST http://localhost:8090/api/v1/cameras/{camera_id}/components/processors \
     -H "Content-Type: application/json" \
     -d '{"id": "my_processor", "type": "your_new_processor_type", "config": {"some_threshold": 0.7}}'
   ```

## Configuring Component Dependencies

Components in the tAPI system can have dependencies on other components. For example, the object tracking processor requires an object detection processor to be present. These dependencies are managed in the `setupComponentRoutes` method in `tAPI/src/api.cpp`:

```cpp
// In setupComponentRoutes method
nlohmann::json dependencies;

// Specify component dependencies
// Key: component type, Value: array of required component types
dependencies["object_tracking"] = {"object_detection"};  // Tracking requires detection
dependencies["line_zone_manager"] = {"object_tracking"}; // Line crossing requires tracking
dependencies["object_classification"] = {}; // Classification doesn't require other processors

// Add global dependency rules
nlohmann::json rules;
rules.push_back("All processors require a source component");
rules.push_back("All sinks require a source component");

response["dependencies"] = dependencies;
response["dependency_rules"] = rules;
```

To add dependencies for your new component:

1. Identify which other components your component requires to function correctly
2. Update the `dependencies` JSON object in the `setupComponentRoutes` method
3. If your component doesn't have dependencies on other processors, use an empty array `{}`
4. If needed, add new global dependency rules to the `rules` array

### How Component Dependencies Are Enforced

The tAPI system enforces component dependencies in two ways:

1. **Backend Validation**: When creating a new component via the API, the system checks if required components exist.

2. **Frontend Validation**: The UI implements dependency validation in the `canAddComponent` function in `PipelineBuilder.tsx`:

   ```javascript
   // If this component type has dependencies, check if they're satisfied
   if (dependencies[type]) {
     const requiredTypes = dependencies[type];
     // Check if we have all required components
     for (const requiredType of requiredTypes) {
       // Check if any processor matches the required type
       const hasRequiredComponent = processorComponents.some(
         processor => processor.type === requiredType
       );
       
       if (!hasRequiredComponent) {
         return false;
       }
     }
   }
   ```

When a user tries to add a component through the UI, this function checks:

1. If a source component exists (all processors and sinks require a source)
2. If a component of the same type already exists (to prevent duplicates)
3. If license tier restrictions allow this component
4. If all dependencies specified in the `dependencies` object are satisfied

Components with unmet dependencies will be disabled in the UI, and users will see a tooltip explaining why they can't add the component.

For example:
- Line zone manager requires object tracking
- Object tracking requires object detection
- Object classification doesn't have any processor dependencies (but still requires a source component)

## Example: Adding an Object Classification Processor

The `ObjectClassificationProcessor` follows this pattern, using the `/classify` endpoint of the tAI REST server to perform image classification on whole frames:

1. It was defined in `ProcessorType` enum as `OBJECT_CLASSIFICATION`
2. It uses shared memory or base64 encoding to send frames to the AI server
3. It processes classification results and displays them on the video feed
4. It generates `CLASSIFICATION` telemetry events for other components to consume
5. It follows the same configuration pattern as other processors

## Troubleshooting Component Permission Issues

When adding new components, you might encounter issues with component permissions not being correctly applied. Here are some tips to troubleshoot these issues:

1. **Verify String Conversion Functions**: Ensure that your string conversion functions (`processorTypeToString` and `stringToProcessorType`) correctly handle your new component type. String comparison issues are common bugs.

2. **Use Detailed Error Logging**: Always include the exact input string in error messages with surrounding quotes:
   ```cpp
   LOG_ERROR("License", "Unknown processor type: '" + typeStr + "'");
   ```
   This helps diagnose whitespace, case sensitivity, or invisible character issues in string comparisons.

3. **Check Permission Map**: Verify that your component permissions are correctly set in `ComponentPermissionHelper` constructor:
   ```cpp
   // Format is [NONE, BASIC, STANDARD, PROFESSIONAL]
   processorPermissions_[ProcessorType::YOUR_NEW_PROCESSOR] = {false, false, true, true};
   ```

4. **Test Via API**: Test your component permissions using the API:
   ```
   curl http://localhost:8090/api/v1/component-types | jq '.permissions.processor.your_new_processor_type'
   ```

5. **Check License Tier**: Verify the current license tier is as expected:
   ```
   curl http://localhost:8090/api/v1/license | jq
   ```

These validation steps can help ensure that your component is correctly registered and has the proper permissions for different license tiers.

## Conclusion

By following this guide, you can extend the tAPI system with custom components that integrate seamlessly with the existing architecture. Remember to follow the established patterns and coding standards to ensure compatibility with the rest of the system. 
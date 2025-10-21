#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>

namespace tapi {

/**
 * @brief Standardized telemetry event type
 */
enum class TelemetryEventType {
    DETECTION,       // Object detection event
    TRACKING,        // Object tracking event
    CROSSING,        // Line crossing event
    CLASSIFICATION,  // Image classification event
    CUSTOM           // Custom event type for extensions
};

/**
 * @brief Bounding box representation for telemetry
 */
struct TelemetryBBox {
    int x;
    int y;
    int width;
    int height;
    
    // Convert to JSON
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["x"] = x;
        j["y"] = y;
        j["width"] = width;
        j["height"] = height;
        return j;
    }
    
    // Create from JSON
    static TelemetryBBox fromJson(const nlohmann::json& j) {
        TelemetryBBox bbox;
        bbox.x = j.value("x", 0);
        bbox.y = j.value("y", 0);
        bbox.width = j.value("width", 0);
        bbox.height = j.value("height", 0);
        return bbox;
    }
    
    // Convert from OpenCV Rect
    static TelemetryBBox fromRect(const cv::Rect& rect) {
        TelemetryBBox bbox;
        bbox.x = rect.x;
        bbox.y = rect.y;
        bbox.width = rect.width;
        bbox.height = rect.height;
        return bbox;
    }
    
    // Convert to OpenCV Rect
    cv::Rect toRect() const {
        return cv::Rect(x, y, width, height);
    }
};

/**
 * @brief Telemetry point for trajectory tracking
 */
struct TelemetryPoint {
    int x;
    int y;
    int64_t timestamp_ms;  // Optional timestamp for the point
    
    // Convert to JSON
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["x"] = x;
        j["y"] = y;
        j["timestamp_ms"] = timestamp_ms;
        return j;
    }
    
    // Create from JSON
    static TelemetryPoint fromJson(const nlohmann::json& j) {
        TelemetryPoint point;
        point.x = j.value("x", 0);
        point.y = j.value("y", 0);
        point.timestamp_ms = j.value("timestamp_ms", int64_t(0));
        return point;
    }
    
    // Convert from OpenCV Point
    static TelemetryPoint fromPoint(const cv::Point& pt, int64_t timestamp_ms = 0) {
        TelemetryPoint point;
        point.x = pt.x;
        point.y = pt.y;
        point.timestamp_ms = timestamp_ms;
        return point;
    }
    
    // Convert to OpenCV Point
    cv::Point toPoint() const {
        return cv::Point(x, y);
    }
};

/**
 * @brief Standardized telemetry event class
 */
class TelemetryEvent {
public:
    /**
     * @brief Create a telemetry event
     * 
     * @param type The event type
     * @param sourceId ID of the component that generated the event
     * @param timestamp Timestamp in milliseconds since epoch
     */
    TelemetryEvent(TelemetryEventType type, const std::string& sourceId, int64_t timestamp = 0) 
        : type_(type), sourceId_(sourceId) {
        // Use current time if timestamp is 0
        if (timestamp == 0) {
            timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        } else {
            timestamp_ = timestamp;
        }
    }
    
    /**
     * @brief Get the event type
     * 
     * @return TelemetryEventType The event type
     */
    TelemetryEventType getType() const { return type_; }
    
    /**
     * @brief Get the source component ID
     * 
     * @return std::string The source component ID
     */
    std::string getSourceId() const { return sourceId_; }
    
    /**
     * @brief Get the event timestamp
     * 
     * @return int64_t Timestamp in milliseconds since epoch
     */
    int64_t getTimestamp() const { return timestamp_; }
    
    /**
     * @brief Set a property on this event
     * 
     * @param key Property name
     * @param value Property value (any JSON-serializable type)
     */
    template<typename T>
    void setProperty(const std::string& key, const T& value) {
        properties_[key] = value;
    }
    
    /**
     * @brief Get a property value
     * 
     * @param key Property name
     * @param defaultValue Default value if property not found
     * @return T The property value
     */
    template<typename T>
    T getProperty(const std::string& key, const T& defaultValue) const {
        auto it = properties_.find(key);
        if (it != properties_.end()) {
            return it.value().get<T>();
        }
        return defaultValue;
    }
    
    /**
     * @brief Check if property exists
     * 
     * @param key Property name
     * @return bool True if property exists
     */
    bool hasProperty(const std::string& key) const {
        return properties_.contains(key);
    }
    
    /**
     * @brief Set the camera ID associated with this event
     * 
     * @param cameraId The camera ID
     */
    void setCameraId(const std::string& cameraId) {
        cameraId_ = cameraId;
    }
    
    /**
     * @brief Get the camera ID
     * 
     * @return std::string The camera ID
     */
    std::string getCameraId() const { return cameraId_; }
    
    /**
     * @brief Convert the telemetry event to JSON
     * 
     * @return nlohmann::json JSON representation of the event
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["type"] = static_cast<int>(type_);
        j["source_id"] = sourceId_;
        j["timestamp"] = timestamp_;
        j["camera_id"] = cameraId_;
        j["properties"] = properties_;
        return j;
    }
    
    /**
     * @brief Create a telemetry event from JSON
     * 
     * @param j JSON representation of the event
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent fromJson(const nlohmann::json& j) {
        TelemetryEventType type = static_cast<TelemetryEventType>(j.value("type", 0));
        std::string sourceId = j.value("source_id", "");
        int64_t timestamp = j.value("timestamp", int64_t(0));
        
        TelemetryEvent event(type, sourceId, timestamp);
        
        if (j.contains("camera_id")) {
            event.setCameraId(j["camera_id"]);
        }
        
        if (j.contains("properties") && j["properties"].is_object()) {
            event.properties_ = j["properties"];
        }
        
        return event;
    }
    
private:
    TelemetryEventType type_;
    std::string sourceId_;
    std::string cameraId_;
    int64_t timestamp_;
    nlohmann::json properties_;
};

/**
 * @brief Helper class for common telemetry event creation
 */
class TelemetryFactory {
public:
    /**
     * @brief Create a detection event
     * 
     * @param sourceId Component that generated the event
     * @param className Detected object class
     * @param confidence Detection confidence (0-1)
     * @param bbox Bounding box
     * @param timestamp Event timestamp (0 for current time)
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent createDetectionEvent(
        const std::string& sourceId,
        const std::string& className,
        float confidence,
        const TelemetryBBox& bbox,
        int64_t timestamp = 0) {
        
        TelemetryEvent event(TelemetryEventType::DETECTION, sourceId, timestamp);
        event.setProperty("class_name", className);
        event.setProperty("confidence", confidence);
        event.setProperty("bbox", bbox.toJson());
        return event;
    }
    
    /**
     * @brief Create a tracking event
     * 
     * @param sourceId Component that generated the event
     * @param trackId Track identifier
     * @param className Tracked object class
     * @param confidence Track confidence (0-1)
     * @param bbox Current bounding box
     * @param trajectory Vector of trajectory points
     * @param timestamp Event timestamp (0 for current time)
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent createTrackingEvent(
        const std::string& sourceId,
        int trackId,
        const std::string& className,
        float confidence,
        const TelemetryBBox& bbox,
        const std::vector<TelemetryPoint>& trajectory,
        int64_t timestamp = 0) {
        
        TelemetryEvent event(TelemetryEventType::TRACKING, sourceId, timestamp);
        event.setProperty("track_id", trackId);
        event.setProperty("class_name", className);
        event.setProperty("confidence", confidence);
        event.setProperty("bbox", bbox.toJson());
        
        nlohmann::json trajectoryJson = nlohmann::json::array();
        for (const auto& point : trajectory) {
            trajectoryJson.push_back(point.toJson());
        }
        event.setProperty("trajectory", trajectoryJson);
        
        return event;
    }
    
    /**
     * @brief Create a line crossing event
     * 
     * @param sourceId Component that generated the event
     * @param zoneId ID of the line zone
     * @param trackId ID of the track that crossed the line
     * @param className Class of the object that crossed
     * @param direction Crossing direction ("in" or "out")
     * @param crossingPoint Point where crossing occurred
     * @param timestamp Event timestamp (0 for current time)
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent createCrossingEvent(
        const std::string& sourceId,
        const std::string& zoneId,
        int trackId,
        const std::string& className,
        const std::string& direction,
        const TelemetryPoint& crossingPoint,
        int64_t timestamp = 0) {
        
        TelemetryEvent event(TelemetryEventType::CROSSING, sourceId, timestamp);
        event.setProperty("zone_id", zoneId);
        event.setProperty("track_id", trackId);
        event.setProperty("class_name", className);
        event.setProperty("direction", direction);
        event.setProperty("crossing_point", crossingPoint.toJson());
        return event;
    }
    
    /**
     * @brief Create a zone entry event
     * 
     * @param sourceId Component that generated the event
     * @param zoneId ID of the polygon zone
     * @param trackId ID of the track that entered the zone
     * @param className Class of the object that entered
     * @param entryPoint Point where entry occurred
     * @param timestamp Event timestamp (0 for current time)
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent createZoneEntryEvent(
        const std::string& sourceId,
        const std::string& zoneId,
        int trackId,
        const std::string& className,
        const TelemetryPoint& entryPoint,
        int64_t timestamp = 0) {
        
        TelemetryEvent event(TelemetryEventType::CUSTOM, sourceId, timestamp);
        event.setProperty("event_type", "zone_entry");
        event.setProperty("zone_id", zoneId);
        event.setProperty("track_id", trackId);
        event.setProperty("class_name", className);
        event.setProperty("entry_point", entryPoint.toJson());
        return event;
    }
    
    /**
     * @brief Create a zone exit event
     * 
     * @param sourceId Component that generated the event
     * @param zoneId ID of the polygon zone
     * @param trackId ID of the track that exited the zone
     * @param className Class of the object that exited
     * @param exitPoint Point where exit occurred
     * @param timestamp Event timestamp (0 for current time)
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent createZoneExitEvent(
        const std::string& sourceId,
        const std::string& zoneId,
        int trackId,
        const std::string& className,
        const TelemetryPoint& exitPoint,
        int64_t timestamp = 0) {
        
        TelemetryEvent event(TelemetryEventType::CUSTOM, sourceId, timestamp);
        event.setProperty("event_type", "zone_exit");
        event.setProperty("zone_id", zoneId);
        event.setProperty("track_id", trackId);
        event.setProperty("class_name", className);
        event.setProperty("exit_point", exitPoint.toJson());
        return event;
    }
    
    /**
     * @brief Create a classification event
     * 
     * @param sourceId Component that generated the event
     * @param className Classification class name
     * @param confidence Classification confidence (0-1)
     * @param timestamp Event timestamp (0 for current time)
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent createClassificationEvent(
        const std::string& sourceId,
        const std::string& className,
        float confidence,
        int64_t timestamp = 0) {
        
        TelemetryEvent event(TelemetryEventType::CLASSIFICATION, sourceId, timestamp);
        event.setProperty("class_name", className);
        event.setProperty("confidence", confidence);
        return event;
    }
    
    /**
     * @brief Create a custom event type
     * 
     * @param sourceId Component that generated the event
     * @param customType String identifier for the custom event type
     * @param customData Custom data as JSON object
     * @param timestamp Event timestamp (0 for current time)
     * @return TelemetryEvent The created event
     */
    static TelemetryEvent createCustomEvent(
        const std::string& sourceId,
        const std::string& customType,
        const nlohmann::json& customData,
        int64_t timestamp = 0) {
        
        TelemetryEvent event(TelemetryEventType::CUSTOM, sourceId, timestamp);
        event.setProperty("custom_type", customType);
        event.setProperty("custom_data", customData);
        return event;
    }
};

} // namespace tapi 
#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <memory>
#include <nlohmann/json.hpp>

namespace tapi {

// Forward declarations
struct Track;
struct Detection;
struct Event;

/**
 * @brief Enum for different positions on a bounding box
 */
enum class Position {
    TOP_LEFT,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT,
    CENTER,
    TOP_CENTER,
    BOTTOM_CENTER,
    CENTER_LEFT,
    CENTER_RIGHT,
    CENTER_OF_MASS
};

/**
 * @brief Simple Point class for line processing
 */
struct Point {
    float x;
    float y;
    
    Point() : x(0), y(0) {}
    Point(float x, float y) : x(x), y(y) {}
};

/**
 * @brief Vector class for line operations
 */
struct Vector {
    Point start;
    Point end;
    
    Vector() : start(0, 0), end(0, 0) {}
    Vector(const Point& start, const Point& end) : start(start), end(end) {}
    
    float Magnitude() const {
        float dx = end.x - start.x;
        float dy = end.y - start.y;
        return std::sqrt(dx*dx + dy*dy);
    }
    
    float CrossProduct(const Point& point) const {
        return (end.x - start.x) * (point.y - start.y) - (end.y - start.y) * (point.x - start.x);
    }
};

/**
 * @brief Structure for tracking objects
 */
struct Track {
    int trackId;
    cv::Rect bbox;
    std::string classId;
    std::string className;
    float confidence;
    int64_t timestamp;
};

/**
 * @brief Structure for detected objects
 */
struct Detection {
    cv::Rect bbox;
    std::string classId;
    std::string className;
    float confidence;
    int64_t timestamp;
};

/**
 * @brief Structure for line crossing events
 */
struct Event {
    int64_t timestamp;
    std::string objectId;
    std::string className;
    std::string type;
    cv::Point location;
    std::string zoneId;
    std::map<std::string, std::string> metadata;
};

/**
 * @brief Convert a position string to Position enum
 */
Position StringToPosition(const std::string& posStr);

/**
 * @brief Convert Position enum to string
 */
std::string PositionToString(Position pos);

/**
 * @brief Class representing a line zone for crossing detection
 */
class LineZone {
public:
    /**
     * @brief Constructor
     * 
     * @param id Unique ID for this line zone
     * @param startX Line start X coordinate
     * @param startY Line start Y coordinate
     * @param endX Line end X coordinate
     * @param endY Line end Y coordinate
     * @param streamId ID of the stream this line zone belongs to
     * @param minCrossingThreshold Minimum crossing threshold (default: 1)
     * @param triggeringAnchorStrings Vector of trigger anchor positions as strings
     * @param triggeringClasses Vector of class names that can trigger zone events (empty = all classes)
     */
    LineZone(const std::string& id,
            float startX, float startY,
            float endX, float endY,
            const std::string& streamId = "",
            int minCrossingThreshold = 1,
            const std::vector<std::string>& triggeringAnchorStrings = {},
            const std::vector<std::string>& triggeringClasses = {});
    
    /**
     * @brief Destructor
     */
    ~LineZone() = default;
    
    /**
     * @brief Initialize the line zone
     * 
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Reset the line zone to its initial state
     */
    void reset();
    
    /**
     * @brief Process tracks to detect line crossings
     * 
     * @param tracks Vector of tracked objects to process
     * @return Vector of crossing events
     */
    std::vector<Event> processTracks(const std::vector<Track>& tracks);
    
    /**
     * @brief Update line zone configuration
     * 
     * @param config JSON configuration object
     * @return true if update successful, false otherwise
     */
    bool updateConfig(const nlohmann::json& config);

    /**
     * @brief Get the line endpoints
     * 
     * @return Pair of start and end points
     */
    const std::pair<Point, Point> getLineEndpoints() const;
    
    /**
     * @brief Set the line endpoints
     * 
     * @param start New start point
     * @param end New end point
     */
    void setLineEndpoints(const Point& start, const Point& end);
    
    /**
     * @brief Get the ID of this line zone
     * 
     * @return The line zone ID
     */
    const std::string& getId() const { return id_; }
    
    /**
     * @brief Update the ID of this line zone
     * 
     * @param newId The new zone ID
     */
    void setId(const std::string& newId) { id_ = newId; }
    
    /**
     * @brief Get the number of objects that have crossed the line in the inward direction
     * 
     * @return The inward crossing count
     */
    int getInCount() const { 
        std::lock_guard<std::mutex> lock(countMutex_); 
        return inCount_; 
    }
    
    /**
     * @brief Get the number of objects that have crossed the line in the outward direction
     * 
     * @return The outward crossing count
     */
    int getOutCount() const { 
        std::lock_guard<std::mutex> lock(countMutex_); 
        return outCount_; 
    }
    
    /**
     * @brief Set the in count manually
     * 
     * @param count New in count value
     */
    void setInCount(int count) { 
        std::lock_guard<std::mutex> lock(countMutex_); 
        inCount_ = count; 
    }
    
    /**
     * @brief Set the out count manually
     * 
     * @param count New out count value
     */
    void setOutCount(int count) { 
        std::lock_guard<std::mutex> lock(countMutex_); 
        outCount_ = count; 
    }
    
    /**
     * @brief Get the stream ID this line zone belongs to
     * 
     * @return Stream ID
     */
    const std::string& getStreamId() const {
        return streamId_;
    }
    
    /**
     * @brief Get the minimum crossing threshold
     * 
     * @return The minimum crossing threshold
     */
    int getMinCrossingThreshold() const {
        return minCrossingThreshold_;
    }
    
    /**
     * @brief Get the triggering anchors
     * 
     * @return Vector of string representations of triggering anchors
     */
    std::vector<std::string> getTriggeringAnchors() const;
    
    /**
     * @brief Get the triggering classes
     * 
     * @return Vector of class names that can trigger zone events
     */
    std::vector<std::string> getTriggeringClasses() const;

private:
    // Line zone ID and stream ID
    std::string id_;
    std::string streamId_;
    
    // Line endpoints
    Point startPoint_;
    Point endPoint_;
    
    // Vector representing the line
    Vector line_;
    
    // Limit vectors for checking if objects are within line zone area
    Vector startRegionLimit_;
    Vector endRegionLimit_;
    
    // Tracking state
    int crossingHistoryLength_;
    std::unordered_map<int, std::deque<bool>> crossingStateHistory_;
    
    // Counts
    int inCount_;
    int outCount_;
    std::unordered_map<int, int> inCountPerClass_;
    std::unordered_map<int, int> outCountPerClass_;
    
    // Class ID to name mapping
    std::unordered_map<int, std::string> classIdToName_;
    
    // Triggering anchors (positions on bounding boxes to check)
    std::vector<Position> triggeringAnchors_;
    
    // Triggering classes (class names that can trigger zone events)
    std::vector<std::string> triggeringClasses_;
    
    // Minimum threshold for crossing detection
    int minCrossingThreshold_;
    
    // Initialization flag
    bool initialized_;

    // Mutex to protect count operations
    mutable std::mutex countMutex_;
    
    // Calculate region of interest limits based on the line vector
    std::pair<Vector, Vector> calculateRegionOfInterestLimits(const Vector& vector);
    
    // Compute which side of the line each detection is on
    std::tuple<std::vector<bool>, std::vector<bool>, std::vector<bool>> 
    computeAnchorSides(const std::vector<Detection>& detections) const;
    
    // Update class ID to name mapping
    void updateClassIdToName(const std::vector<Detection>& detections);
};

// Helper function to get current timestamp in milliseconds
int64_t getCurrentTimestamp();

} // namespace tapi 
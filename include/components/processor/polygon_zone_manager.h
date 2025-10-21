#pragma once

#include "component.h"
#include "geometry/polygon_zone.h"
#include "components/processor/object_tracker_processor.h"
#include "components/processor/zone_timer.h"
#include "components/telemetry.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <deque>
#include <nlohmann/json.hpp>
#include <chrono>

namespace tapi {

/**
 * @brief Represents a polygon zone crossing event
 */
struct PolygonZoneEvent {
    int64_t timestamp;
    std::string objectId;
    std::string className;
    cv::Point location;
    std::string zoneId;
    std::string eventType; // "zone_entry" or "zone_exit"
    std::map<std::string, std::string> metadata;
};

/**
 * @brief Processor component for managing polygon zones
 * 
 * The PolygonZoneManager allows defining polygon areas in a video frame
 * and detecting when objects enter or exit these zones.
 */
class PolygonZoneManager : public ProcessorComponent {
public:
    /**
     * @brief Constructor
     * 
     * @param id Component ID
     * @param camera Pointer to the parent camera
     * @param type Component type string
     * @param config Initial configuration
     */
    PolygonZoneManager(const std::string& id, Camera* camera, 
                      const std::string& type, const nlohmann::json& config);
    
    /**
     * @brief Destructor
     */
    ~PolygonZoneManager() override;
    
    /**
     * @brief Initialize the component
     * 
     * @return true if initialization was successful, false otherwise
     */
    bool initialize() override;
    
    /**
     * @brief Start the component
     * 
     * @return true if start was successful, false otherwise
     */
    bool start() override;
    
    /**
     * @brief Stop the component
     * 
     * @return true if stop was successful, false otherwise
     */
    bool stop() override;
    
    /**
     * @brief Update component configuration
     * 
     * @param config New configuration as JSON
     * @return true if update was successful, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override;
    
    /**
     * @brief Get the component status
     * 
     * @return Component status as JSON
     */
    nlohmann::json getStatus() const override;
    
    /**
     * @brief Process a frame with tracked objects to detect zone events
     * 
     * @param frame Input video frame
     * @param trackedObjects List of tracked objects from the object tracker
     * @return Processed frame and list of zone events
     */
    std::pair<cv::Mat, std::vector<PolygonZoneEvent>> 
    processFrame(const cv::Mat& frame, const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects);
    
    /**
     * @brief Add a polygon zone
     * 
     * @param id Zone ID
     * @param polygon Vector of points defining the polygon
     * @param triggeringAnchors List of anchor points that trigger the zone
     * @param triggeringClasses List of class names that can trigger zone events (empty = all classes)
     * @return true if zone was added successfully, false otherwise
     */
    bool addPolygonZone(const std::string& id, 
                        const std::vector<cv::Point2f>& polygon,
                        const std::vector<std::string>& triggeringAnchors = {"BOTTOM_CENTER"},
                        const std::vector<std::string>& triggeringClasses = {});
    
    /**
     * @brief Remove a polygon zone
     * 
     * @param id Zone ID to remove
     * @return true if zone was removed successfully, false otherwise
     */
    bool removePolygonZone(const std::string& id);
    
    /**
     * @brief Get IDs of all polygon zones
     * 
     * @return Vector of zone IDs
     */
    std::vector<std::string> getPolygonZoneIds() const;
    
    /**
     * @brief Get a specific polygon zone
     * 
     * @param id Zone ID
     * @return Shared pointer to the zone, or nullptr if not found
     */
    std::shared_ptr<PolygonZone> getPolygonZone(const std::string& id) const;
    
    /**
     * @brief Get all zone events
     * 
     * @return Vector of all zone events
     */
    std::vector<PolygonZoneEvent> getZoneEvents() const;
    
    /**
     * @brief Clear all stored zone events
     */
    void clearZoneEvents();
    
    /**
     * @brief Test polygon creation with a configuration
     * @param config Configuration with zones
     * @return true if test was successful
     */
    bool testPolygonCreation(const nlohmann::json& config);
    
private:
    /**
     * @brief Draw all polygon zones on a frame
     * 
     * @param frame Frame to draw on
     */
    void drawPolygonZones(cv::Mat& frame) const;
    
    /**
     * @brief Draw all polygon zones on a frame with track IDs for objects inside each zone
     * 
     * @param frame Frame to draw on
     * @param objectsInZones Map of zone IDs to vectors of track IDs that are inside each zone
     */
    void drawPolygonZones(cv::Mat& frame, const std::map<std::string, std::vector<int>>& objectsInZones) const;
    
    /**
     * @brief Draw tracked objects with time in zone information
     * 
     * @param frame Frame to draw on
     * @param trackedObjects List of tracked objects
     * @param objectsInZones Map of zone IDs to vectors of track IDs that are inside each zone
     * @param zoneTimesMap Map of zone IDs to maps of object IDs to time spent in zone
     */
    void drawObjectsWithTimeInZone(
        cv::Mat& frame, 
        const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects,
        const std::map<std::string, std::vector<int>>& objectsInZones,
        const std::map<std::string, std::unordered_map<int, double>>& zoneTimesMap) const;
    
    /**
     * @brief Format time duration in seconds to MM:SS format
     * 
     * @param seconds Time in seconds
     * @return Formatted time string
     */
    std::string formatTime(double seconds) const;
    
    /**
     * @brief Convert PolygonZone events to PolygonZoneEvents
     * 
     * @param events Events from a polygon zone
     * @return Converted PolygonZoneEvents
     */
    std::vector<PolygonZoneEvent> convertEvents(const std::vector<Event>& events) const;
    
    /**
     * @brief Convert tracked objects to tracks for zone processing
     * 
     * @param trackedObjects Objects from tracker
     * @return Vector of Track objects
     */
    std::vector<Track> convertTrackedObjects(
        const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects) const;
    
    /**
     * @brief Convert normalized coordinates to pixel coordinates
     * 
     * @param normalizedPoint Point with normalized coordinates (0.0-1.0)
     * @return Point with pixel coordinates
     */
    cv::Point normalizedToPixel(const cv::Point2f& normalizedPoint) const;
    
    /**
     * @brief Convert pixel coordinates to normalized coordinates
     * 
     * @param pixelPoint Point with pixel coordinates
     * @return Point with normalized coordinates (0.0-1.0)
     */
    cv::Point2f pixelToNormalized(const cv::Point& pixelPoint) const;
    
    /**
     * @brief Get current timestamp in milliseconds
     * 
     * @return int64_t Current timestamp
     */
    int64_t getCurrentTimestamp() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    
    // Component state variables
    std::map<std::string, std::shared_ptr<PolygonZone>> polygonZones_; ///< Map of zone ID to zone object
    std::map<std::string, cv::Scalar> zoneColors_; ///< Map of zone ID to zone color
    size_t nextColorIndex_; ///< Index for next color from palette
    mutable std::mutex mutex_; ///< Mutex for thread safety
    std::vector<PolygonZoneEvent> zoneEvents_; ///< List of zone events
    ZoneTimer zoneTimer_; ///< Timer for tracking time in zones
    
    // Visualization options
    bool drawZones_; ///< Whether to draw zones on the output frame
    cv::Scalar fillColor_; ///< Default fill color for polygon zones
    float opacity_; ///< Opacity for filled polygons
    cv::Scalar outlineColor_; ///< Default outline color for polygon zones
    int outlineThickness_; ///< Outline thickness for polygon zones
    bool drawLabels_; ///< Whether to draw zone labels
    cv::Scalar textColor_; ///< Text color for labels
    float textScale_; ///< Scale for label text
    int textThickness_; ///< Thickness for label text
    cv::Scalar textBackgroundColor_; ///< Background color for text
    int textPadding_; ///< Padding around text
    bool displayTextBox_; ///< Whether to display a background box behind text
    bool displayCounts_; ///< Whether to display object counts in each zone
    bool displayTimeInZone_; ///< Whether to display time in zone next to object IDs
    
    // Frame information
    int frameWidth_; ///< Current frame width
    int frameHeight_; ///< Current frame height
    bool useNormalizedCoords_; ///< Whether to use normalized coordinates (0.0-1.0)
};

} // namespace tapi 
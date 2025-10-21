#pragma once

#include "component.h"
#include "geometry/line_zone.h"
#include "components/processor/object_tracker_processor.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <nlohmann/json.hpp>

namespace tapi {

/**
 * @brief Structure to represent a line crossing event
 */
struct LineCrossingEvent {
    int64_t timestamp;            ///< Event timestamp
    std::string objectId;         ///< Object ID (track ID)
    std::string className;        ///< Object class name
    std::string direction;        ///< Crossing direction ("in" or "out")
    cv::Point location;           ///< Crossing location
    std::string zoneId;           ///< Line zone ID
    std::map<std::string, std::string> metadata; ///< Additional metadata
};

/**
 * @brief Component for managing line zones to detect object crossings
 * 
 * This component manages multiple line zones for detecting when objects 
 * cross defined lines. It visualizes the lines and crossing counts on the frame.
 * 
 * Features:
 * - Multiple line zones support
 * - In/out counting
 * - Normalized coordinates (0-1) for resolution independence
 * - Direction arrows visualization
 */
class LineZoneManager : public ProcessorComponent {
public:
    /**
     * @brief Construct a new Line Zone Manager
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Component type (should be "line_zone_manager")
     * @param config Initial configuration
     */
    LineZoneManager(const std::string& id, Camera* camera, 
                    const std::string& type, const nlohmann::json& config);
    
    /**
     * @brief Destructor
     */
    ~LineZoneManager() override;
    
    /**
     * @brief Initialize the manager
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override;
    
    /**
     * @brief Start the manager
     * 
     * @return true if start succeeded, false otherwise
     */
    bool start() override;
    
    /**
     * @brief Stop the manager
     * 
     * @return true if stop succeeded, false otherwise
     */
    bool stop() override;
    
    /**
     * @brief Update manager configuration
     * 
     * @param config JSON configuration
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override;
    
    /**
     * @brief Get component status
     * 
     * @return JSON object with status information
     */
    nlohmann::json getStatus() const override;
    
    /**
     * @brief Process a frame with tracked objects to check for line crossings
     * 
     * @param frame Input frame
     * @param trackedObjects Objects being tracked
     * @return pair of processed frame and crossing events
     */
    std::pair<cv::Mat, std::vector<LineCrossingEvent>> 
    processFrame(const cv::Mat& frame, const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects);
    
    /**
     * @brief Add a new line zone
     * 
     * @param id Line zone ID
     * @param startX Line start X coordinate (normalized 0-1)
     * @param startY Line start Y coordinate (normalized 0-1)
     * @param endX Line end X coordinate (normalized 0-1)
     * @param endY Line end Y coordinate (normalized 0-1)
     * @param minCrossingThreshold Minimum threshold for crossing detection
     * @param triggeringAnchors Vector of triggering anchor points
     * @return true if zone was added successfully, false otherwise
     */
    bool addLineZone(const std::string& id, 
                     float startX, float startY, 
                     float endX, float endY,
                     int minCrossingThreshold = 1,
                     const std::vector<std::string>& triggeringAnchors = {});
    
    /**
     * @brief Remove a line zone
     * 
     * @param id Line zone ID to remove
     * @return true if zone was removed successfully, false otherwise
     */
    bool removeLineZone(const std::string& id);
    
    /**
     * @brief Get all line zone IDs
     * 
     * @return Vector of line zone IDs
     */
    std::vector<std::string> getLineZoneIds() const;
    
    /**
     * @brief Get line zone by ID
     * 
     * @param id Line zone ID to get
     * @return shared_ptr to LineZone object or nullptr if not found
     */
    std::shared_ptr<LineZone> getLineZone(const std::string& id) const;
    
    /**
     * @brief Get all line crossing events
     * 
     * @return Vector of all line crossing events
     */
    std::vector<LineCrossingEvent> getCrossingEvents() const;
    
    /**
     * @brief Clear all line crossing events
     */
    void clearCrossingEvents();

    /**
     * @brief Convert normalized coordinates to pixel coordinates
     * 
     * @param normalizedPoint Point with coordinates in 0-1 range
     * @return Point with coordinates in pixel range
     */
    Point normalizedToPixel(const Point& normalizedPoint) const;
    
    /**
     * @brief Convert pixel coordinates to normalized coordinates
     * 
     * @param pixelPoint Point with coordinates in pixel range
     * @return Point with coordinates in 0-1 range
     */
    Point pixelToNormalized(const Point& pixelPoint) const;

private:
    /**
     * @brief Draw all line zones on a frame
     * 
     * @param frame Frame to draw on
     */
    void drawLineZones(cv::Mat& frame) const;
    
    /**
     * @brief Convert LineZone Events to LineCrossingEvents
     * 
     * @param events Vector of LineZone Events
     * @return Vector of LineCrossingEvents
     */
    std::vector<LineCrossingEvent> convertEvents(const std::vector<Event>& events) const;
    
    /**
     * @brief Convert tracked objects to LineZone tracks format
     * 
     * @param trackedObjects Vector of tracked objects
     * @return Vector of LineZone tracks
     */
    std::vector<Track> convertTrackedObjects(
        const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects) const;

    // Map of line zone ID to LineZone instance
    std::map<std::string, std::shared_ptr<LineZone>> lineZones_;
    
    // Drawing settings
    bool drawZones_;
    cv::Scalar lineColor_;
    int lineThickness_;
    bool drawCounts_;
    cv::Scalar textColor_;
    double textScale_;
    int textThickness_;
    
    // Direction arrow settings
    bool drawDirectionArrows_;
    cv::Scalar arrowColor_;
    float arrowSize_;
    float arrowHeadSize_;
    float arrowAngleDegrees_;
    
    // Endpoint circles
    bool drawEndpointCircles_;
    cv::Scalar circleColor_;
    int circleRadius_;
    
    // Text styling
    cv::Scalar textBackgroundColor_;
    int textPadding_;
    bool displayTextBox_;
    std::string inText_;
    std::string outText_;
    bool textOrientToLine_;
    bool textCentered_;
    
    // Stored crossing events
    std::vector<LineCrossingEvent> crossingEvents_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Camera dimensions
    int frameWidth_;
    int frameHeight_;
    
    // Flag to use normalized coordinates
    bool useNormalizedCoords_;
};

} // namespace tapi 
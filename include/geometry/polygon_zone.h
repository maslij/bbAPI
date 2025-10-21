#pragma once

#include <opencv2/opencv.hpp>
#include "geometry/line_zone.h"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <memory>
#include <nlohmann/json.hpp>

namespace tapi {

// Using structures from line_zone.h:
// Position (enum)
// Point
// Track
// Detection
// Event

/**
 * @brief Class for managing a polygon zone
 * 
 * A polygon zone defines an area in the video frame and can detect
 * when objects enter or exit the zone.
 */
class PolygonZone {
public:
    /**
     * @brief Construct a new Polygon Zone object
     * 
     * @param id Zone ID
     * @param polygon Vector of points defining the polygon
     * @param streamId ID of the stream/camera
     * @param triggeringAnchorStrings List of anchor points that trigger the zone
     * @param triggeringClasses List of class names that can trigger zone events (empty = all classes)
     */
    PolygonZone(const std::string& id,
               const std::vector<cv::Point2f>& polygon,
               const std::string& streamId,
               const std::vector<std::string>& triggeringAnchorStrings = {"BOTTOM_CENTER"},
               const std::vector<std::string>& triggeringClasses = {});
    
    /**
     * @brief Initialize the polygon zone
     * 
     * @return true if initialization was successful
     */
    bool initialize();
    
    /**
     * @brief Reset the polygon zone
     */
    void reset();
    
    /**
     * @brief Get the polygon vertices
     * 
     * @return const std::vector<cv::Point2f>& Polygon vertices
     */
    const std::vector<cv::Point2f>& getPolygon() const;
    
    /**
     * @brief Set polygon vertices
     * 
     * @param polygon New polygon vertices
     */
    void setPolygon(const std::vector<cv::Point2f>& polygon);
    
    /**
     * @brief Update zone configuration
     * 
     * @param config New configuration
     * @return true if update was successful
     */
    bool updateConfig(const nlohmann::json& config);
    
    /**
     * @brief Process tracks to generate zone entry/exit events
     * 
     * @param tracks Tracks to process
     * @return std::vector<Event> List of zone events
     */
    std::vector<Event> processTracks(const std::vector<Track>& tracks);
    
    /**
     * @brief Get the zone ID
     * 
     * @return std::string Zone ID
     */
    std::string getId() const { return id_; }
    
    /**
     * @brief Update the zone ID
     * 
     * @param newId The new zone ID
     */
    void setId(const std::string& newId) { id_ = newId; }
    
    /**
     * @brief Get the stream ID
     * 
     * @return std::string Stream ID
     */
    std::string getStreamId() const { return streamId_; }
    
    /**
     * @brief Get the in count (entries)
     * 
     * @return int In count
     */
    int getInCount() const { return inCount_; }
    
    /**
     * @brief Get the out count (exits)
     * 
     * @return int Out count
     */
    int getOutCount() const { return outCount_; }
    
    /**
     * @brief Get the current count (entries - exits)
     * 
     * @return int Current count
     */
    int getCurrentCount() const { return inCount_ - outCount_; }
    
    /**
     * @brief Get the list of triggering anchor points
     * 
     * @return std::vector<std::string> List of triggering anchor points
     */
    std::vector<std::string> getTriggeringAnchors() const;
    
    /**
     * @brief Get the list of triggering classes
     * 
     * @return std::vector<std::string> List of class names that can trigger zone events
     */
    std::vector<std::string> getTriggeringClasses() const;
    
    /**
     * @brief Check if detections are in the zone
     * 
     * @param detections Detections to check
     * @return std::vector<bool> Vector of boolean values indicating if each detection is in the zone
     */
    std::vector<bool> computeAnchorsInZone(const std::vector<Detection>& detections) const;
    
private:
    /**
     * @brief Create mask from polygon for efficient point-in-polygon testing
     */
    void createMask();
    
    /**
     * @brief Update class ID to name mapping
     * 
     * @param detections Detections to extract class info from
     */
    void updateClassIdToName(const std::vector<Detection>& detections);
    
    // Zone properties
    std::string id_;              ///< Zone ID
    std::string streamId_;        ///< Stream ID
    std::vector<cv::Point2f> polygon_; ///< Polygon vertices (using float points for normalized coords)
    cv::Mat mask_;               ///< Binary mask for point-in-polygon testing
    cv::Size frameResolution_;   ///< Frame resolution
    
    // Zone state
    int inCount_;                ///< Number of objects entering the zone
    int outCount_;               ///< Number of objects exiting the zone
    std::map<int, int> inCountPerClass_;  ///< In count per class ID
    std::map<int, int> outCountPerClass_; ///< Out count per class ID
    std::map<int, std::string> classIdToName_; ///< Mapping from class ID to name
    
    // Track state history
    std::map<int, std::deque<bool>> zoneStateHistory_; ///< Track state history (in zone or not)
    
    // Zone configuration
    std::vector<Position> triggeringAnchors_; ///< List of anchor points that trigger the zone
    std::vector<std::string> triggeringClasses_; ///< List of class names that can trigger zone events
    
    // Thread safety
    bool initialized_;           ///< Whether the zone is initialized
    mutable std::mutex countMutex_; ///< Mutex for thread safety
};

/**
 * @brief Class for annotating a polygon zone on a video frame
 */
class PolygonZoneAnnotator {
public:
    /**
     * @brief Construct a new Polygon Zone Annotator object
     * 
     * @param zone Polygon zone to annotate
     * @param color Color for the zone
     * @param thickness Line thickness
     * @param textColor Text color
     * @param textScale Text scale
     * @param textThickness Text thickness
     * @param textPadding Padding around text
     * @param displayCount Whether to display count
     * @param opacity Opacity for filled polygon
     */
    PolygonZoneAnnotator(
        const PolygonZone& zone,
        const cv::Scalar& color = cv::Scalar(0, 255, 0),
        int thickness = 2,
        const cv::Scalar& textColor = cv::Scalar(255, 255, 255),
        float textScale = 0.6f,
        int textThickness = 2,
        int textPadding = 5,
        bool displayCount = true,
        float opacity = 0.2f
    );
    
    /**
     * @brief Annotate a frame with the polygon zone
     * 
     * @param scene Frame to annotate
     * @param label Optional label to display
     * @return cv::Mat Annotated frame
     */
    cv::Mat annotate(cv::Mat scene, const std::string& label = "");
    
private:
    /**
     * @brief Calculate polygon center
     * 
     * @param polygon Polygon vertices
     * @return cv::Point Center point
     */
    cv::Point getPolygonCenter(const std::vector<cv::Point>& polygon) const;
    
    // Properties
    const PolygonZone& zone_;     ///< Reference to the polygon zone
    cv::Scalar color_;           ///< Color for the zone
    int thickness_;              ///< Line thickness
    cv::Scalar textColor_;       ///< Text color
    float textScale_;            ///< Text scale
    int textThickness_;          ///< Text thickness
    int textPadding_;            ///< Padding around text
    int font_;                   ///< Font type
    bool displayCount_;          ///< Whether to display count
    float opacity_;              ///< Opacity for filled polygon
    cv::Point center_;           ///< Center of the polygon
};

} // namespace tapi 
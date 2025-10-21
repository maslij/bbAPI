#pragma once

#include "component.h"
//#include "ByteTrack/BYTETracker.h"
#include "components/processor/object_detector_processor.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <memory>
#include <unordered_map>
#include <random>
#include <unordered_set>
#include <map>

#include "bytetrack/BYTETracker.h"

namespace tapi {

/**
 * @brief Object tracking processor component using ByteTracker
 */
class ObjectTrackerProcessor : public ProcessorComponent {
public:
    /**
     * @brief Structure to represent a tracked object
     */
    struct TrackedObject {
        int trackId;                      ///< Unique track ID
        std::string className;            ///< Class name
        float confidence;                 ///< Detection confidence
        cv::Rect bbox;                    ///< Bounding box
        int age;                          ///< Track age in frames
        std::vector<cv::Point> trajectory; ///< Track trajectory points
    };
    
    /**
     * @brief Construct a new Object Tracker Processor
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Component type (should be "object_tracking")
     * @param config Initial configuration
     */
    ObjectTrackerProcessor(const std::string& id, Camera* camera, 
                           const std::string& type, const nlohmann::json& config);
    
    /**
     * @brief Destructor
     */
    ~ObjectTrackerProcessor() override;
    
    /**
     * @brief Initialize the processor
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override;
    
    /**
     * @brief Start the processor
     * 
     * @return true if start succeeded, false otherwise
     */
    bool start() override;
    
    /**
     * @brief Stop the processor
     * 
     * @return true if stop succeeded, false otherwise
     */
    bool stop() override;
    
    /**
     * @brief Update processor configuration
     * 
     * @param config New configuration
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override;
    
    /**
     * @brief Get processor configuration
     * 
     * @return nlohmann::json Current configuration
     */
    nlohmann::json getConfig() const override;
    
    /**
     * @brief Get processor status
     * 
     * @return nlohmann::json Processor status
     */
    nlohmann::json getStatus() const override;
    
    /**
     * @brief Process a frame to track objects
     * 
     * @param frame Input frame
     * @param detections Input detections from object detector
     * @return std::pair<cv::Mat, std::vector<TrackedObject>> Processed frame with annotations and tracked objects
     */
    std::pair<cv::Mat, std::vector<TrackedObject>> processFrame(
        const cv::Mat& frame, 
        const std::vector<ObjectDetectorProcessor::Detection>& detections);
    
private:
    /**
     * @brief Convert ObjectDetectorProcessor::Detection to Object
     * 
     * @param detection Detection object
     * @return Object ByteTracker compatible object
     */
    Object convertDetection(const ObjectDetectorProcessor::Detection& detection);
    
    /**
     * @brief Draw tracking information on the frame
     * 
     * @param frame Frame to draw on
     * @param trackedObjects List of tracked objects
     * @return cv::Mat Frame with tracking visualizations
     */
    cv::Mat drawTracking(const cv::Mat& frame, const std::vector<TrackedObject>& trackedObjects);
    
private:
    std::string type_;                    ///< Component type
    int frameRate_;                       ///< Camera frame rate for tracking calculations
    int trackBuffer_;                     ///< Track buffer size (history)
    float trackThresh_;                   ///< Tracking threshold [0-1]
    float highThresh_;                    ///< High confidence threshold [0-1]
    float matchThresh_;                   ///< Matching threshold [0-1]
    bool drawTracking_;                   ///< Whether to draw tracking info on frame
    bool drawTrackId_;                    ///< Whether to draw track ID on label
    bool drawTrackTrajectory_;            ///< Whether to draw track trajectory
    bool drawSemiTransparentBoxes_;       ///< Whether to draw semi-transparent bounding boxes
    float labelFontScale_;                ///< Font scale for labels
    
    std::vector<cv::Scalar> colors_;      ///< Legacy: Colors for visualizing different tracks
    std::vector<std::string> uniqueClassNames_; ///< List of unique class names encountered
    std::vector<cv::Scalar> classColorMap_;   ///< Colors mapped to class names (same index as uniqueClassNames_)
    mutable std::mutex mutex_;            ///< Mutex for thread safety
    
    std::unique_ptr<BYTETracker> tracker_; ///< ByteTracker instance
    
    int totalTrackedObjects_;             ///< Total number of objects tracked
    int activeTrackedObjects_;            ///< Number of currently active tracked objects
    int processedFrames_;                 ///< Counter for processed frames
    std::string lastError_;               ///< Last error message
    
    // Persistent mapping between numeric labels and class names
    std::unordered_map<int, std::string> labelToClassMap_; ///< Map from numeric labels to class names
    
    // Random number generator for color generation
    std::mt19937 rng_;                    ///< Random number generator
    
    // Trajectory configuration
    size_t trajectoryMaxLength_;          ///< Maximum number of trajectory points to keep
    float maxAllowedDistanceRatio_;       ///< Maximum allowed distance between consecutive points as ratio of frame width
    int trajectoryCleanupThreshold_;      ///< Number of frames after which to remove disappeared tracks
    
    // Trajectory storage (moved from static variables in processFrame)
    std::map<int, std::vector<cv::Point>> trajectoryHistory_; ///< Trajectory history per track ID
    std::map<int, std::pair<cv::Point, std::string>> lastKnownPositions_; ///< Last known position and class per track ID
    std::map<int, int> trackDisappearCounter_; ///< Counter for frames since track was last seen
    
    // Persistent track class mapping to prevent label switching
    std::unordered_map<int, std::string> trackClassMap_; ///< Persistent class mapping for each track ID
    std::unordered_map<int, std::map<std::string, int>> trackClassConfidence_; ///< Track class confidence counts
};

} // namespace tapi 
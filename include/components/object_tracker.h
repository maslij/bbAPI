#pragma once

#include "component_instance.h"
#include "bytetrack/BYTETracker.h"
#include <opencv2/opencv.hpp>
#include <map>
#include <unordered_map>

namespace tapi {

/**
 * @brief Component that tracks objects across frames using ByteTrack algorithm
 */
class ObjectTracker : public ComponentInstance {
public:
    /**
     * @brief Construct a new Object Tracker component
     * 
     * @param node Pipeline node
     * @param componentDef Component definition
     */
    ObjectTracker(const PipelineNode& node, 
                 std::shared_ptr<VisionComponent> componentDef);
    
    /**
     * @brief Destructor
     */
    ~ObjectTracker() override = default;
    
    /**
     * @brief Initialize the tracker
     */
    bool initialize() override;
    
    /**
     * @brief Process frame to track objects
     */
    std::map<std::string, DataContainer> process(
        const std::map<std::string, DataContainer>& inputs) override;
    
    /**
     * @brief Reset the component
     */
    void reset() override;
    
    /**
     * @brief Update component configuration
     * 
     * @param newConfig The new configuration
     * @return true if the configuration was updated successfully
     */
    bool updateConfig(const std::map<std::string, nlohmann::json>& newConfig) override;
    
private:
    int frameRate_;                ///< Frame rate for tracker (used for track buffer)
    int trackBuffer_;              ///< Track buffer size 
    float trackThresh_;            ///< Detection confidence threshold for tracking
    float highThresh_;             ///< High confidence threshold for tracking
    float matchThresh_;            ///< Matching threshold for IOU
    int maxTrajectoryLength_;      ///< Maximum number of points to keep in trajectory
    bool storeTrajectory_;         ///< Whether to store trajectory information

    std::unique_ptr<BYTETracker> tracker_; ///< ByteTracker instance
    
    /// Mapping from track ID to class ID and class name
    std::map<int, std::pair<std::string, std::string>> classMapping_;

    /// Mapping from track ID to trajectory history
    std::map<int, std::vector<cv::Rect>> trackTrajectories_;
    
    /**
     * @brief Convert tapi::Detection to Object
     */
    Object convertDetectionToObject(const Detection& detection);
    
    /**
     * @brief Convert STrack to Track for output
     */
    Track convertSTrackToTrack(const STrack& strack, int64_t timestamp);
    
    /**
     * @brief Calculate Intersection over Union between two rectangles
     */
    double calculateIoU(const cv::Rect& rect1, const cv::Rect& rect2);

    /**
     * @brief Get current timestamp in milliseconds since epoch
     */
    int64_t getCurrentTimestamp() const;
};

} // namespace tapi 
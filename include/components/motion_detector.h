#pragma once

#include "component_instance.h"
#include <opencv2/opencv.hpp>

namespace tapi {

/**
 * @brief Component that detects motion in video frames
 */
class MotionDetector : public ComponentInstance {
public:
    /**
     * @brief Construct a new Motion Detector component
     * 
     * @param node Pipeline node
     * @param componentDef Component definition
     */
    MotionDetector(const PipelineNode& node, 
                 std::shared_ptr<VisionComponent> componentDef);
    
    /**
     * @brief Destructor
     */
    ~MotionDetector() override = default;
    
    /**
     * @brief Initialize the detector
     */
    bool initialize() override;
    
    /**
     * @brief Process frame to detect motion
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
    cv::Ptr<cv::BackgroundSubtractor> bgSubtractor_; ///< Background subtractor
    double sensitivity_;                             ///< Motion sensitivity [0-1]
    int history_;                                    ///< History length for background model
    int threshold_;                                  ///< Threshold for foreground mask
    cv::Mat prevFrame_;                              ///< Previous frame for simple diff method
    bool useBackgroundSubtraction_;                  ///< Whether to use bg subtraction vs. frame diff
    
    /**
     * @brief Extract motion regions as detections
     */
    std::vector<Detection> extractMotionRegions(const cv::Mat& frame, const cv::Mat& mask);
};

} // namespace tapi 
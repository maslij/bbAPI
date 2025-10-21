#pragma once

#include "component_instance.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <atomic>
#include <mutex>

namespace tapi {

/**
 * @brief Camera source component that serves as a visual indicator for video streams
 * 
 * This component doesn't capture video itself, but receives frames from the Stream class
 * and passes them through. It's primarily used as a UI element in the pipeline editor.
 */
class CameraSource : public ComponentInstance {
public:
    /**
     * @brief Construct a new Camera Source component
     * 
     * @param node Pipeline node
     * @param componentDef Component definition
     */
    CameraSource(const PipelineNode& node, 
                std::shared_ptr<VisionComponent> componentDef);
    
    /**
     * @brief Destructor
     */
    ~CameraSource() override;
    
    /**
     * @brief Initialize the camera visual component
     */
    bool initialize() override;
    
    /**
     * @brief Process frame
     * 
     * Passes through the frame provided in the inputs
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
    std::string cameraSource_;     ///< Camera source identifier (for display only)
    int fps_;                       ///< Target FPS (for display only)
    std::atomic<bool> running_;     ///< Component state tracking
    std::mutex configMutex_;        ///< Mutex for thread-safe configuration updates
};

} // namespace tapi 
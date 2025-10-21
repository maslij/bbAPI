#pragma once

#include "component_instance.h"
#include <opencv2/opencv.hpp>

namespace tapi {

/**
 * @brief Component that detects objects in images using the tAI API
 */
class ObjectDetector : public ComponentInstance {
public:
    /**
     * @brief Construct a new Object Detector component
     * 
     * @param node Pipeline node
     * @param componentDef Component definition
     */
    ObjectDetector(const PipelineNode& node, 
                  std::shared_ptr<VisionComponent> componentDef);
    
    /**
     * @brief Destructor
     */
    ~ObjectDetector() override;
    
    /**
     * @brief Initialize the detector
     */
    bool initialize() override;
    
    /**
     * @brief Process frame to detect objects by calling tAI API
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
    std::string modelName_;            ///< Model name (type)
    float confidenceThreshold_;        ///< Confidence threshold [0-1]
    std::vector<std::string> classes_; ///< Class names
    int inputWidth_;                   ///< Network input width
    int inputHeight_;                  ///< Network input height
    std::string apiEndpoint_;          ///< tAI API endpoint
    bool useSharedMemory_;             ///< Whether to use shared memory for image transfer
    int sharedMemoryFd_;               ///< Shared memory file descriptor
    std::string sharedMemoryName_;     ///< Current shared memory segment name
    
    /**
     * @brief Base64 encode binary data
     * 
     * @param data Pointer to binary data
     * @param length Length of data
     * @return std::string Base64 encoded string
     */
    std::string base64Encode(const unsigned char* data, size_t length);
    
    /**
     * @brief Create shared memory for an image
     * 
     * @param image The image to place in shared memory
     * @return std::string The shared memory key name, or empty string on failure
     */
    std::string createSharedMemory(const cv::Mat& image);
    
    /**
     * @brief Clean up shared memory resources
     */
    void cleanupSharedMemory();
};

} // namespace tapi 
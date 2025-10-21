#pragma once

#include "component.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility> // for std::pair
#include <random>  // for std::mt19937
#include <memory>  // for std::unique_ptr
#include <curl/curl.h> // For CURL instead of httplib

namespace tapi {

/**
 * @brief Object classification processor component using an AI server
 */
class ObjectClassificationProcessor : public ProcessorComponent {
public:
    /**
     * @brief Structure to represent a classification result
     */
    struct Classification {
        std::string className;
        float confidence;
    };
    
    /**
     * @brief Construct a new Object Classification Processor
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Component type (should be "object_classification")
     * @param config Initial configuration
     */
    ObjectClassificationProcessor(const std::string& id, Camera* camera, 
                           const std::string& type, const nlohmann::json& config);
    
    /**
     * @brief Destructor
     */
    ~ObjectClassificationProcessor() override;
    
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
     * @brief Process a frame to classify objects
     * 
     * @param frame Input frame
     * @return std::pair<cv::Mat, std::vector<Classification>> Processed frame with annotations and classifications
     */
    std::pair<cv::Mat, std::vector<Classification>> processFrame(const cv::Mat& frame);
    
    /**
     * @brief Get available models from the current AI server
     * 
     * @return std::vector<std::string> List of available model IDs
     */
    std::vector<std::string> getAvailableModels();
    
    /**
     * @brief Get available models from the AI server
     * 
     * @return std::vector<std::string> List of available model IDs
     */
    static std::vector<std::string> getAvailableModels(const std::string& serverUrl);
    
    /**
     * @brief Get supported classes for a model from the AI server
     * 
     * @param modelId ID of the model
     * @return std::vector<std::string> List of supported class names
     */
    static std::vector<std::string> getModelClasses(const std::string& serverUrl, const std::string& modelId);
    
    /**
     * @brief Get available model types for a model from the AI server
     * 
     * @param serverUrl URL of the AI server
     * @param modelId ID of the model
     * @return std::vector<std::string> List of supported model types
     */
    static std::vector<std::string> getAvailableModelTypes(const std::string& serverUrl, const std::string& modelId);
    
    /**
     * @brief Get health status of all models
     * 
     * @return nlohmann::json Model health information
     */
    static nlohmann::json getModelHealth(const std::string& serverUrl);

    /**
     * @brief Send an image to the AI server for classification
     * 
     * @param image Image to process
     * @return std::vector<Classification> Classification results
     */
    std::vector<Classification> classifyImage(const cv::Mat& image);
    
private:
    /**
     * @brief Convert image to base64 string
     * 
     * @param image Image to convert
     * @return std::string Base64-encoded image
     */
    std::string imageToBase64(const cv::Mat& image);
    
    /**
     * @brief Create a shared memory segment for an image and write the image data to it
     * 
     * @param image Image to store in shared memory
     * @return std::string Shared memory key
     */
    std::string createSharedMemory(const cv::Mat& image);
    
    /**
     * @brief Clean up the shared memory resources
     */
    void cleanupSharedMemory();
    
    /**
     * @brief Generate a random string for use as a shared memory key
     * 
     * @param length Length of the random string
     * @return std::string Random string
     */
    std::string generateRandomKey(size_t length = 16);
    
    /**
     * @brief Helper function to perform a GET request using CURL
     * 
     * @param endpoint API endpoint (without hostname)
     * @param responseJson Output JSON containing the response
     * @return bool Success status
     */
    bool curlGet(const std::string& endpoint, nlohmann::json& responseJson);
    
    /**
     * @brief Helper function to perform a POST request using CURL
     * 
     * @param endpoint API endpoint (without hostname)
     * @param requestJson JSON data to send in request body
     * @param responseJson Output JSON containing the response
     * @return bool Success status
     */
    bool curlPost(const std::string& endpoint, const nlohmann::json& requestJson, nlohmann::json& responseJson);
    
    /**
     * @brief Initialize CURL with common settings
     */
    void initCurl();
    
    /**
     * @brief Get the server URL from environment or config 
     */
    static std::string getServerUrlFromEnvOrConfig();
    
    /**
     * @brief CURL write callback to capture response data
     */
    static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* response);
    
private:
    std::string type_;                    ///< Component type
    std::string serverUrl_;               ///< AI server URL
    std::string modelId_;                 ///< Selected model ID
    std::string modelType_;               ///< Selected model type (e.g. resnet50, googlenet)
    float confidenceThreshold_;           ///< Confidence threshold [0-1]
    bool drawClassification_;             ///< Whether to draw classification on frame
    bool useSharedMemory_;                ///< Whether to use shared memory for image transfer
    float textFontScale_;                 ///< Font scale for text
    int sharedMemoryFd_;                  ///< Shared memory file descriptor
    std::string sharedMemoryKey_;         ///< Current shared memory segment name
    
    CURL* curl_;                          ///< CURL handle for API requests
    
    mutable std::mutex mutex_;            ///< Mutex for thread safety
    
    std::string lastError_;               ///< Last error message
    int processedFrames_;                 ///< Counter for processed frames
    int classificationCount_;             ///< Counter for total classifications
    std::mt19937 rng_;                    ///< Random number generator for shared memory keys
};

} // namespace tapi 
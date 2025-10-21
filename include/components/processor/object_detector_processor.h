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
#include <optional> // for std::optional
#include <curl/curl.h> // Keep CURL for backwards compatibility
#include "utils/shm_utils.h" // Add our new utility
#include "utils/http_client.h" // Use our custom HTTP client
#include "utils/grpc_client.h" // Use the actual Triton gRPC client
#include <chrono> // For std::chrono

namespace tapi {

// Forward declarations
class InferenceClient;
class InferenceConfig;

/**
 * @brief Result type for error handling
 */
template<typename T>
class Result {
private:
    std::optional<T> value_;
    std::string error_;
    
    explicit Result(T value) : value_(std::move(value)) {}
    explicit Result(std::string error) : error_(std::move(error)) {}
    
public:
    static Result<T> success(T value) { return Result(std::move(value)); }
    static Result<T> error(const std::string& msg) { return Result(msg); }
    
    bool isSuccess() const { return value_.has_value(); }
    bool isError() const { return !value_.has_value(); }
    const T& getValue() const { return *value_; }
    const std::string& getError() const { return error_; }
    
    // Allow move semantics
    T&& moveValue() { return std::move(*value_); }
};

/**
 * @brief Specialization for void type
 */
template<>
class Result<void> {
private:
    bool success_;
    std::string error_;
    
    explicit Result(bool success) : success_(success) {}
    explicit Result(std::string error) : success_(false), error_(std::move(error)) {}
    
public:
    static Result<void> success() { return Result(true); }
    static Result<void> error(const std::string& msg) { return Result(msg); }
    
    bool isSuccess() const { return success_; }
    bool isError() const { return !success_; }
    const std::string& getError() const { return error_; }
};

/**
 * @brief Configuration management for inference
 */
class InferenceConfig {
public:
    struct ModelConfig {
        std::string id = "yolov7";
        std::string inputName = "images";
        std::string outputName = "output";
        std::string inputFormat = "NCHW";
        int inputSize = 640;
        
        static ModelConfig fromModelId(const std::string& modelId);
    };
    
    struct NetworkConfig {
        std::string serverUrl;
        std::string protocol = "http";
        int timeout = 30;
        int retries = 3;
        int connectTimeout = 5;
        bool verboseLogging = false;
    };
    
    struct ProcessingConfig {
        float confidenceThreshold = 0.25f;
        float iouThreshold = 0.45f;
        std::vector<std::string> classes;
        bool drawBoundingBoxes = true;
        float labelFontScale = 0.5f;
    };
    
    InferenceConfig() = default;
    
    static Result<InferenceConfig> fromJson(const nlohmann::json& config);
    nlohmann::json toJson() const;
    
    const ModelConfig& getModelConfig() const { return model_; }
    const NetworkConfig& getNetworkConfig() const { return network_; }
    const ProcessingConfig& getProcessingConfig() const { return processing_; }
    
    void setModelConfig(const ModelConfig& config) { model_ = config; }
    void setNetworkConfig(const NetworkConfig& config) { network_ = config; }
    void setProcessingConfig(const ProcessingConfig& config) { processing_ = config; }
    
private:
    ModelConfig model_;
    NetworkConfig network_;
    ProcessingConfig processing_;
};

/**
 * @brief RAII wrapper for Triton inference inputs
 */
class TritonInputWrapper {
private:
    std::unique_ptr<triton::client::InferInput> input_;
    
public:
    TritonInputWrapper(const std::string& name, const std::vector<int64_t>& shape, const std::string& datatype);
    ~TritonInputWrapper() = default;
    
    // No copy, only move
    TritonInputWrapper(const TritonInputWrapper&) = delete;
    TritonInputWrapper& operator=(const TritonInputWrapper&) = delete;
    TritonInputWrapper(TritonInputWrapper&&) = default;
    TritonInputWrapper& operator=(TritonInputWrapper&&) = default;
    
    triton::client::InferInput* get() { return input_.get(); }
    const triton::client::InferInput* get() const { return input_.get(); }
    
    Result<void> setSharedMemory(const std::string& name, size_t size, size_t offset = 0);
    Result<void> appendRaw(const std::vector<uint8_t>& data);
};

/**
 * @brief RAII wrapper for Triton inference outputs
 */
class TritonOutputWrapper {
private:
    std::unique_ptr<triton::client::InferRequestedOutput> output_;
    
public:
    explicit TritonOutputWrapper(const std::string& name);
    ~TritonOutputWrapper() = default;
    
    // No copy, only move
    TritonOutputWrapper(const TritonOutputWrapper&) = delete;
    TritonOutputWrapper& operator=(const TritonOutputWrapper&) = delete;
    TritonOutputWrapper(TritonOutputWrapper&&) = default;
    TritonOutputWrapper& operator=(TritonOutputWrapper&&) = default;
    
    const triton::client::InferRequestedOutput* get() const { return output_.get(); }
};

/**
 * @brief RAII wrapper for complete Triton inference session
 */
class TritonInferenceSession {
private:
    std::vector<TritonInputWrapper> inputs_;
    std::vector<TritonOutputWrapper> outputs_;
    
public:
    // Input data storage to keep data alive during inference
    std::vector<uint8_t> inputData;
    
    TritonInferenceSession() = default;
    ~TritonInferenceSession() = default;
    
    // No copy, only move
    TritonInferenceSession(const TritonInferenceSession&) = delete;
    TritonInferenceSession& operator=(const TritonInferenceSession&) = delete;
    TritonInferenceSession(TritonInferenceSession&&) = default;
    TritonInferenceSession& operator=(TritonInferenceSession&&) = default;
    
    Result<void> addInput(const std::string& name, const std::vector<int64_t>& shape, const std::string& datatype);
    Result<void> addOutput(const std::string& name);
    
    Result<std::unique_ptr<triton::client::InferResult>> performInference(
        triton::client::InferenceServerClient* client, const std::string& modelId);
    
    TritonInputWrapper* getInput(size_t index) { return index < inputs_.size() ? &inputs_[index] : nullptr; }
    const TritonOutputWrapper* getOutput(size_t index) const { return index < outputs_.size() ? &outputs_[index] : nullptr; }
    
    size_t getInputCount() const { return inputs_.size(); }
    size_t getOutputCount() const { return outputs_.size(); }
};

/**
 * @brief Abstract base class for inference clients
 */
class InferenceClient {
public:
    virtual ~InferenceClient() = default;
    virtual Result<std::unique_ptr<triton::client::InferResult>> performInference(
        TritonInferenceSession& session, const std::string& modelId) = 0;
    virtual Result<std::vector<std::string>> getAvailableModels() = 0;
    virtual Result<bool> checkHealth() = 0;
};

/**
 * @brief Factory for creating inference clients
 */
class InferenceClientFactory {
public:
    static Result<std::unique_ptr<InferenceClient>> create(const InferenceConfig::NetworkConfig& config);
};

/**
 * @brief Object detection processor component using an AI server
 */
class ObjectDetectorProcessor : public ProcessorComponent {
public:
    /**
     * @brief Structure to represent a detection bounding box
     */
    struct Detection {
        std::string className;
        float confidence;
        cv::Rect bbox;
    };
    
    /**
     * @brief Inference context for tracking preprocessing results
     */
    struct InferenceContext {
        cv::Mat preprocessedImage;
        std::vector<int64_t> inputShape;
        float scaleX, scaleY;
        std::string inputFormat;
        int inputSize;
    };
    
    /**
     * @brief Construct a new Object Detector Processor
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Component type (should be "object_detection")
     * @param config Initial configuration
     */
    ObjectDetectorProcessor(const std::string& id, Camera* camera, 
                           const std::string& type, const nlohmann::json& config);
    
    /**
     * @brief Destructor
     */
    ~ObjectDetectorProcessor() override;
    
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
     * @brief Check if the AI server is available
     * 
     * @return bool True if server is available, false otherwise
     */
    bool checkServerAvailability();
    
    /**
     * @brief Process a frame to detect objects
     * 
     * @param frame Input frame
     * @return std::pair<cv::Mat, std::vector<Detection>> Processed frame with annotations and detected objects
     */
    std::pair<cv::Mat, std::vector<Detection>> processFrame(const cv::Mat& frame);
    
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
     * @brief Get health status of all models
     * 
     * @return nlohmann::json Model health information
     */
    static nlohmann::json getModelHealth(const std::string& serverUrl);

    /**
     * @brief Send an image to the AI server for detection (improved version)
     * 
     * @param image Image to process
     * @return Result<std::vector<Detection>> Detected objects or error
     */
    Result<std::vector<Detection>> detectObjectsImproved(const cv::Mat& image);
    
    /**
     * @brief Send an image to the AI server for detection (legacy version)
     * 
     * @param image Image to process
     * @return std::vector<Detection> Detected objects
     */
    std::vector<Detection> detectObjects(const cv::Mat& image);
    
private:
    // New improved methods
    
    /**
     * @brief Get model configuration for current model
     */
    InferenceConfig::ModelConfig getModelConfiguration() const;
    
    /**
     * @brief Preprocess image for inference
     */
    Result<InferenceContext> preprocessImage(const cv::Mat& image, const InferenceConfig::ModelConfig& config);
    
    /**
     * @brief Prepare inference session with inputs and outputs
     */
    Result<TritonInferenceSession> prepareInferenceSession(const InferenceContext& context, 
                                                          const InferenceConfig::ModelConfig& config);
    
    /**
     * @brief Parse inference results into detections
     */
    Result<std::vector<Detection>> parseInferenceResults(const triton::client::InferResult& result, 
                                                       const InferenceContext& context, 
                                                       const InferenceConfig::ModelConfig& config);
    
    /**
     * @brief Apply non-maximum suppression to detections
     */
    std::vector<Detection> applyNonMaximumSuppression(const std::vector<Detection>& detections, 
                                                     float iouThreshold) const;
    
    /**
     * @brief Draw bounding boxes on image
     */
    void drawDetections(cv::Mat& image, const std::vector<Detection>& detections) const;
    
    /**
     * @brief Log inference latency information
     */
    void logInferenceLatency(const std::chrono::high_resolution_clock::time_point& startTime,
                           const std::chrono::high_resolution_clock::time_point& endTime,
                           size_t detectionCount) const;
    
    // Legacy methods (kept for backward compatibility)
    
    /**
     * @brief Convert image to base64 string
     * 
     * @param image Image to convert
     * @return std::string Base64-encoded image
     */
    std::string imageToBase64(const cv::Mat& image);
    
    /**
     * @brief Create a shared memory segment for an image
     * 
     * @param image Image to store in shared memory
     * @return std::string Shared memory name
     */
    std::string createSharedMemory(const cv::Mat& image);
    
    /**
     * @brief Clean up the shared memory resources
     */
    void cleanupSharedMemory();
    
    /**
     * @brief Check server health for initialization purposes
     * 
     * @return bool True if server is healthy, false otherwise
     */
    bool checkServerHealth();
    
    /**
     * @brief Generate a random string for use as a shared memory key
     * 
     * @param length Length of the random string
     * @return std::string Random string
     */
    std::string generateRandomKey(size_t length = 16);
    
    /**
     * @brief Helper function to perform a GET request using HTTP client
     * 
     * @param endpoint API endpoint (without hostname)
     * @param responseJson Output JSON containing the response
     * @return bool Success status
     */
    bool curlGet(const std::string& endpoint, nlohmann::json& responseJson);
    
    /**
     * @brief Helper function to perform a POST request using HTTP client
     * 
     * @param endpoint API endpoint (without hostname)
     * @param requestJson JSON data to send in request body
     * @param responseJson Output JSON containing the response
     * @return bool Success status
     */
    bool curlPost(const std::string& endpoint, const nlohmann::json& requestJson, nlohmann::json& responseJson);
    
    /**
     * @brief Initialize HTTP client with common settings
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
    
    /**
     * @brief Get the class name for a given class ID from the model
     * 
     * @param classId Class ID from model output
     * @return std::string Class name
     */
    std::string getClassName(int classId);
    
private:
    // Configuration
    InferenceConfig config_;
    nlohmann::json legacyConfig_;         ///< Legacy config storage
    
    // Legacy configuration (for backward compatibility)
    std::string type_;                    ///< Component type
    std::string serverUrl_;               ///< AI server URL
    std::string modelId_;                 ///< Selected model ID
    float confidenceThreshold_;           ///< Confidence threshold [0-1]
    std::vector<std::string> classes_;    ///< Selected classes to detect
    bool drawBoundingBoxes_;              ///< Whether to draw bounding boxes on frame
    std::string protocol_;                ///< Protocol: "http", "http_shm", "grpc", "grpc_shm"
    float labelFontScale_;                ///< Font scale for labels
    
    // Inference client
    std::unique_ptr<InferenceClient> inferenceClient_;
    
    // Shared memory utility
    std::unique_ptr<utils::TritonSharedMemory> shm_;
    
    // Legacy HTTP clients (for backward compatibility)
    std::unique_ptr<triton::client::InferenceServerHttpClient> http_client_;
    std::unique_ptr<triton::client::InferenceServerGrpcClient> grpc_client_;
    
    // Keep CURL for backward compatibility during transition
    CURL* curl_; 
    
    std::vector<cv::Scalar> colors_;      ///< Colors for visualizing different classes
    mutable std::mutex mutex_;            ///< Mutex for thread safety
    
    std::string lastError_;               ///< Last error message
    int processedFrames_;                 ///< Counter for processed frames
    int detectionCount_;                  ///< Counter for total detected objects
    std::mt19937 rng_;                    ///< Random number generator for shared memory keys
    
    //! Flag indicating whether the processor has been initialized
    bool initialized_ = false;

    //! Flag indicating whether the AI server is available
    bool serverAvailable_ = true;
    
    //! Last time the server availability was checked
    std::chrono::steady_clock::time_point lastServerCheckTime_;
    
    //! Buffer to keep input data alive during inference (legacy)
    std::shared_ptr<std::vector<uint8_t>> inputDataBuffer_;
    
    //! Flag to control verbose logging (latency measurements every frame)
    bool verboseLogging_;
};

} // namespace tapi 
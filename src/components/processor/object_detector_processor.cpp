#include "components/processor/object_detector_processor.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <cmath>
#include <opencv2/dnn.hpp>
#include "logger.h"
// For shared memory
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <cstdlib> // For getenv
#include <cstdint> // For fixed-width integers like uint8_t
#include "utils/url_utils.h"
#include "utils/shm_utils.h" // Include our new utility
#include "config_manager.h"
#include "global_config.h" // Include GlobalConfig

namespace tapi {

// Shared memory image structure definition
struct SharedMemoryImage {
    int width;
    int height;
    int channels;
    int step;
    size_t dataSize;
    // Data follows this header in the shared memory
};

// Triton shared memory requires input and output names
struct TritonTensorShape {
    int64_t dims[4];  // NHWC format (batch, height, width, channels)
    int dimCount;
};

// Base64 encoding table
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

// Base64 encoding function
static std::string base64_encode(const unsigned char* buf, unsigned int bufLen) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (bufLen--) {
        char_array_3[i++] = *(buf++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; i < 4; i++) {
                ret += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++) {
            ret += base64_chars[char_array_4[j]];
        }

        while((i++ < 3)) {
            ret += '=';
        }
    }

    return ret;
}

// Generate random colors for visualization
static std::vector<cv::Scalar> generateColors(size_t numClasses) {
    std::vector<cv::Scalar> colors;
    std::mt19937 rng(12345); // Fixed seed for reproducibility
    std::uniform_int_distribution<int> dist(50, 255);
    
    for (size_t i = 0; i < numClasses; ++i) {
        colors.push_back(cv::Scalar(dist(rng), dist(rng), dist(rng)));
    }
    
    return colors;
}

// =============================================================================
// New Improved Classes Implementation
// =============================================================================

// -----------------------------------------------------------------------------
// InferenceConfig Implementation
// -----------------------------------------------------------------------------

InferenceConfig::ModelConfig InferenceConfig::ModelConfig::fromModelId(const std::string& modelId) {
    ModelConfig config;
    config.id = modelId;
    
    if (modelId == "yolov7") {
        config.inputName = "images";
        config.outputName = "num_dets,det_boxes,det_scores,det_classes";
        config.inputFormat = "NCHW";
        config.inputSize = 640;
    } else if (modelId == "yolov7_qat") {
        config.inputName = "images";
        config.outputName = "num_dets,det_boxes,det_scores,det_classes";
        config.inputFormat = "NCHW";
        config.inputSize = 640;
    } else if (modelId.find("yolov8") != std::string::npos) {
        config.inputName = "images";
        config.outputName = "output";
        config.inputFormat = "NCHW";
        config.inputSize = 640;
    } else if (modelId.find("yolov5") != std::string::npos) {
        config.inputName = "images";
        config.outputName = "output";
        config.inputFormat = "NCHW";
        config.inputSize = 640;
    } else {
        // Default YOLO configuration
        config.inputName = "images";
        config.outputName = "output";
        config.inputFormat = "NCHW";
        config.inputSize = 640;
    }
    
    return config;
}

Result<InferenceConfig> InferenceConfig::fromJson(const nlohmann::json& config) {
    InferenceConfig inferenceConfig;
    
    try {
        // Model configuration
        if (config.contains("model_id") && config["model_id"].is_string()) {
            inferenceConfig.model_ = ModelConfig::fromModelId(config["model_id"].get<std::string>());
        }
        
        // Network configuration
        if (config.contains("server_url") && config["server_url"].is_string()) {
            inferenceConfig.network_.serverUrl = config["server_url"].get<std::string>();
        }
        
        if (config.contains("protocol") && config["protocol"].is_string()) {
            inferenceConfig.network_.protocol = config["protocol"].get<std::string>();
        }
        
        if (config.contains("timeout") && config["timeout"].is_number()) {
            inferenceConfig.network_.timeout = config["timeout"].get<int>();
        }
        
        if (config.contains("verbose_logging") && config["verbose_logging"].is_boolean()) {
            inferenceConfig.network_.verboseLogging = config["verbose_logging"].get<bool>();
        }
        
        // Processing configuration
        if (config.contains("confidence_threshold") && config["confidence_threshold"].is_number()) {
            inferenceConfig.processing_.confidenceThreshold = config["confidence_threshold"].get<float>();
        }
        
        if (config.contains("classes") && config["classes"].is_array()) {
            inferenceConfig.processing_.classes.clear();
            for (const auto& item : config["classes"]) {
                if (item.is_string()) {
                    inferenceConfig.processing_.classes.push_back(item.get<std::string>());
                }
            }
        }
        
        if (config.contains("draw_bounding_boxes") && config["draw_bounding_boxes"].is_boolean()) {
            inferenceConfig.processing_.drawBoundingBoxes = config["draw_bounding_boxes"].get<bool>();
        }
        
        if (config.contains("label_font_scale") && config["label_font_scale"].is_number()) {
            inferenceConfig.processing_.labelFontScale = config["label_font_scale"].get<float>();
        }
        
        return Result<InferenceConfig>::success(std::move(inferenceConfig));
    } catch (const std::exception& e) {
        return Result<InferenceConfig>::error("Failed to parse configuration: " + std::string(e.what()));
    }
}

nlohmann::json InferenceConfig::toJson() const {
    nlohmann::json config;
    
    // Model configuration
    config["model_id"] = model_.id;
    
    // Network configuration
    config["server_url"] = network_.serverUrl;
    config["protocol"] = network_.protocol;
    config["timeout"] = network_.timeout;
    config["verbose_logging"] = network_.verboseLogging;
    
    // Processing configuration
    config["confidence_threshold"] = processing_.confidenceThreshold;
    config["classes"] = processing_.classes;
    config["draw_bounding_boxes"] = processing_.drawBoundingBoxes;
    config["label_font_scale"] = processing_.labelFontScale;
    
    return config;
}

// -----------------------------------------------------------------------------
// RAII Wrappers Implementation
// -----------------------------------------------------------------------------

TritonInputWrapper::TritonInputWrapper(const std::string& name, const std::vector<int64_t>& shape, const std::string& datatype) {
    triton::client::InferInput* raw_input = nullptr;
    triton::client::Error err = triton::client::InferInput::Create(&raw_input, name, shape, datatype);
    if (!err.IsOk()) {
        throw std::runtime_error("Failed to create input '" + name + "': " + err.Message());
    }
    input_.reset(raw_input);
}

Result<void> TritonInputWrapper::setSharedMemory(const std::string& name, size_t size, size_t offset) {
    if (!input_) {
        return Result<void>::error("Input wrapper not initialized");
    }
    
    triton::client::Error err = input_->SetSharedMemory(name, size, offset);
    if (!err.IsOk()) {
        return Result<void>::error("Failed to set shared memory: " + err.Message());
    }
    
    return Result<void>::success();
}

Result<void> TritonInputWrapper::appendRaw(const std::vector<uint8_t>& data) {
    if (!input_) {
        return Result<void>::error("Input wrapper not initialized");
    }
    
    triton::client::Error err = input_->AppendRaw(data);
    if (!err.IsOk()) {
        return Result<void>::error("Failed to append raw data: " + err.Message());
    }
    
    return Result<void>::success();
}

TritonOutputWrapper::TritonOutputWrapper(const std::string& name) {
    triton::client::InferRequestedOutput* raw_output = nullptr;
    triton::client::Error err = triton::client::InferRequestedOutput::Create(&raw_output, name);
    if (!err.IsOk()) {
        throw std::runtime_error("Failed to create output '" + name + "': " + err.Message());
    }
    output_.reset(raw_output);
}

Result<void> TritonInferenceSession::addInput(const std::string& name, const std::vector<int64_t>& shape, const std::string& datatype) {
    try {
        inputs_.emplace_back(name, shape, datatype);
        return Result<void>::success();
    } catch (const std::exception& e) {
        return Result<void>::error("Failed to add input: " + std::string(e.what()));
    }
}

Result<void> TritonInferenceSession::addOutput(const std::string& name) {
    try {
        outputs_.emplace_back(name);
        return Result<void>::success();
    } catch (const std::exception& e) {
        return Result<void>::error("Failed to add output: " + std::string(e.what()));
    }
}

Result<std::unique_ptr<triton::client::InferResult>> TritonInferenceSession::performInference(
    triton::client::InferenceServerClient* client, const std::string& modelId) {
    
    if (!client) {
        return Result<std::unique_ptr<triton::client::InferResult>>::error("Client is null");
    }
    
    // Prepare raw pointers for Triton client
    std::vector<triton::client::InferInput*> inputPtrs;
    std::vector<const triton::client::InferRequestedOutput*> outputPtrs;
    
    for (auto& input : inputs_) {
        inputPtrs.push_back(input.get());
    }
    
    for (const auto& output : outputs_) {
        outputPtrs.push_back(output.get());
    }
    
    // Create inference options
    triton::client::InferOptions options(modelId);
    
    // Perform inference
    triton::client::InferResult* result = nullptr;
    triton::client::Error err;
    
    // Try casting to HTTP client first
    auto* httpClient = dynamic_cast<triton::client::InferenceServerHttpClient*>(client);
    if (httpClient) {
        err = httpClient->Infer(&result, options, inputPtrs, outputPtrs);
    } else {
        // Try casting to gRPC client
        auto* grpcClient = dynamic_cast<triton::client::InferenceServerGrpcClient*>(client);
        if (grpcClient) {
            err = grpcClient->Infer(&result, options, inputPtrs, outputPtrs);
        } else {
            return Result<std::unique_ptr<triton::client::InferResult>>::error("Unknown client type");
        }
    }
    
    if (!err.IsOk()) {
        return Result<std::unique_ptr<triton::client::InferResult>>::error("Inference failed: " + err.Message());
    }
    
    return Result<std::unique_ptr<triton::client::InferResult>>::success(std::unique_ptr<triton::client::InferResult>(result));
}

// -----------------------------------------------------------------------------
// Inference Client Implementations
// -----------------------------------------------------------------------------

class HttpInferenceClient : public InferenceClient {
private:
    std::unique_ptr<triton::client::InferenceServerHttpClient> client_;
    std::string serverUrl_;
    
public:
    HttpInferenceClient(const std::string& serverUrl, bool verboseLogging) : serverUrl_(serverUrl) {
        triton::client::Error err = triton::client::InferenceServerHttpClient::Create(&client_, serverUrl, verboseLogging);
        if (!err.IsOk()) {
            throw std::runtime_error("Failed to create HTTP client: " + err.Message());
        }
    }
    
    Result<std::unique_ptr<triton::client::InferResult>> performInference(
        TritonInferenceSession& session, const std::string& modelId) override {
        return session.performInference(client_.get(), modelId);
    }
    
    Result<std::vector<std::string>> getAvailableModels() override {
        // Implementation would call the HTTP client to get models
        return Result<std::vector<std::string>>::error("Not implemented yet");
    }
    
    Result<bool> checkHealth() override {
        // Implementation would call the HTTP client to check health
        return Result<bool>::error("Not implemented yet");
    }
};

class GrpcInferenceClient : public InferenceClient {
private:
    std::unique_ptr<triton::client::InferenceServerGrpcClient> client_;
    std::string serverUrl_;
    
public:
    GrpcInferenceClient(const std::string& serverUrl, bool verboseLogging) : serverUrl_(serverUrl) {
        // Convert HTTP URL to gRPC URL
        std::string grpcUrl = serverUrl;
        if (grpcUrl.find("http://") == 0) {
            grpcUrl = grpcUrl.substr(7);
        } else if (grpcUrl.find("https://") == 0) {
            grpcUrl = grpcUrl.substr(8);
        }
        if (!grpcUrl.empty() && grpcUrl.back() == '/') {
            grpcUrl.pop_back();
        }
        
        // Adjust port for gRPC
        size_t colonPos = grpcUrl.rfind(':');
        if (colonPos != std::string::npos) {
            std::string host = grpcUrl.substr(0, colonPos);
            std::string port = grpcUrl.substr(colonPos + 1);
            if (port == "8000") {
                grpcUrl = host + ":8001";
            }
        }
        
        triton::client::Error err = triton::client::InferenceServerGrpcClient::Create(&client_, grpcUrl, verboseLogging);
        if (!err.IsOk()) {
            throw std::runtime_error("Failed to create gRPC client: " + err.Message());
        }
    }
    
    Result<std::unique_ptr<triton::client::InferResult>> performInference(
        TritonInferenceSession& session, const std::string& modelId) override {
        return session.performInference(client_.get(), modelId);
    }
    
    Result<std::vector<std::string>> getAvailableModels() override {
        return Result<std::vector<std::string>>::error("Not implemented yet");
    }
    
    Result<bool> checkHealth() override {
        return Result<bool>::error("Not implemented yet");
    }
};

// -----------------------------------------------------------------------------
// Inference Client Factory Implementation
// -----------------------------------------------------------------------------

Result<std::unique_ptr<InferenceClient>> InferenceClientFactory::create(const InferenceConfig::NetworkConfig& config) {
    try {
        if (config.protocol == "http" || config.protocol == "http_shm") {
            return Result<std::unique_ptr<InferenceClient>>::success(
                std::make_unique<HttpInferenceClient>(config.serverUrl, config.verboseLogging));
        } else if (config.protocol == "grpc" || config.protocol == "grpc_shm") {
            return Result<std::unique_ptr<InferenceClient>>::success(
                std::make_unique<GrpcInferenceClient>(config.serverUrl, config.verboseLogging));
        } else {
            return Result<std::unique_ptr<InferenceClient>>::error("Unsupported protocol: " + config.protocol);
        }
    } catch (const std::exception& e) {
        return Result<std::unique_ptr<InferenceClient>>::error("Failed to create inference client: " + std::string(e.what()));
    }
}

// =============================================================================
// ObjectDetectorProcessor Implementation (Updated)
// =============================================================================

ObjectDetectorProcessor::ObjectDetectorProcessor(
    const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
    : ProcessorComponent(id, camera),
      type_(type),
      serverUrl_(GlobalConfig::getInstance().getAiServerUrl()),  // Always use GlobalConfig as the source of truth
      modelId_("yolov7"),
      protocol_("grpc"),  // Default to HTTP protocol
      confidenceThreshold_(0.25),  // Lower default threshold to match test_inference.py
      drawBoundingBoxes_(true),
      labelFontScale_(0.5f),
      shm_(new utils::TritonSharedMemory()),  // Initialize shared memory manager
      http_client_(nullptr),  // Initialize HTTP client to nullptr
      curl_(nullptr),
      processedFrames_(0),
      grpc_client_(nullptr),
      detectionCount_(0),
      rng_(std::random_device()()),
      serverAvailable_(true),
      lastServerCheckTime_(std::chrono::steady_clock::now()),
      verboseLogging_(false) {
    
    // Initialize the improved configuration system
    auto configResult = InferenceConfig::fromJson(config);
    if (configResult.isSuccess()) {
        config_ = configResult.getValue();
        std::cout << "Successfully initialized improved configuration system" << std::endl;
    } else {
        std::cerr << "Failed to initialize improved configuration: " << configResult.getError() << std::endl;
        // Continue with legacy configuration
    }
    
    // Log the initial server URL from GlobalConfig
    std::cout << "ObjectDetector server URL (from GlobalConfig): " << serverUrl_ << std::endl;
    
    // First, check directly in the config for protocol setting
    if (config.contains("protocol") && config["protocol"].is_string()) {
        protocol_ = config["protocol"].get<std::string>();
        std::cout << "ObjectDetector using protocol from config: " 
                  << protocol_ << std::endl;
    }
    // Check if use_shared_memory (legacy config) is set - if so, convert to appropriate protocol
    else if (config.contains("use_shared_memory") && config["use_shared_memory"].is_boolean()) {
        bool use_shared_memory = config["use_shared_memory"].get<bool>();
        protocol_ = use_shared_memory ? "http_shm" : "http";
        std::cout << "ObjectDetector using protocol based on legacy shared memory setting: " 
                  << protocol_ << std::endl;
    }
    // Next, check global config
    else {
        bool use_shared_memory = GlobalConfig::getInstance().getUseSharedMemory();
        protocol_ = use_shared_memory ? "http_shm" : "http";
        std::cout << "ObjectDetector using protocol based on global shared memory setting: " 
                  << protocol_ << std::endl;
    }
    
    // Apply initial configuration
    updateConfig(config);
    
    // Log the final server URL after config is applied
    std::cout << "ObjectDetector final server URL: " << serverUrl_ << " (use this for connections)" << std::endl;
    
    // Check if we need to prefer specific model type
    if (config.contains("model_id") && !config["model_id"].is_null()) {
        if (config["model_id"].is_string()) {
            modelId_ = config["model_id"].get<std::string>();
            std::cout << "Model ID set from config: " << modelId_ << std::endl;
        }
    }
    
    // Generate default colors
    colors_ = generateColors(20); // Default to 20 classes
    
    // Initialize CURL global once
    curl_global_init(CURL_GLOBAL_ALL);
}

ObjectDetectorProcessor::~ObjectDetectorProcessor() {
    std::cout << "ObjectDetectorProcessor destructor started for " << getId() << std::endl;
    
    // Always call stop to ensure proper cleanup
    stop();
    
    // Clean up HTTP client
    if (http_client_) {
        std::cout << "Cleaning up HTTP client in destructor for " << getId() << std::endl;
        http_client_.reset();
    }

    if (grpc_client_) {
        std::cout << "Cleaning up gRPC client in destructor for " << getId() << std::endl;
        grpc_client_.reset();
    }
    
    // Clean up CURL
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    
    // Clean up global CURL resources
    curl_global_cleanup();

    // Explicitly clean up shared memory resources
    cleanupSharedMemory();
    
    std::cout << "ObjectDetectorProcessor destructor completed for " << getId() << std::endl;
}

bool ObjectDetectorProcessor::initialize() {
    // If already initialized, only reinitialize shared memory if needed
    if (initialized_) {
        std::cout << "Object Detector already initialized: " << getId() << std::endl;
        
        // If using shared memory protocol, check if we need to reinitialize it
        if ((protocol_ == "http_shm" || protocol_ == "grpc_shm") && 
            (!shm_->isValid() || shm_->getSharedMemoryInfo() == std::make_tuple(std::string(""), nullptr, size_t(0)))) {
            std::cout << "Reinitializing shared memory for " << getId() << std::endl;
            
            // Get Triton server URL from GlobalConfig
            serverUrl_ = GlobalConfig::getInstance().getAiServerUrl();
            
            bool serverAvailable = checkServerAvailability();
            if (!serverAvailable) {
                std::cerr << "Cannot reinitialize shared memory: Triton server is not available at " << serverUrl_ << std::endl;
                return true; // Return true anyway since processor is initialized, just without shared memory
            }
            
            // Create a dummy image for initialization
            const int maxWidth = 1280;
            const int maxHeight = 1280;
            const int channels = 3;
            cv::Mat dummyImage(maxHeight, maxWidth, CV_8UC3, cv::Scalar(0, 0, 0));
            
            std::string shmName = "tapi_persistent_" + getId();
            std::cout << "Initializing persistent shared memory region '" << shmName << "'" << std::endl;
            
            // Create with skipRegistration=true to avoid double registration
            std::string result = shm_->createImageSharedMemory(dummyImage, shmName, true);
            if (result.empty()) {
                std::cerr << "Failed to initialize shared memory, will use HTTP for data transfer" << std::endl;
                // Switch to non-shared memory protocol
                if (protocol_ == "http_shm") protocol_ = "http";
                if (protocol_ == "grpc_shm") protocol_ = "grpc";
            } else {
                std::cout << "Successfully initialized shared memory region for inference" << std::endl;
                
                // Now explicitly register with Triton server
                if (!shm_->registerWithTritonServer()) {
                    std::cerr << "Failed to register shared memory with Triton, will use HTTP for data transfer" << std::endl;
                    // Switch to non-shared memory protocol
                    if (protocol_ == "http_shm") protocol_ = "http";
                    if (protocol_ == "grpc_shm") protocol_ = "grpc";
                    shm_->cleanup();
                } else {
                    std::cout << "Successfully registered shared memory with Triton server" << std::endl;
                }
            }
        }
        
        // Check if we need to initialize the HTTP client
        if (protocol_ == "http" && !http_client_) {
            std::cout << "Initializing HTTP client for " << getId() << std::endl;
            try {
                triton::client::Error err = triton::client::InferenceServerHttpClient::Create(
                    &http_client_, serverUrl_, verboseLogging_);
                if (!err.IsOk()) {
                    std::cerr << "Failed to create HTTP client: " << err.Message() << std::endl;
                    // Continue anyway, we'll fall back to local CURL implementation
                } else {
                    std::cout << "Successfully created HTTP client" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception creating HTTP client: " << e.what() << std::endl;
                // Continue anyway, we'll fall back to local CURL implementation
            }
        }
        
        // Check if we need to initialize the gRPC client
        if (protocol_ == "grpc" && !grpc_client_) {
            std::cout << "Initializing gRPC client for " << getId() << std::endl;
            try {
                // Extract host and port from serverUrl for gRPC
                std::string grpcUrl = serverUrl_;
                // Remove http:// or https:// prefix if present
                if (grpcUrl.find("http://") == 0) {
                    grpcUrl = grpcUrl.substr(7);
                } else if (grpcUrl.find("https://") == 0) {
                    grpcUrl = grpcUrl.substr(8);
                }
                // Remove trailing slash if present
                if (!grpcUrl.empty() && grpcUrl.back() == '/') {
                    grpcUrl.pop_back();
                }
                
                // Check if we need to adjust the port for gRPC
                // Triton typically uses 8000 for HTTP and 8001 for gRPC
                size_t colonPos = grpcUrl.rfind(':');
                if (colonPos != std::string::npos) {
                    std::string host = grpcUrl.substr(0, colonPos);
                    std::string port = grpcUrl.substr(colonPos + 1);
                    if (port == "8000") {
                        // Switch to gRPC port
                        grpcUrl = host + ":8001";
                        std::cout << "Switching from HTTP port 8000 to gRPC port 8001" << std::endl;
                    }
                }
                
                std::cout << "Creating gRPC client with URL: " << grpcUrl << std::endl;
                triton::client::Error err = triton::client::InferenceServerGrpcClient::Create(
                    &grpc_client_, grpcUrl, false);  // false for verbose logging
                if (!err.IsOk()) {
                    std::cerr << "Failed to create gRPC client: " << err.Message() << std::endl;
                    // Continue anyway, we'll fall back to local CURL implementation
                } else {
                    std::cout << "Successfully created gRPC client" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception creating gRPC client: " << e.what() << std::endl;
                // Continue anyway, we'll fall back to local CURL implementation
            }
        }
        
        return true;
    }
    
    std::cout << "Initializing Object Detector processor: " << getId() << std::endl;
    std::cout << "Using server URL: " << serverUrl_ << std::endl;
    
    try {
        // Initialize CURL if needed (only log once)
        if (!curl_) {
            initCurl();
        }
        
        // Check if Triton server is available
        bool serverAvailable = false;
        try {
            // Create a fresh CURL handle for health check to avoid using potentially corrupted handle
            CURL* healthCheckCurl = curl_easy_init();
            if (!healthCheckCurl) {
                throw std::runtime_error("Failed to initialize CURL for health check");
            }
            
            // Use the standard Triton health endpoint (not model-specific)
            std::string url = serverUrl_;
            if (url.back() == '/') {
                url += "v2/health/ready";
            } else {
                url += "/v2/health/ready";
            }
            
            std::cout << "Checking Triton server health at: " << url << std::endl;
            
            // Set request options
            curl_easy_setopt(healthCheckCurl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(healthCheckCurl, CURLOPT_TIMEOUT, 2L); // 2 seconds timeout
            curl_easy_setopt(healthCheckCurl, CURLOPT_HTTPGET, 1L); // GET request
            
            // Add connection options for robustness
            curl_easy_setopt(healthCheckCurl, CURLOPT_CONNECTTIMEOUT, 1L); // 1 second connect timeout
            curl_easy_setopt(healthCheckCurl, CURLOPT_TCP_KEEPALIVE, 1L); // Enable TCP keepalive
            
            // Always ignore SSL verification for internal APIs
            curl_easy_setopt(healthCheckCurl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(healthCheckCurl, CURLOPT_SSL_VERIFYHOST, 0L);
            
            // Capture response data (even though we don't use it)
            std::string response;
            curl_easy_setopt(healthCheckCurl, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
            curl_easy_setopt(healthCheckCurl, CURLOPT_WRITEDATA, &response);
            
            // Perform the request
            CURLcode res = curl_easy_perform(healthCheckCurl);
            
            // Check if request was successful
            if (res == CURLE_OK) {
                // Get HTTP response code
                long httpCode = 0;
                curl_easy_getinfo(healthCheckCurl, CURLINFO_RESPONSE_CODE, &httpCode);
                
                if (httpCode == 200) {
                    serverAvailable = true;
                    std::cout << "Triton server is available." << std::endl;
                } else {
                    std::cout << "Triton server returned status code: " << httpCode << std::endl;
                }
            } else {
                std::cout << "Failed to connect to Triton server: " << curl_easy_strerror(res) << std::endl;
                
                // Try alternative health endpoint
                url = serverUrl_;
                if (url.back() == '/') {
                    url += "api/health/ready";
                } else {
                    url += "/api/health/ready";
                }
                
                std::cout << "Trying alternative health endpoint: " << url << std::endl;
                curl_easy_setopt(healthCheckCurl, CURLOPT_URL, url.c_str());
                res = curl_easy_perform(healthCheckCurl);
                
                if (res == CURLE_OK) {
                    long httpCode = 0;
                    curl_easy_getinfo(healthCheckCurl, CURLINFO_RESPONSE_CODE, &httpCode);
                    
                    if (httpCode == 200) {
                        serverAvailable = true;
                        std::cout << "Triton server is available at alternative endpoint." << std::endl;
                    }
                }
            }
            
            // Clean up the temporary CURL handle
            curl_easy_cleanup(healthCheckCurl);
        } catch (const std::exception& e) {
            std::cerr << "Error checking Triton server health: " << e.what() << std::endl;
        }
        
        if (!serverAvailable) {
            lastError_ = "Triton server is not available at " + serverUrl_;
            std::cerr << lastError_ << std::endl;
            
            // Mark server as unavailable
            serverAvailable_ = false;
            
            // For initialization we need the server to be available, so return false
            return false;
        } else {
            serverAvailable_ = true;
        }
        
        // Initialize HTTP client if using HTTP protocol
        if (protocol_ == "http" && !http_client_) {
            std::cout << "Initializing HTTP client for " << getId() << std::endl;
            try {
                triton::client::Error err = triton::client::InferenceServerHttpClient::Create(
                    &http_client_, serverUrl_, verboseLogging_);
                if (!err.IsOk()) {
                    std::cerr << "Failed to create HTTP client: " << err.Message() << std::endl;
                    // Continue anyway, we'll fall back to local CURL implementation
                } else {
                    std::cout << "Successfully created HTTP client" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception creating HTTP client: " << e.what() << std::endl;
                // Continue anyway, we'll fall back to local CURL implementation
            }
        }

        if (protocol_ == "grpc" && !grpc_client_) {
            std::cout << "Initializing gRPC client for " << getId() << std::endl;
            try {
                // Extract host and port from serverUrl for gRPC
                std::string grpcUrl = serverUrl_;
                // Remove http:// or https:// prefix if present
                if (grpcUrl.find("http://") == 0) {
                    grpcUrl = grpcUrl.substr(7);
                } else if (grpcUrl.find("https://") == 0) {
                    grpcUrl = grpcUrl.substr(8);
                }
                // Remove trailing slash if present
                if (!grpcUrl.empty() && grpcUrl.back() == '/') {
                    grpcUrl.pop_back();
                }
                
                // Check if we need to adjust the port for gRPC
                // Triton typically uses 8000 for HTTP and 8001 for gRPC
                size_t colonPos = grpcUrl.rfind(':');
                if (colonPos != std::string::npos) {
                    std::string host = grpcUrl.substr(0, colonPos);
                    std::string port = grpcUrl.substr(colonPos + 1);
                    if (port == "8000") {
                        // Switch to gRPC port
                        grpcUrl = host + ":8001";
                        std::cout << "Switching from HTTP port 8000 to gRPC port 8001" << std::endl;
                    }
                }
                
                std::cout << "Creating gRPC client with URL: " << grpcUrl << std::endl;
                triton::client::Error err = triton::client::InferenceServerGrpcClient::Create(
                    &grpc_client_, grpcUrl, false);  // false for verbose logging
                if (!err.IsOk()) {
                    std::cerr << "Failed to create gRPC client: " << err.Message() << std::endl;
                    // Continue anyway, we'll fall back to local CURL implementation
                } else {
                    std::cout << "Successfully created gRPC client" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception creating gRPC client: " << e.what() << std::endl;
                // Continue anyway, we'll fall back to local CURL implementation
            }
        }
        
        // Get available models if server is available
        auto availableModels = getAvailableModels(serverUrl_);
        
        // Validate the selected model if we have available models
        if (!availableModels.empty()) {
            bool modelFound = false;
            for (const auto& model : availableModels) {
                if (model == modelId_) {
                    modelFound = true;
                    break;
                }
            }
            
            if (!modelFound) {
                std::cout << "Selected model '" << modelId_ << "' not found. Using first available model: "
                       << availableModels[0] << std::endl;
                modelId_ = availableModels[0];
            }
            
            // Get class names for the selected model
            auto modelClasses = getModelClasses(serverUrl_, modelId_);
            
            // If no specific classes selected, use all available classes
            if (classes_.empty() && !modelClasses.empty()) {
                classes_ = modelClasses;
                std::cout << "Using all available classes for model " << modelId_ << std::endl;
            }
            
            // Generate colors for visualization (one per class)
            colors_ = generateColors(std::max(classes_.size(), modelClasses.size()));
        } else {
            // Generate some default colors anyway
            colors_ = generateColors(20);
            std::cout << "No models available from Triton server." << std::endl;
        }
        
        // Only initialize shared memory for non-temporary processors (with real camera connections)
        // Skip for temporary processors used for model discovery
        if ((protocol_ == "http_shm" || protocol_ == "grpc_shm") && serverAvailable && camera_ != nullptr) {
            // Ensure any existing shared memory is properly cleaned up first
            cleanupSharedMemory();
            
            // Reset the shared memory object to create a fresh instance
            shm_.reset(new utils::TritonSharedMemory());
            
            // Pre-allocate shared memory with a reasonable size for all possible model inputs
            // Maximum size would be 1280x1280 with 3 channels (RGB) for YOLOv7, which is ~20MB for float32
            const int maxWidth = 1280;
            const int maxHeight = 1280;
            const int channels = 3;
            
            // Create a dummy image for initialization
            cv::Mat dummyImage(maxHeight, maxWidth, CV_8UC3, cv::Scalar(0, 0, 0));
            
            std::string shmName = "tapi_persistent_" + getId();
            std::cout << "Initializing persistent shared memory region '" << shmName << "'" << std::endl;
            
            // Create with skipRegistration=true to avoid double registration
            std::string result = shm_->createImageSharedMemory(dummyImage, shmName, true);
            if (result.empty()) {
                std::cerr << "Failed to initialize shared memory, will use HTTP for data transfer" << std::endl;
                // Switch to non-shared memory protocol
                if (protocol_ == "http_shm") protocol_ = "http";
                if (protocol_ == "grpc_shm") protocol_ = "grpc";
            } else {
                std::cout << "Successfully initialized shared memory region for inference" << std::endl;
                
                // Now explicitly register with Triton server
                if (!shm_->registerWithTritonServer()) {
                    std::cerr << "Failed to register shared memory with Triton, will use HTTP for data transfer" << std::endl;
                    // Switch to non-shared memory protocol
                    if (protocol_ == "http_shm") protocol_ = "http";
                    if (protocol_ == "grpc_shm") protocol_ = "grpc";
                    shm_->cleanup();
                } else {
                    std::cout << "Successfully registered shared memory with Triton server" << std::endl;
                }
            }
        } else if ((protocol_ == "http_shm" || protocol_ == "grpc_shm") && camera_ == nullptr) {
            // For temporary processors, disable shared memory
            std::cout << "Temporary processor detected - skipping shared memory initialization" << std::endl;
            // Switch to non-shared memory protocol
            if (protocol_ == "http_shm") protocol_ = "http";
            if (protocol_ == "grpc_shm") protocol_ = "grpc";
        }
        
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("Initialization error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return false;
    }
}

bool ObjectDetectorProcessor::start() {
    if (running_) {
        return true; // Already running
    }
    
    // Initialize if not already done
    if (!initialized_) {
        if (!initialize()) {
            return false;
        }
    } else if (protocol_ == "http_shm" || protocol_ == "grpc_shm") {
        // For already initialized processors, we need to make sure shared memory
        // is properly set up when restarting after a stop
        
        // Check if we need to reinitialize shared memory
        if (!shm_ || !shm_->isValid()) {
            std::cout << "Processor already initialized but shared memory needs setup. Reinitializing shared memory for " << getId() << std::endl;
            
            // Perform only the shared memory initialization portion of initialize()
            try {
                // Force a re-check of server availability
                bool wasAvailable = serverAvailable_;
                bool serverAvailable = checkServerHealth();
                
                // Log server availability changes
                if (!wasAvailable && serverAvailable) {
                    std::cout << "Server became available since last check" << std::endl;
                } else if (wasAvailable && !serverAvailable) {
                    std::cout << "Server became unavailable since last check" << std::endl;
                }
                
                if (serverAvailable && camera_ != nullptr) {
                    // Make sure we start clean
                    cleanupSharedMemory();
                    
                    // Reset the shared memory object
                    shm_.reset(new utils::TritonSharedMemory());
                    
                    // Allocate memory with same size as in initialize()
                    const int maxWidth = 1280;
                    const int maxHeight = 1280;
                    const int channels = 3;
                    
                    // Create a dummy image for initialization
                    cv::Mat dummyImage(maxHeight, maxWidth, CV_8UC3, cv::Scalar(0, 0, 0));
                    
                    std::string shmName = "tapi_persistent_" + getId();
                    std::cout << "Re-initializing persistent shared memory region '" << shmName << "'" << std::endl;
                    
                    // Create with skipRegistration=true to avoid double registration
                    std::string result = shm_->createImageSharedMemory(dummyImage, shmName, true);
                    if (result.empty()) {
                        std::cerr << "Failed to initialize shared memory on restart, will use HTTP for data transfer" << std::endl;
                        // Switch to non-shared memory protocol
                        if (protocol_ == "http_shm") protocol_ = "http";
                        if (protocol_ == "grpc_shm") protocol_ = "grpc";
                    } else {
                        std::cout << "Successfully initialized shared memory region for inference on restart" << std::endl;
                        
                        // Now explicitly register with Triton server
                        if (!shm_->registerWithTritonServer()) {
                            std::cerr << "Failed to register shared memory with Triton on restart" << std::endl;
                            // Switch to non-shared memory protocol
                            if (protocol_ == "http_shm") protocol_ = "http";
                            if (protocol_ == "grpc_shm") protocol_ = "grpc";
                            shm_->cleanup();
                        } else {
                            std::cout << "Successfully registered shared memory with Triton server on restart" << std::endl;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error reinitializing shared memory: " << e.what() << std::endl;
                // Switch to non-shared memory protocol
                if (protocol_ == "http_shm") protocol_ = "http";
                if (protocol_ == "grpc_shm") protocol_ = "grpc";
            }
        }
    }
    
    // Initialize HTTP client if using HTTP protocol
    if (protocol_ == "http" && !http_client_) {
        std::cout << "Initializing HTTP client on start for " << getId() << std::endl;
        try {
            triton::client::Error err = triton::client::InferenceServerHttpClient::Create(
                &http_client_, serverUrl_, true);
            if (!err.IsOk()) {
                std::cerr << "Failed to create HTTP client: " << err.Message() << std::endl;
                // Continue anyway, we'll fall back to local CURL implementation
            } else {
                std::cout << "Successfully created HTTP client" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception creating HTTP client: " << e.what() << std::endl;
            // Continue anyway, we'll fall back to local CURL implementation
        }
    }

    if (protocol_ == "grpc" && !grpc_client_) {
        std::cout << "Initializing gRPC client on start for " << getId() << std::endl;
        try {
            // Extract host and port from serverUrl for gRPC
            std::string grpcUrl = serverUrl_;
            // Remove http:// or https:// prefix if present
            if (grpcUrl.find("http://") == 0) {
                grpcUrl = grpcUrl.substr(7);
            } else if (grpcUrl.find("https://") == 0) {
                grpcUrl = grpcUrl.substr(8);
            }
            // Remove trailing slash if present
            if (!grpcUrl.empty() && grpcUrl.back() == '/') {
                grpcUrl.pop_back();
            }
            
            // Check if we need to adjust the port for gRPC
            // Triton typically uses 8000 for HTTP and 8001 for gRPC
            size_t colonPos = grpcUrl.rfind(':');
            if (colonPos != std::string::npos) {
                std::string host = grpcUrl.substr(0, colonPos);
                std::string port = grpcUrl.substr(colonPos + 1);
                if (port == "8000") {
                    // Switch to gRPC port
                    grpcUrl = host + ":8001";
                    std::cout << "Switching from HTTP port 8000 to gRPC port 8001" << std::endl;
                }
            }
            
            std::cout << "Creating gRPC client with URL: " << grpcUrl << std::endl;
            triton::client::Error err = triton::client::InferenceServerGrpcClient::Create(
                &grpc_client_, grpcUrl, false);  // false for verbose logging
            if (!err.IsOk()) {
                std::cerr << "Failed to create gRPC client: " << err.Message() << std::endl;
                // Continue anyway, we'll fall back to local CURL implementation
            } else {
                std::cout << "Successfully created gRPC client" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception creating gRPC client: " << e.what() << std::endl;
            // Continue anyway, we'll fall back to local CURL implementation
        }
    }
    
    // If server is marked as unavailable, try again to check availability before giving up
    if (!serverAvailable_) {
        std::cout << "Server was previously marked as unavailable, rechecking availability..." << std::endl;
        serverAvailable_ = checkServerHealth();
        if (serverAvailable_) {
            std::cout << "Server became available and is now ready for use" << std::endl;
            lastError_ = ""; // Clear previous error
        } else {
            std::cout << "Server is still unavailable" << std::endl;
        }
    }
    
    // If the server is not available, we can't start the processor
    if (!serverAvailable_) {
        lastError_ = "Cannot start processor: Triton server is not available at " + serverUrl_;
        std::cerr << lastError_ << std::endl;
        return false;
    }
    
    running_ = true;
    std::cout << "Object Detector processor started: " << getId() << std::endl;
    return true;
}

// Helper method to check if server is available
bool ObjectDetectorProcessor::checkServerHealth() {
    CURL* healthCheckCurl = curl_easy_init();
    if (!healthCheckCurl) {
        std::cerr << "Failed to initialize CURL for health check" << std::endl;
        return false;
    }
    
    bool available = false;
    
    try {
        // Use the standard Triton health endpoint
        std::string url = serverUrl_;
        if (url.back() == '/') {
            url += "v2/health/ready";
        } else {
            url += "/v2/health/ready";
        }
        
        std::cout << "Checking Triton server health at: " << url << std::endl;
        
        // Set request options with shorter timeout
        curl_easy_setopt(healthCheckCurl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(healthCheckCurl, CURLOPT_TIMEOUT, 2L); // Use a very short 2 second timeout for health checks
        curl_easy_setopt(healthCheckCurl, CURLOPT_NOBODY, 0L);  // Full request, not just HEAD
        curl_easy_setopt(healthCheckCurl, CURLOPT_HTTPGET, 1L); // GET request
        
        // Add connection options for robustness
        curl_easy_setopt(healthCheckCurl, CURLOPT_CONNECTTIMEOUT, 1L); // 1 second connect timeout
        curl_easy_setopt(healthCheckCurl, CURLOPT_TCP_KEEPALIVE, 1L); // Enable TCP keepalive
        
        // Capture response data
        std::string response;
        curl_easy_setopt(healthCheckCurl, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(healthCheckCurl, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        CURLcode res = curl_easy_perform(healthCheckCurl);
        
        // Check if request was successful
        if (res == CURLE_OK) {
            // Get HTTP response code
            long httpCode = 0;
            curl_easy_getinfo(healthCheckCurl, CURLINFO_RESPONSE_CODE, &httpCode);
            
            if (httpCode == 200) {
                available = true;
            } else {
                std::cerr << "Triton server health check failed, HTTP code: " << httpCode << std::endl;
            }
        } else {
            std::cerr << "Health check failed: " << curl_easy_strerror(res) << std::endl;
            
            // Try alternative health endpoint
            url = serverUrl_;
            if (url.back() == '/') {
                url += "api/health/ready";
            } else {
                url += "/api/health/ready";
            }
            
            std::cout << "Trying alternative health endpoint: " << url << std::endl;
            curl_easy_setopt(healthCheckCurl, CURLOPT_URL, url.c_str());
            res = curl_easy_perform(healthCheckCurl);
            
            if (res == CURLE_OK) {
                long httpCode = 0;
                curl_easy_getinfo(healthCheckCurl, CURLINFO_RESPONSE_CODE, &httpCode);
                
                if (httpCode == 200) {
                    available = true;
                } else {
                    std::cerr << "Alternative health check failed, HTTP code: " << httpCode << std::endl;
                }
            } else {
                std::cerr << "Alternative health check failed: " << curl_easy_strerror(res) << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error checking Triton server health: " << e.what() << std::endl;
    }
    
    // Clean up the temporary CURL handle
    curl_easy_cleanup(healthCheckCurl);
    
    // Update the server availability flag and check time
    serverAvailable_ = available;
    lastServerCheckTime_ = std::chrono::steady_clock::now();
    
    return available;
}

bool ObjectDetectorProcessor::stop() {
    if (!running_) {
        return true; // Already stopped
    }
    
    running_ = false;
    
    // Clean up HTTP client when stopping
    if (protocol_ == "http" && http_client_) {
        std::cout << "Stopping processor: cleaning up HTTP client for " << getId() << std::endl;
        http_client_.reset();
    }

    if (protocol_ == "grpc" && grpc_client_) {
        std::cout << "Stopping processor: cleaning up gRPC client for " << getId() << std::endl;
        grpc_client_.reset();
    }
    
    // Clean up shared memory resources when stopping the processor
    // This is critical to prevent issues when restarting
    if ((protocol_ == "http_shm" || protocol_ == "grpc_shm") && shm_ && shm_->isValid()) {
        std::cout << "Stopping processor: cleaning up shared memory for " << getId() << std::endl;
        cleanupSharedMemory();
    }
    
    // Don't reset initialization flag, so shared memory will be reinitialized on restart
    // initialized_ = false;  <-- This line was causing the issue
    
    // Only log stop message if we were actually running
    if (running_) {
        std::cout << "Object Detector processor stopped: " << getId() << std::endl;
    }
    return true;
}

bool ObjectDetectorProcessor::updateConfig(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Always use GlobalConfig as the source of truth for server URL
    serverUrl_ = GlobalConfig::getInstance().getAiServerUrl();
    std::cout << "UPDATE CONFIG: Using AI server URL from GlobalConfig: " << serverUrl_ << std::endl;
    
    // Try to update the improved configuration
    auto configResult = InferenceConfig::fromJson(config);
    if (configResult.isSuccess()) {
        config_ = configResult.getValue();
        std::cout << "Successfully updated improved configuration system" << std::endl;
    }
    
    if (config.contains("model_id") && !config["model_id"].is_null()) {
        if (config["model_id"].is_string()) {
            modelId_ = config["model_id"].get<std::string>();
        }
    }
    
    if (config.contains("confidence_threshold") && !config["confidence_threshold"].is_null()) {
        if (config["confidence_threshold"].is_number()) {
            confidenceThreshold_ = config["confidence_threshold"].get<float>();
            // Clamp to valid range
            confidenceThreshold_ = std::max(0.0f, std::min(1.0f, confidenceThreshold_));
        }
    }
    
    if (config.contains("classes") && !config["classes"].is_null()) {
        if (config["classes"].is_array()) {
            classes_.clear();
            for (const auto& item : config["classes"]) {
                if (item.is_string()) {
                    classes_.push_back(item.get<std::string>());
                }
            }
        }
    }
    
    if (config.contains("draw_bounding_boxes") && !config["draw_bounding_boxes"].is_null()) {
        if (config["draw_bounding_boxes"].is_boolean()) {
            drawBoundingBoxes_ = config["draw_bounding_boxes"].get<bool>();
        }
    }
    
    // First check for the new protocol property
    if (config.contains("protocol") && !config["protocol"].is_null()) {
        if (config["protocol"].is_string()) {
            std::string new_protocol = config["protocol"].get<std::string>();
            // Check if protocol has changed
            if (new_protocol != protocol_) {
                // Clean up existing client if changing protocols
                if (http_client_) {
                    http_client_.reset();
                }
                if (grpc_client_) {
                    grpc_client_.reset();
                }
                protocol_ = new_protocol;
            }
            std::cout << "UPDATE CONFIG: Protocol setting changed to " << protocol_ << std::endl;
        }
    } 
    // Then check for the legacy use_shared_memory property
    else if (config.contains("use_shared_memory") && !config["use_shared_memory"].is_null()) {
        if (config["use_shared_memory"].is_boolean()) {
            bool use_shared_memory = config["use_shared_memory"].get<bool>();
            std::string new_protocol = use_shared_memory ? "http_shm" : "http";
            
            // Check if this would change the protocol
            if (new_protocol != protocol_) {
                // Clean up existing client if changing protocols
                if (http_client_) {
                    http_client_.reset();
                }
                if (grpc_client_) {
                    grpc_client_.reset();
                }
                protocol_ = new_protocol;
            }
            std::cout << "UPDATE CONFIG: Protocol changed to " << protocol_ 
                      << " based on legacy shared memory setting: " 
                      << (use_shared_memory ? "true" : "false") << std::endl;
        }
    } 
    // Otherwise check global setting for backward compatibility
    else {
        bool use_shared_memory = GlobalConfig::getInstance().getUseSharedMemory();
        std::string new_protocol = use_shared_memory ? "http_shm" : "http";
        
        // Check if this would change the protocol
        if (new_protocol != protocol_) {
            // Clean up existing client if changing protocols
            if (http_client_) {
                http_client_.reset();
            }
            if (grpc_client_) {
                grpc_client_.reset();
            }
            protocol_ = new_protocol;
        }
        std::cout << "UPDATE CONFIG: Using global shared memory setting for protocol: " 
                  << protocol_ << std::endl;
    }
    
    if (config.contains("label_font_scale") && !config["label_font_scale"].is_null()) {
        if (config["label_font_scale"].is_number()) {
            labelFontScale_ = config["label_font_scale"].get<float>();
            // Clamp to reasonable range
            labelFontScale_ = std::max(0.1f, std::min(2.0f, labelFontScale_));
        }
    }
    
    if (config.contains("verbose_logging") && !config["verbose_logging"].is_null()) {
        if (config["verbose_logging"].is_boolean()) {
            verboseLogging_ = config["verbose_logging"].get<bool>();
        }
    }
    
    // Store the raw config for later retrieval (keep legacy behavior)
    legacyConfig_ = config;
    
    return true;
}

nlohmann::json ObjectDetectorProcessor::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Return improved config if available, otherwise legacy config
    if (!config_.getModelConfig().id.empty()) {
        return config_.toJson();
    } else {
        return legacyConfig_;
    }
}

nlohmann::json ObjectDetectorProcessor::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto status = Component::getStatus();
    
    // Override the generic "processor" type with the specific processor type
    status["type"] = "object_detection";
    
    status["model_id"] = modelId_;
    status["server_url"] = serverUrl_;
    status["protocol"] = protocol_;
    status["confidence_threshold"] = confidenceThreshold_;
    status["processed_frames"] = processedFrames_;
    status["detection_count"] = detectionCount_;
    status["label_font_scale"] = labelFontScale_;
    status["server_available"] = serverAvailable_;
    status["verbose_logging"] = verboseLogging_;
    
    // Add selected classes
    nlohmann::json classesJson = nlohmann::json::array();
    for (const auto& cls : classes_) {
        classesJson.push_back(cls);
    }
    status["classes"] = classesJson;
    
    if (!lastError_.empty()) {
        status["last_error"] = lastError_;
    }
    
    return status;
}

std::pair<cv::Mat, std::vector<ObjectDetectorProcessor::Detection>> ObjectDetectorProcessor::processFrame(const cv::Mat& frame) {
    LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Starting for processor " + getId());
    
    std::vector<Detection> detections;
    
    try {
        // Skip processing if stopped or if frame is empty
        if (!initialized_ || frame.empty()) {
            LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Skipping - not initialized or empty frame for processor " + getId());
            return {frame, {}};
        }
        
        LOG_DEBUG("ObjectDetectorProcessor", "processFrame: About to clone frame for processor " + getId());
        
        // Make a copy of the frame for processing
        cv::Mat processedFrame = frame.clone();
        
        LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Frame cloned successfully for processor " + getId());
        LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Config model ID: '" + config_.getModelConfig().id + "' for processor " + getId());
        
        // Attempt to detect objects in the frame using improved method if configuration is available
        if (config_.getModelConfig().id.empty()) {
            LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Using legacy detection method for processor " + getId());
            
            // Use legacy detection method
            detections = detectObjects(processedFrame);
            
            LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Legacy detection completed for processor " + getId());
        } else {
            LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Using improved detection method for processor " + getId());
            
            // Use improved detection method
            auto detectResult = detectObjectsImproved(processedFrame);
            if (detectResult.isSuccess()) {
                LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Improved detection succeeded for processor " + getId());
                detections = detectResult.moveValue();
            } else {
                LOG_ERROR("ObjectDetectorProcessor", "processFrame: Improved detection failed for processor " + getId() + ": " + detectResult.getError() + ", falling back to legacy method");
                detections = detectObjects(processedFrame);
                LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Legacy fallback completed for processor " + getId());
            }
        }
        
        LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Detection completed, found " + std::to_string(detections.size()) + " objects for processor " + getId());
        
        // Draw bounding boxes for detections if enabled
        if (drawBoundingBoxes_) {
            LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Drawing bounding boxes for processor " + getId());
            drawDetections(processedFrame, detections);
            LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Bounding boxes drawn for processor " + getId());
        }
        
        // Increment the processed frames counter
        processedFrames_++;
        detectionCount_ += detections.size();
        
        LOG_DEBUG("ObjectDetectorProcessor", "processFrame: Successfully completed for processor " + getId());
        
        return {processedFrame, detections};
        
    } catch (const std::exception& e) {
        LOG_ERROR("ObjectDetectorProcessor", "processFrame: Exception in processor " + getId() + ": " + e.what());
        lastError_ = std::string("Processing error: ") + e.what();
        
        // Mark server as unavailable if this was related to server communication
        if (std::string(e.what()).find("connect to server") != std::string::npos || 
            std::string(e.what()).find("CURL request") != std::string::npos ||
            std::string(e.what()).find("timeout") != std::string::npos) {
            serverAvailable_ = false;
            LOG_ERROR("ObjectDetectorProcessor", "Marking AI server as unavailable due to connection error");
        }
        
        return {frame, {}};
    } catch (...) {
        // Catch any other unexpected errors to prevent crashes
        lastError_ = "Unknown processing error occurred";
        LOG_ERROR("ObjectDetectorProcessor", "processFrame: Unknown error in processor " + getId());
        return {frame, {}};
    }
}

std::vector<ObjectDetectorProcessor::Detection> ObjectDetectorProcessor::detectObjects(const cv::Mat& image) {
    // Start total detection time measurement
    auto detectStartTime = std::chrono::high_resolution_clock::now();
    
    std::vector<Detection> detections;

    // If server is known to be unavailable, we should periodically try to reconnect
    if (!serverAvailable_) {
        // Check if enough time has passed since the last server check (10 seconds)
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedSinceLastCheck = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - lastServerCheckTime_).count();
            
        if (elapsedSinceLastCheck > 10) {
            // Try to reconnect to the server
            std::cout << "Server was previously unavailable. Attempting to reconnect after " 
                      << elapsedSinceLastCheck << " seconds..." << std::endl;
            if (checkServerAvailability()) {
                std::cout << "Server is now available! Continuing with detection." << std::endl;
                lastError_ = ""; // Clear previous error
            } else {
                std::cout << "Server is still unavailable. Will retry later." << std::endl;
                return detections; // Still unavailable, return empty detections
            }
        } else {
            // Not enough time has passed, return empty detections
            return detections;
        }
    }

    try {
        // Get model configuration based on model ID 
        const std::string model_type = "yolo"; // Default model type is YOLO
        std::string inputName = "images";      // Default input name for many YOLO models
        std::string outputName = "output";     // Default output name
        std::string inputFormat = "NCHW";      // Channel-first format is common for ONNX models
        int inputSize = 640;                   // Common input size for YOLO models
        
        if (modelId_ == "yolov7") {
            // Standard YOLOv7 output tensor name
            outputName = "num_dets,det_boxes,det_scores,det_classes";     // TensorRT model outputs
            inputSize = 640;            // YOLOv7 input size
        } else if (modelId_ == "yolov7_qat") {
            // Custom TensorRT model with specific outputs
            outputName = "num_dets,det_boxes,det_scores,det_classes";
            inputSize = 640;            // YOLOv7 input size
        }
        
        // Resize the image to match expected model dimensions
        cv::Mat resizedImage;
        cv::resize(image, resizedImage, cv::Size(inputSize, inputSize), 0, 0, cv::INTER_LINEAR);
        
        // Store original dimensions for scaling back the detections
        float orig_width = static_cast<float>(image.cols);
        float orig_height = static_cast<float>(image.rows);
        
        // Measure data preparation time
        auto dataPrepStart = std::chrono::high_resolution_clock::now();
        
        // Initialize HTTP client if needed
        if (!http_client_) {
            initCurl();
            if (protocol_ == "http" || protocol_ == "http_shm") {
                triton::client::Error err = triton::client::InferenceServerHttpClient::Create(
                    &http_client_, serverUrl_, verboseLogging_);
                if (!err.IsOk()) {
                    throw std::runtime_error("Failed to create HTTP client: " + err.Message());
                }
            }
        }

        if (!grpc_client_) {
            if (protocol_ == "grpc" || protocol_ == "grpc_shm") {
                // Extract host and port from serverUrl for gRPC
                std::string grpcUrl = serverUrl_;
                // Remove http:// or https:// prefix if present
                if (grpcUrl.find("http://") == 0) {
                    grpcUrl = grpcUrl.substr(7);
                } else if (grpcUrl.find("https://") == 0) {
                    grpcUrl = grpcUrl.substr(8);
                }
                // Remove trailing slash if present
                if (!grpcUrl.empty() && grpcUrl.back() == '/') {
                    grpcUrl.pop_back();
                }
                
                // Check if we need to adjust the port for gRPC
                // Triton typically uses 8000 for HTTP and 8001 for gRPC
                size_t colonPos = grpcUrl.rfind(':');
                if (colonPos != std::string::npos) {
                    std::string host = grpcUrl.substr(0, colonPos);
                    std::string port = grpcUrl.substr(colonPos + 1);
                    if (port == "8000") {
                        // Switch to gRPC port
                        grpcUrl = host + ":8001";
                        std::cout << "Switching from HTTP port 8000 to gRPC port 8001" << std::endl;
                    }
                }
                
                std::cout << "Creating gRPC client with URL: " << grpcUrl << std::endl;
                triton::client::Error err = triton::client::InferenceServerGrpcClient::Create(
                    &grpc_client_, grpcUrl, false);  // false for verbose logging
                if (!err.IsOk()) {
                    throw std::runtime_error("Failed to create gRPC client: " + err.Message());
                }
            }
        }
        
        // Convert the image to float and normalize to [0-1]
        cv::Mat floatImage;
        resizedImage.convertTo(floatImage, CV_32FC3, 1.0f/255.0f);
        
        // Create inference options
        triton::client::InferOptions options(modelId_);
        
        // Prepare inputs
        std::vector<triton::client::InferInput*> inputs;
        std::vector<const triton::client::InferRequestedOutput*> outputs;
        
        // Create input
        triton::client::InferInput* input;
        std::vector<int64_t> inputShape;
        
        if (inputFormat == "NCHW") {
            // [batch, channels, height, width]
            inputShape = {1, static_cast<int64_t>(floatImage.channels()), 
                          static_cast<int64_t>(floatImage.rows), 
                          static_cast<int64_t>(floatImage.cols)};
        } else {
            // [batch, height, width, channels]
            inputShape = {1, static_cast<int64_t>(floatImage.rows), 
                          static_cast<int64_t>(floatImage.cols), 
                          static_cast<int64_t>(floatImage.channels())};
        }
        
        triton::client::Error err = triton::client::InferInput::Create(
            &input, inputName, inputShape, "FP32");
        if (!err.IsOk()) {
            throw std::runtime_error("Error creating input: " + err.Message());
        }
        
        // Add input to the list
        inputs.push_back(input);
        
        // Check if using shared memory or direct data transfer
        if ((protocol_ == "http_shm" || protocol_ == "grpc_shm") && shm_->isValid()) {
            // Get shared memory info
            auto [name, ptr, size] = shm_->getSharedMemoryInfo();
            
            // Update data in shared memory
            float* sharedMemPtr = static_cast<float*>(ptr);
            
            // Copy data to shared memory in the correct format
            if (inputFormat == "NCHW") {
                // NCHW format (common in ONNX models)
                const int height = floatImage.rows;
                const int width = floatImage.cols;
                const int channels = floatImage.channels();
                
                // Rearrange from HWC to CHW
                for (int c = 0; c < channels; ++c) {
                    size_t offset = c * width * height;
                    for (int h = 0; h < height; ++h) {
                        for (int w = 0; w < width; ++w) {
                            const cv::Vec3f& pixel = floatImage.at<cv::Vec3f>(h, w);
                            sharedMemPtr[offset + h * width + w] = pixel[c];
                        }
                    }
                }
            } else {
                // NHWC format
                size_t bytesSize = floatImage.total() * floatImage.channels() * sizeof(float);
                
                if (floatImage.isContinuous()) {
                    std::memcpy(sharedMemPtr, floatImage.data, bytesSize);
                } else {
                    const int height = floatImage.rows;
                    const int width = floatImage.cols;
                    
                    size_t idx = 0;
                    for (int h = 0; h < height; ++h) {
                        for (int w = 0; w < width; ++w) {
                            const cv::Vec3f& pixel = floatImage.at<cv::Vec3f>(h, w);
                            sharedMemPtr[idx++] = pixel[0];  // B
                            sharedMemPtr[idx++] = pixel[1];  // G
                            sharedMemPtr[idx++] = pixel[2];  // R
                        }
                    }
                }
            }
            
            // Set shared memory for input
            size_t dataSize = floatImage.total() * floatImage.channels() * sizeof(float);
            err = input->SetSharedMemory(name, dataSize, 0);
            if (!err.IsOk()) {
                throw std::runtime_error("Error setting shared memory for input: " + err.Message());
            }
        } else {
            // Direct data transfer
            // Create a shared_ptr to ensure the data remains valid throughout the inference
            auto inputData = std::make_shared<std::vector<uint8_t>>();
            
            // Properly format the data
            if (inputFormat == "NCHW") {
                // Convert HWC to CHW format
                const int height = floatImage.rows;
                const int width = floatImage.cols;
                const int channels = floatImage.channels();
                
                // Pre-allocate memory for the flattened CHW data
                const size_t dataSizeBytes = height * width * channels * sizeof(float);
                inputData->resize(dataSizeBytes);
                float* dataPtr = reinterpret_cast<float*>(inputData->data());
                
                // Copy data in CHW format
                for (int c = 0; c < channels; ++c) {
                    size_t chOffset = c * height * width;
                    for (int h = 0; h < height; ++h) {
                        for (int w = 0; w < width; ++w) {
                            const cv::Vec3f& pixel = floatImage.at<cv::Vec3f>(h, w);
                            dataPtr[chOffset + h * width + w] = pixel[c];
                        }
                    }
                }
            } else {
                // NHWC format (just flatten the image)
                const int height = floatImage.rows;
                const int width = floatImage.cols;
                const int channels = floatImage.channels();
                
                // Pre-allocate memory for the flattened HWC data
                const size_t dataSizeBytes = height * width * channels * sizeof(float);
                inputData->resize(dataSizeBytes);
                float* dataPtr = reinterpret_cast<float*>(inputData->data());
                
                // Copy data in HWC format
                size_t idx = 0;
                for (int h = 0; h < height; ++h) {
                    for (int w = 0; w < width; ++w) {
                        const cv::Vec3f& pixel = floatImage.at<cv::Vec3f>(h, w);
                        for (int c = 0; c < channels; ++c) {
                            dataPtr[idx++] = pixel[c];
                        }
                    }
                }
            }
            
            // Add the raw input data
            err = input->AppendRaw(*inputData);
            if (!err.IsOk()) {
                throw std::runtime_error("Error adding input data: " + err.Message());
            }
            
            // Store the shared_ptr to keep the data alive during inference
            // We'll store it in a member variable that gets cleared after inference
            inputDataBuffer_ = inputData;
        }
        
        // Setup outputs
        if (outputName.find(",") != std::string::npos) {
            // Multiple output names separated by commas
            std::stringstream ss(outputName);
            std::string item;
            while (std::getline(ss, item, ',')) {
                triton::client::InferRequestedOutput* output;
                err = triton::client::InferRequestedOutput::Create(&output, item);
                if (!err.IsOk()) {
                    throw std::runtime_error("Error creating output: " + err.Message());
                }
                outputs.push_back(output);
            }
        } else {
            // Single output
            triton::client::InferRequestedOutput* output;
            err = triton::client::InferRequestedOutput::Create(&output, outputName);
            if (!err.IsOk()) {
                throw std::runtime_error("Error creating output: " + err.Message());
            }
            outputs.push_back(output);
        }
        
        // End of data preparation time measurement
        auto dataPrepEnd = std::chrono::high_resolution_clock::now();
        
        // Perform inference
        auto inferStart = std::chrono::high_resolution_clock::now();
        
        triton::client::InferResult* result = nullptr;
        bool success = false;
        
        // Call Infer (debug logging only on first few frames)
        static int debugLogCount = 0;
        if (debugLogCount < 3) {
            std::cout << "[DEBUG] Protocol: " << protocol_ << ", HTTP client: " << (http_client_ ? "available" : "null") 
                      << ", gRPC client: " << (grpc_client_ ? "available" : "null") << std::endl;
            debugLogCount++;
        }
        
        if (protocol_ == "http" || protocol_ == "http_shm") {
            if (debugLogCount < 3) {
                std::cout << "[DEBUG] Using HTTP client for inference" << std::endl;
            }
            err = http_client_->Infer(&result, options, inputs, outputs);
        } else if (protocol_ == "grpc" || protocol_ == "grpc_shm") {
            // Use the actual gRPC client for inference
            if (grpc_client_) {
                if (debugLogCount < 3) {
                    std::cout << "[DEBUG] Using gRPC client for inference" << std::endl;
                }
                err = grpc_client_->Infer(&result, options, inputs, outputs);
            } else {
                // If gRPC client is not available, fall back to HTTP
                std::cerr << "gRPC client not available, falling back to HTTP" << std::endl;
                if (!http_client_) {
                    triton::client::Error httpErr = triton::client::InferenceServerHttpClient::Create(
                        &http_client_, serverUrl_, verboseLogging_);
                    if (!httpErr.IsOk()) {
                        err = httpErr;
                    } else {
                        err = http_client_->Infer(&result, options, inputs, outputs);
                    }
                } else {
                    err = http_client_->Infer(&result, options, inputs, outputs);
                }
            }
        }
        
        if (!err.IsOk()) {
            std::cerr << "Inference failed: " << err.Message() << std::endl;
            // Mark server as unavailable to avoid repeated failures
            serverAvailable_ = false;
            lastServerCheckTime_ = std::chrono::steady_clock::now();
        } else {
            success = true;
        }
        
        auto inferEnd = std::chrono::high_resolution_clock::now();
        
        // Calculate time measurements
        std::chrono::duration<double, std::milli> dataPrepDuration = dataPrepEnd - dataPrepStart;
        std::chrono::duration<double, std::milli> inferDuration = inferEnd - inferStart;
        std::chrono::duration<double, std::milli> totalDuration = inferEnd - dataPrepStart;
        
        // Log timing information based on verbosity settings
        static int timingLogCount = 0;
        bool shouldLogTiming = verboseLogging_ || (timingLogCount % 10 == 0);
        if (shouldLogTiming) {
            std::cout << "[Inference Latency] Data preparation: " << dataPrepDuration.count() << " ms" << std::endl;
            std::cout << "[Inference Latency] Inference (incl. " 
                      << ((protocol_ == "grpc" || protocol_ == "grpc_shm") ? "gRPC" : "HTTP") 
                      << "): " << inferDuration.count() << " ms" << std::endl;
            std::cout << "[Inference Latency] Total processing: " << totalDuration.count() << " ms" << std::endl;
        }
        timingLogCount++;
        
        // If inference was successful, process the results
        if (success) {
            // Check if this is the yolov7/yolov7_qat format with specific named outputs
            if ((modelId_ == "yolov7" || modelId_ == "yolov7_qat") && 
                outputs.size() == 4) {
                
                // Use the user-configured confidence threshold
                float actualConfidenceThreshold = confidenceThreshold_;
                float iouThreshold = 0.45f;
                
                std::vector<cv::Rect> allBoxes;
                std::vector<float> allConfidences;
                std::vector<int> allClassIds;
                
                // Get data for each output tensor
                const uint8_t* num_dets_buf = nullptr;
                const uint8_t* boxes_buf = nullptr;
                const uint8_t* scores_buf = nullptr;
                const uint8_t* classes_buf = nullptr;
                
                size_t num_dets_byte_size = 0;
                size_t boxes_byte_size = 0;
                size_t scores_byte_size = 0;
                size_t classes_byte_size = 0;
                
                // Extract data from each tensor
                err = result->RawData("num_dets", &num_dets_buf, &num_dets_byte_size);
                if (!err.IsOk()) {
                    std::cerr << "Error getting num_dets data: " << err.Message() << std::endl;
                }
                
                err = result->RawData("det_boxes", &boxes_buf, &boxes_byte_size);
                if (!err.IsOk()) {
                    std::cerr << "Error getting det_boxes data: " << err.Message() << std::endl;
                }
                
                err = result->RawData("det_scores", &scores_buf, &scores_byte_size);
                if (!err.IsOk()) {
                    std::cerr << "Error getting det_scores data: " << err.Message() << std::endl;
                }
                
                err = result->RawData("det_classes", &classes_buf, &classes_byte_size);
                if (!err.IsOk()) {
                    std::cerr << "Error getting det_classes data: " << err.Message() << std::endl;
                }
                
                if (num_dets_buf && boxes_buf && scores_buf && classes_buf) {
                    // Get the number of detections
                    int numDetections = reinterpret_cast<const int*>(num_dets_buf)[0];
                    
                    // Get pointers to the float arrays
                    const float* boxes = reinterpret_cast<const float*>(boxes_buf);
                    const float* scores = reinterpret_cast<const float*>(scores_buf);
                    const int* classes = reinterpret_cast<const int*>(classes_buf);
                    
                    // Scale factors to convert to original image dimensions
                    float scaleX = orig_width / inputSize;
                    float scaleY = orig_height / inputSize;
                    
                    // Process each detection
                    for (int i = 0; i < numDetections; ++i) {
                        float score = scores[i];
                        
                        // Filter by confidence threshold
                        if (score >= actualConfidenceThreshold) {
                            int classId = classes[i];
                            
                            // Extract bounding box coordinates (in x1,y1,x2,y2 format)
                            float x1 = boxes[i * 4 + 0];
                            float y1 = boxes[i * 4 + 1];
                            float x2 = boxes[i * 4 + 2];
                            float y2 = boxes[i * 4 + 3];
                            
                            // Scale to original image dimensions
                            x1 *= scaleX;
                            y1 *= scaleY;
                            x2 *= scaleX;
                            y2 *= scaleY;
                            
                            // Add to results if valid
                            if (x1 < x2 && y1 < y2) {
                                // Create rect with correct dimensions
                                int rx1 = static_cast<int>(std::round(x1));
                                int ry1 = static_cast<int>(std::round(y1));
                                int width = static_cast<int>(std::round(x2 - x1));
                                int height = static_cast<int>(std::round(y2 - y1));
                                
                                cv::Rect bbox(rx1, ry1, width, height);
                                allBoxes.push_back(bbox);
                                allConfidences.push_back(score);
                                allClassIds.push_back(classId);
                            }
                        }
                    }
                }
                
                // Apply NMS if we have detections
                if (!allBoxes.empty()) {
                    // Apply global class-agnostic NMS across all detections
                    std::vector<int> keepIndices;
                    cv::dnn::NMSBoxes(allBoxes, allConfidences, actualConfidenceThreshold,
                                    iouThreshold, keepIndices);
                    
                    // Add kept detections to final results
                    for (int keepIdx : keepIndices) {
                        Detection det;
                        det.bbox = allBoxes[keepIdx];
                        det.confidence = allConfidences[keepIdx];
                        det.className = getClassName(allClassIds[keepIdx]);
                        
                        // Only add the detection if it matches one of the selected classes
                        // or if no specific classes were selected (classes_ is empty)
                        if (classes_.empty() || 
                            std::find(classes_.begin(), classes_.end(), det.className) != classes_.end()) {
                            detections.push_back(det);
                        }
                    }
                }
            } else {
                // Process standard YOLO output format
                const uint8_t* output_buf = nullptr;
                size_t buf_size = 0;
                
                if (outputs.size() > 0) {
                    // Get the first output data
                    err = result->RawData(outputs[0]->Name(), &output_buf, &buf_size);
                    if (!err.IsOk()) {
                        std::cerr << "Error getting output data: " << err.Message() << std::endl;
                    } else {
                        // Get output shape
                        std::vector<int64_t> outputShape;
                        err = result->Shape(outputs[0]->Name(), &outputShape);
                        if (!err.IsOk()) {
                            std::cerr << "Error getting output shape: " << err.Message() << std::endl;
                        }
                        
                        // Get output datatype
                        std::string outputDatatype;
                        err = result->Datatype(outputs[0]->Name(), &outputDatatype);
                        if (!err.IsOk()) {
                            std::cerr << "Error getting output datatype: " << err.Message() << std::endl;
                        }
                        
                        // Process based on datatype
                        if (outputDatatype == "FP32") {
                            const float* output_data = reinterpret_cast<const float*>(output_buf);
                            
                            // Determine shape and parameters
                            size_t numBoxes = 0;
                            size_t boxDim = 0;
                            
                            if (outputShape.size() >= 2) {
                                // If it's [batch, numboxes, dims]
                                if (outputShape.size() == 3) {
                                    numBoxes = outputShape[1];
                                    boxDim = outputShape[2];
                                }
                                // If it's [numboxes, dims]
                                else if (outputShape.size() == 2) {
                                    numBoxes = outputShape[0];
                                    boxDim = outputShape[1];
                                }
                                // Handle case where it's [1, numboxes*dims]
                                else if (outputShape.size() == 2 && outputShape[0] == 1) {
                                    // Try to guess boxDim based on common YOLO formats
                                    if (buf_size % (85 * sizeof(float)) == 0) {
                                        boxDim = 85; // COCO has 80 classes + 5 box params
                                        numBoxes = buf_size / (boxDim * sizeof(float));
                                    } else if (buf_size % (7 * sizeof(float)) == 0) {
                                        boxDim = 7; // Some YOLO variants use [x,y,w,h,conf,class_id,score]
                                        numBoxes = buf_size / (boxDim * sizeof(float));
                                    } else {
                                        // Make a reasonable guess
                                        boxDim = 85;
                                        numBoxes = buf_size / (boxDim * sizeof(float));
                                    }
                                } 
                            } else if (buf_size > 0) {
                                // Make a guess based on the output length and common formats
                                if (buf_size % (85 * sizeof(float)) == 0) {
                                    boxDim = 85; // 80 classes + 5 box params for COCO models
                                    numBoxes = buf_size / (boxDim * sizeof(float));
                                } else if (buf_size % (7 * sizeof(float)) == 0) {
                                    boxDim = 7; // Some YOLO variants use [x,y,w,h,conf,class_id,score]
                                    numBoxes = buf_size / (boxDim * sizeof(float));
                                } else {
                                    // Last resort: just guess a reasonable boxDim
                                    boxDim = buf_size > (85 * sizeof(float)) ? 85 : 
                                            (buf_size > (7 * sizeof(float)) ? 7 : 6);
                                    numBoxes = buf_size / (boxDim * sizeof(float));
                                }
                            }
                            
                            // Use the user-configured confidence threshold
                            float actualConfidenceThreshold = confidenceThreshold_;
                            float iouThreshold = 0.45f;
                            int numClasses = boxDim - 5; // Box format is [x,y,w,h,obj_conf,class_probs...]
                            
                            std::vector<cv::Rect> allBoxes;
                            std::vector<float> allConfidences;
                            std::vector<int> allClassIds;
                            
                            // Process each box
                            for (size_t i = 0; i < numBoxes; ++i) {
                                size_t baseIdx = i * boxDim;
                                
                                // Ensure we have enough data for this box
                                if (baseIdx + 4 >= (buf_size / sizeof(float))) {
                                    continue;
                                }
                                
                                // Extract box data
                                float x = output_data[baseIdx + 0];
                                float y = output_data[baseIdx + 1];
                                float w = output_data[baseIdx + 2];
                                float h = output_data[baseIdx + 3];
                                float obj_conf = output_data[baseIdx + 4];
                                
                                // Skip low confidence detections
                                if (obj_conf < actualConfidenceThreshold) {
                                    continue;
                                }
                                
                                // Check for valid box dimensions
                                if (w <= 0 || h <= 0 || std::isnan(w) || std::isnan(h)) {
                                    continue;
                                }
                                
                                // Find class with highest probability
                                float maxClassProb = 0.0f;
                                int classId = 0;
                                
                                // Two different ways to interpret class predictions
                                if (baseIdx + 5 + numClasses <= (buf_size / sizeof(float))) {
                                    for (int cls = 0; cls < numClasses; ++cls) {
                                        float classProb = output_data[baseIdx + 5 + cls];
                                        if (classProb > maxClassProb) {
                                            maxClassProb = classProb;
                                            classId = cls;
                                        }
                                    }
                                } else {
                                    // Protect against indexing errors
                                    maxClassProb = 1.0;
                                }
                                
                                // Use either the class score or obj_conf based on model type
                                float confidence;
                                
                                // YOLOv8/YOLOv9 output direct class confidence
                                // YOLOv5/YOLOv7 require multiplying objectness with class confidence
                                if (modelId_.find("yolov8") != std::string::npos || 
                                    modelId_.find("yolov9") != std::string::npos) {
                                    confidence = maxClassProb; // Already includes objectness
                                } else {
                                    confidence = obj_conf * maxClassProb; // Traditional YOLO approach
                                }
                                
                                // Skip low confidence detections
                                if (confidence < actualConfidenceThreshold) {
                                    continue;
                                }
                                
                                // Convert center to corner format
                                float x1 = x - w/2;
                                float y1 = y - h/2;
                                float x2 = x + w/2;
                                float y2 = y + h/2;
                                
                                // Scale directly to original image dimensions
                                float scale_x = orig_width / inputSize;
                                float scale_y = orig_height / inputSize;
                                
                                x1 *= scale_x;
                                y1 *= scale_y;
                                x2 *= scale_x;
                                y2 *= scale_y;
                                
                                // Add valid detections
                                if (x2 > x1 && y2 > y1) {
                                    // Create rect with correct dimensions
                                    int rx1 = static_cast<int>(std::round(x1));
                                    int ry1 = static_cast<int>(std::round(y1));
                                    int width = static_cast<int>(std::round(x2 - x1));
                                    int height = static_cast<int>(std::round(y2 - y1));
                                    
                                    cv::Rect bbox(rx1, ry1, width, height);
                                    allBoxes.push_back(bbox);
                                    allConfidences.push_back(confidence);
                                    allClassIds.push_back(classId);
                                }
                            }
                            
                            // Apply NMS
                            if (!allBoxes.empty()) {
                                // Apply global class-agnostic NMS across all detections
                                std::vector<int> keepIndices;
                                cv::dnn::NMSBoxes(allBoxes, allConfidences, actualConfidenceThreshold,
                                                iouThreshold, keepIndices);
                                
                                // Add kept detections to final results
                                for (int keepIdx : keepIndices) {
                                    Detection det;
                                    det.bbox = allBoxes[keepIdx];
                                    det.confidence = allConfidences[keepIdx];
                                    det.className = getClassName(allClassIds[keepIdx]);
                                    
                                    // Only add the detection if it matches one of the selected classes
                                    // or if no specific classes were selected (classes_ is empty)
                                    if (classes_.empty() || 
                                        std::find(classes_.begin(), classes_.end(), det.className) != classes_.end()) {
                                        detections.push_back(det);
                                    }
                                }
                            }
                        } else {
                            std::cerr << "Unsupported output datatype: " << outputDatatype << std::endl;
                        }
                    }
                }
            }
        }
        
        // Clean up
        for (auto& input : inputs) {
            delete input;
        }
        for (auto& output : outputs) {
            delete output;
        }
        if (result) {
            delete result;
        }
        
        // Clear the input data buffer to free memory
        inputDataBuffer_.reset();
        
    } catch (const std::exception& e) {
        lastError_ = std::string("Detection error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        
        // Clear the input data buffer to free memory even on error
        inputDataBuffer_.reset();
        
        // Mark server as unavailable if this was a connection error
        if (std::string(e.what()).find("connect to server") != std::string::npos ||
            std::string(e.what()).find("Connection refused") != std::string::npos ||
            std::string(e.what()).find("Timeout was reached") != std::string::npos) {
            serverAvailable_ = false;
            lastServerCheckTime_ = std::chrono::steady_clock::now();
            std::cerr << "Marking AI server as unavailable due to connection error" << std::endl;
        }
    }
    
    // Calculate and log end-to-end detection time based on verbosity settings
    auto detectEndTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalDetectDuration = detectEndTime - detectStartTime;
    static int endToEndLogCount = 0;
    bool shouldLogEndToEnd = verboseLogging_ || (endToEndLogCount % 10 == 0);
    if (shouldLogEndToEnd) {
        std::cout << "[Inference Latency] TOTAL end-to-end detection: " << totalDetectDuration.count() 
                  << " ms (found " << detections.size() << " objects)" << std::endl;
    }
    endToEndLogCount++;
    
    return detections;
}

void ObjectDetectorProcessor::cleanupSharedMemory() {
    if (shm_) {
        // Check if there's an active shared memory region to clean up
        if (shm_->isValid()) {
            std::cout << "Cleaning up shared memory for ObjectDetector " << getId() << std::endl;
            
            // Make sure to explicitly unregister from Triton server before cleanup
            if (!shm_->unregisterFromTritonServer()) {
                std::cerr << "Warning: Failed to unregister shared memory from Triton server, continuing with cleanup" << std::endl;
            } else {
                std::cout << "Successfully unregistered shared memory from Triton server" << std::endl;
            }
            
            shm_->cleanup();
            std::cout << "Shared memory cleanup completed for " << getId() << std::endl;
        } else {
            std::cout << "No active shared memory to clean up for " << getId() << std::endl;
        }
    }
}

std::string ObjectDetectorProcessor::generateRandomKey(size_t length) {
    return utils::TritonSharedMemory::generateRandomString(length);
}

std::string ObjectDetectorProcessor::imageToBase64(const cv::Mat& image) {
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    return base64_encode(buffer.data(), buffer.size());
}

std::string ObjectDetectorProcessor::getClassName(int classId) {
    // Get the class name for a given class ID
    // This might need to be fetched from the model metadata
    // Standard COCO class names
    static const std::map<int, std::string> cocoClasses = {
        {0, "person"}, {1, "bicycle"}, {2, "car"}, {3, "motorcycle"}, {4, "airplane"}, {5, "bus"}, 
        {6, "train"}, {7, "truck"}, {8, "boat"}, {9, "traffic light"}, {10, "fire hydrant"}, 
        {11, "stop sign"}, {12, "parking meter"}, {13, "bench"}, {14, "bird"}, {15, "cat"}, 
        {16, "dog"}, {17, "horse"}, {18, "sheep"}, {19, "cow"}, {20, "elephant"}, {21, "bear"}, 
        {22, "zebra"}, {23, "giraffe"}, {24, "backpack"}, {25, "umbrella"}, {26, "handbag"}, 
        {27, "tie"}, {28, "suitcase"}, {29, "frisbee"}, {30, "skis"}, {31, "snowboard"}, 
        {32, "sports ball"}, {33, "kite"}, {34, "baseball bat"}, {35, "baseball glove"}, 
        {36, "skateboard"}, {37, "surfboard"}, {38, "tennis racket"}, {39, "bottle"}, 
        {40, "wine glass"}, {41, "cup"}, {42, "fork"}, {43, "knife"}, {44, "spoon"}, {45, "bowl"}, 
        {46, "banana"}, {47, "apple"}, {48, "sandwich"}, {49, "orange"}, {50, "broccoli"}, 
        {51, "carrot"}, {52, "hot dog"}, {53, "pizza"}, {54, "donut"}, {55, "cake"}, {56, "chair"}, 
        {57, "couch"}, {58, "potted plant"}, {59, "bed"}, {60, "dining table"}, {61, "toilet"}, 
        {62, "tv"}, {63, "laptop"}, {64, "mouse"}, {65, "remote"}, {66, "keyboard"}, {67, "cell phone"}, 
        {68, "microwave"}, {69, "oven"}, {70, "toaster"}, {71, "sink"}, {72, "refrigerator"}, 
        {73, "book"}, {74, "clock"}, {75, "vase"}, {76, "scissors"}, {77, "teddy bear"}, 
        {78, "hair drier"}, {79, "toothbrush"}
    };
    
    // Use standard COCO class names for all models for simplicity
    
    // Check in standard COCO classes
    auto it = cocoClasses.find(classId);
    if (it != cocoClasses.end()) {
        return it->second;
    }
    
    // If no matching class found, return generic class name
    return "class_" + std::to_string(classId);
}

std::vector<std::string> ObjectDetectorProcessor::getAvailableModels() {
    return getAvailableModels(serverUrl_);
}

std::vector<std::string> ObjectDetectorProcessor::getAvailableModels(const std::string& serverUrl) {
    std::vector<std::string> models;
    
    try {
        // Create a temporary CURL handle for this request
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        // Set up URL for Triton model repository index API
        std::string url = serverUrl;
        if (url.back() == '/') {
            url += "v2/repository/index";
        } else {
            url += "/v2/repository/index";
        }

        std::cout << "Getting available models from: " << url << std::endl;
        
        // Set request options
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5 seconds timeout
        curl_easy_setopt(curl, CURLOPT_POST, 1L); // POST request
        
        // Create headers
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Add empty JSON body (required by Triton API)
        std::string requestBody = "{}";
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
        
        // Capture response data
        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        CURLcode res = curl_easy_perform(curl);
        
        // Check if request was successful
        if (res != CURLE_OK) {
            // Try alternative endpoint
            std::string alt_url = serverUrl;
            if (alt_url.back() == '/') {
                alt_url += "v2/models";
            } else {
                alt_url += "/v2/models";
            }
            
            std::cout << "Trying alternative models endpoint: " << alt_url << std::endl;
            curl_easy_setopt(curl, CURLOPT_URL, alt_url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L); // Switch to GET for alternative endpoint
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL); // Clear headers
            res = curl_easy_perform(curl);
            
            if (res != CURLE_OK) {
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
            }
        }
        
        // Get HTTP response code
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        
        // Clean up CURL
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (httpCode != 200) {
            throw std::runtime_error("Server error: " + std::to_string(httpCode) + " " + response);
        }
        
        // Parse the response
        auto responseJson = nlohmann::json::parse(response);
        
        // Extract models from Triton repository/index response
        if (responseJson.is_array()) {
            for (const auto& model : responseJson) {
                if (model.contains("name") && model.contains("state")) {
                    // Only include models that are in READY state
                    std::string state = model["state"].get<std::string>();
                    if (state == "READY") {
                        models.push_back(model["name"].get<std::string>());
                    } else {
                        std::cout << "Skipping model '" << model["name"].get<std::string>() 
                                  << "' with state: " << state << std::endl;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error getting available models: " << e.what() << std::endl;
    }
    
    return models;
}

std::vector<std::string> ObjectDetectorProcessor::getModelClasses(const std::string& serverUrl, const std::string& modelId) {
    std::vector<std::string> classes;
    
    try {
        // Create a temporary CURL handle for this request
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        // Set up URL for Triton model metadata API
        std::string url = serverUrl;
        if (url.back() == '/') {
            url += "v2/models/" + modelId;
        } else {
            url += "/v2/models/" + modelId;
        }
        
        // Set request options
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5 seconds timeout
        
        // Capture response data
        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        CURLcode res = curl_easy_perform(curl);
        
        // Check if request was successful
        if (res != CURLE_OK) {
            curl_easy_cleanup(curl);
            throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
        }
        
        // Get HTTP response code
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        
        // Clean up CURL
        curl_easy_cleanup(curl);
        
        if (httpCode != 200) {
            throw std::runtime_error("Server error: " + std::to_string(httpCode) + " " + response);
        }
        
        // Parse the response
        auto responseJson = nlohmann::json::parse(response);
        
        // Extract classes from model metadata
        // This is a placeholder - the actual extraction will depend on how class information 
        // is stored in your Triton model configuration
        // For now, we'll just use placeholder class names
        classes = {"person", "car", "bicycle", "motorcycle", "bus", "truck"};
    } catch (const std::exception& e) {
        std::cerr << "Error getting model classes: " << e.what() << std::endl;
    }
    
    return classes;
}

nlohmann::json ObjectDetectorProcessor::getModelHealth(const std::string& serverUrl) {
    nlohmann::json health;
    
    try {
        // Create a temporary CURL handle for this request
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        // Set up URL for Triton health endpoint
        std::string url = serverUrl;
        if (url.back() == '/') {
            url += "v2/health/ready";
        } else {
            url += "/v2/health/ready";
        }
        
        // Set request options
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5 seconds timeout
        
        // Capture response data
        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        CURLcode res = curl_easy_perform(curl);
        
        // Check if request was successful
        if (res != CURLE_OK) {
            curl_easy_cleanup(curl);
            throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
        }
        
        // Get HTTP response code
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        
        // Clean up CURL
        curl_easy_cleanup(curl);
        
        // Construct health response
        health["status"] = (httpCode == 200) ? "ok" : "error";
        health["service"] = "Triton Inference Server";
        
        // Get additional model information if health check passes
        if (httpCode == 200) {
            // Get list of models
            std::vector<std::string> models = getAvailableModels(serverUrl);
            
            nlohmann::json modelsJson = nlohmann::json::array();
            for (const auto& model : models) {
                nlohmann::json modelJson;
                modelJson["id"] = model;
                modelJson["type"] = "object_detection";  // Assuming all models are for object detection
                modelJson["status"] = "loaded";
                
                // Get classes for this model
                std::vector<std::string> modelClasses = getModelClasses(serverUrl, model);
                nlohmann::json classesJson = nlohmann::json::array();
                for (const auto& cls : modelClasses) {
                    classesJson.push_back(cls);
                }
                modelJson["classes"] = classesJson;
                
                modelsJson.push_back(modelJson);
            }
            
            health["models"] = modelsJson;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error getting model health: " << e.what() << std::endl;
        health["status"] = "error";
        health["error"] = e.what();
    }
    
    return health;
}

void ObjectDetectorProcessor::initCurl() {
    // Clean up existing CURL handle if any
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr; // Set to nullptr to avoid double free
    }
    
    // Create a new CURL handle
    curl_ = curl_easy_init();
    if (!curl_) {
        throw std::runtime_error("Failed to initialize CURL");
    }
    
    // Only log CURL initialization once per processor instance
    static bool curlInitLogged = false;
    if (!curlInitLogged) {
        std::cout << "Successfully initialized new CURL handle for " << getId() << std::endl;
        curlInitLogged = true;
    }
}

bool ObjectDetectorProcessor::curlGet(const std::string& endpoint, nlohmann::json& responseJson) {
    // If server is known to be unavailable, we should periodically try to reconnect
    if (!serverAvailable_) {
        // Check if enough time has passed since the last server check (10 seconds)
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedSinceLastCheck = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - lastServerCheckTime_).count();
            
        if (elapsedSinceLastCheck > 10) {
            // Try to reconnect to the server
            std::cout << "Server was previously unavailable. Attempting to reconnect for GET request after " 
                      << elapsedSinceLastCheck << " seconds..." << std::endl;
            if (checkServerAvailability()) {
                std::cout << "Server is now available! Continuing with GET request." << std::endl;
                lastError_ = ""; // Clear previous error
            } else {
                lastError_ = "Skipping GET request because AI server is unavailable";
                std::cerr << lastError_ << std::endl;
                return false;
            }
        } else {
            // Not enough time has passed
            lastError_ = "Skipping GET request because AI server is unavailable";
            std::cerr << lastError_ << std::endl;
            return false;
        }
    }
    
    // Create a temporary CURL handle for this request
    CURL* handle = curl_easy_init();
    if (!handle) {
        lastError_ = "Failed to initialize CURL handle for GET request";
        std::cerr << lastError_ << std::endl;
        return false;
    }
    
    try {
        // Construct full URL
        std::string url = serverUrl_;
        if (url.back() != '/' && endpoint.front() != '/') {
            url += '/';
        }
        url += endpoint;
        
        // Set request options on fresh handle
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 5L); // 5 seconds timeout
        
        // Capture response data
        std::string response;
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        CURLcode res = curl_easy_perform(handle);
        
        // Check if request was successful
        if (res != CURLE_OK) {
            std::string errorMsg = std::string("CURL request failed: ") + curl_easy_strerror(res);
            curl_easy_cleanup(handle); // Clean up before throwing
            throw std::runtime_error(errorMsg);
        }
        
        // Get HTTP response code
        long httpCode = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);
        
        if (httpCode != 200) {
            std::string errorMsg = "Server error: " + std::to_string(httpCode) + " " + response;
            curl_easy_cleanup(handle); // Clean up before throwing
            throw std::runtime_error(errorMsg);
        }
        
        // Parse the response
        responseJson = nlohmann::json::parse(response);
        
        // Clean up the CURL handle
        curl_easy_cleanup(handle);
        
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("GET request error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        
        // Make sure we clean up the CURL handle even on error
        curl_easy_cleanup(handle);
        
        return false;
    }
}

bool ObjectDetectorProcessor::curlPost(const std::string& endpoint, const nlohmann::json& requestJson, 
                                      nlohmann::json& responseJson) {
    // Start overall POST request timing
    auto postStartTime = std::chrono::high_resolution_clock::now();
    
    // If server is known to be unavailable, we should periodically try to reconnect
    if (!serverAvailable_) {
        // Check if enough time has passed since the last server check (10 seconds)
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedSinceLastCheck = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - lastServerCheckTime_).count();
            
        if (elapsedSinceLastCheck > 10) {
            // Try to reconnect to the server
            std::cout << "Server was previously unavailable. Attempting to reconnect for POST request after " 
                      << elapsedSinceLastCheck << " seconds..." << std::endl;
            if (checkServerAvailability()) {
                std::cout << "Server is now available! Continuing with POST request." << std::endl;
                lastError_ = ""; // Clear previous error
            } else {
                lastError_ = "Skipping POST request because AI server is unavailable";
                std::cerr << lastError_ << std::endl;
                return false;
            }
        } else {
            // Not enough time has passed
            lastError_ = "Skipping POST request because AI server is unavailable";
            std::cerr << lastError_ << std::endl;
            return false;
        }
    }

    // Create a temporary CURL handle for this request
    CURL* handle = curl_easy_init();
    if (!handle) {
        lastError_ = "Failed to initialize CURL handle for POST request";
        std::cerr << lastError_ << std::endl;
        return false;
    }
    
    // Initialize headers to NULL for proper cleanup in case of errors
    struct curl_slist* headers = NULL;
    
    try {
        // Construct full URL
        std::string url = serverUrl_;
        if (url.back() != '/' && endpoint.front() != '/') {
            url += '/';
        }
        url += endpoint;
        
        // Prepare request body
        std::string requestBody = requestJson.dump();
        
        // Create headers
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        // Set request options
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_POST, 1L);
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.c_str());
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L); // 30 seconds timeout
        
        // Capture response data
        std::string response;
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        
        // Measure just the HTTP request time
        auto httpStartTime = std::chrono::high_resolution_clock::now();
        
        // Perform the request
        CURLcode res = curl_easy_perform(handle);
        
        // Measure HTTP request completion time
        auto httpEndTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> httpDuration = httpEndTime - httpStartTime;
        
        // Log HTTP request time
        std::cout << "[POST Latency] HTTP request to " << url << " took " << httpDuration.count() 
                << " ms (using " << ((protocol_ == "http_shm" || protocol_ == "grpc_shm") ? "shared memory" : "direct data") 
                << ", request size: " << requestBody.size() << " bytes)" << std::endl;
        
        // Check if request was successful
        if (res != CURLE_OK) {
            std::string errorMsg = std::string("CURL request failed: ") + curl_easy_strerror(res);
            std::cerr << "Request to " << url << " failed: " << errorMsg << std::endl;
            
            // Set the server as unavailable if this was a timeout or connection error
            if (res == CURLE_OPERATION_TIMEDOUT || 
                res == CURLE_COULDNT_CONNECT || 
                res == CURLE_COULDNT_RESOLVE_HOST) {
                serverAvailable_ = false;
                std::cerr << "Marking AI server as unavailable due to network error" << std::endl;
                lastServerCheckTime_ = std::chrono::steady_clock::now();
            }
            
            // Clean up resources before returning
            curl_slist_free_all(headers);
            curl_easy_cleanup(handle);
            
            lastError_ = errorMsg;
            return false;
        }
        
        // Get HTTP response code
        long httpCode = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &httpCode);
        
        if (httpCode != 200) {
            std::string errorMsg = "Server error: " + std::to_string(httpCode) + " " + response;
            
            // Print detailed request info for debugging
            std::cerr << "Request URL: " << url << std::endl;
            std::cerr << "Request body: " << requestBody.substr(0, 300) << "..." << std::endl;
            std::cerr << "Response: " << response << std::endl;
            
            if (response.find("unexpected inference output") != std::string::npos) {
                std::cerr << "This is likely due to a mismatch between the model's expected outputs and what was requested." << std::endl;
                std::cerr << "Current model ID: " << modelId_ << std::endl;
                
                // Provide guidance on fixing the issue
                if (modelId_ == "yolov7_qat") {
                    std::cerr << "For yolov7_qat model, ensure the output names are: num_dets, det_boxes, det_scores, det_classes" << std::endl;
                }
            }
            
            // Clean up resources before returning
            curl_slist_free_all(headers);
            curl_easy_cleanup(handle);
            
            lastError_ = errorMsg;
            return false;
        }
        
        // Parse the response
        try {
            responseJson = nlohmann::json::parse(response);
        } catch (const nlohmann::json::exception& e) {
            std::string errorMsg = "Failed to parse response JSON: " + std::string(e.what());
            std::cerr << errorMsg << std::endl;
            std::cerr << "Response received: " << response.substr(0, 200) << (response.length() > 200 ? "..." : "") << std::endl;
            
            // Clean up resources before returning
            curl_slist_free_all(headers);
            curl_easy_cleanup(handle);
            
            lastError_ = errorMsg;
            return false;
        }
        
        // Clean up resources
        curl_slist_free_all(headers);
        curl_easy_cleanup(handle);
        
        // Calculate and log total POST request time (including setup and parsing)
        auto postEndTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> totalPostDuration = postEndTime - postStartTime;
        std::cout << "[POST Latency] Total POST operation took " << totalPostDuration.count() 
                << " ms (including setup and parsing)" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        // Make sure we clean up resources even on error
        if (headers) {
            curl_slist_free_all(headers);
        }
        if (handle) {
            curl_easy_cleanup(handle);
        }
        
        lastError_ = std::string("POST request error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return false;
    }
}

size_t ObjectDetectorProcessor::curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    return utils::curlWriteCallback(contents, size, nmemb, response);
}

std::string ObjectDetectorProcessor::getServerUrlFromEnvOrConfig() {
    return utils::getServerUrlFromEnvOrConfig();
}

bool ObjectDetectorProcessor::checkServerAvailability() {
    CURL* healthCheckCurl = curl_easy_init();
    if (!healthCheckCurl) {
        std::cerr << "Failed to initialize CURL for health check" << std::endl;
        return false;
    }
    
    bool available = false;
    
    try {
        // Use the standard Triton health endpoint
        std::string url = serverUrl_;
        if (url.back() == '/') {
            url += "v2/health/ready";
        } else {
            url += "/v2/health/ready";
        }
        
        // Only log health checks if server was previously unavailable or in debug mode
        // std::cout << "Re-checking Triton server health at: " << url << std::endl;

        // Set request options with shorter timeout
        curl_easy_setopt(healthCheckCurl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(healthCheckCurl, CURLOPT_TIMEOUT, 2L); // Use a very short 2 second timeout for health checks
        curl_easy_setopt(healthCheckCurl, CURLOPT_NOBODY, 0L);  // Full request, not just HEAD
        curl_easy_setopt(healthCheckCurl, CURLOPT_HTTPGET, 1L); // GET request
        
        // Add connection options for robustness
        curl_easy_setopt(healthCheckCurl, CURLOPT_CONNECTTIMEOUT, 1L); // 1 second connect timeout
        curl_easy_setopt(healthCheckCurl, CURLOPT_TCP_KEEPALIVE, 1L); // Enable TCP keepalive
        
        // Capture response data
        std::string response;
        curl_easy_setopt(healthCheckCurl, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(healthCheckCurl, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        CURLcode res = curl_easy_perform(healthCheckCurl);
        
        // Check if request was successful
        if (res == CURLE_OK) {
            // Get HTTP response code
            long httpCode = 0;
            curl_easy_getinfo(healthCheckCurl, CURLINFO_RESPONSE_CODE, &httpCode);
            
            if (httpCode == 200) {
                available = true;
            } else {
                std::cerr << "Triton server health check failed, HTTP code: " << httpCode << std::endl;
            }
        } else {
            std::cerr << "Health check failed: " << curl_easy_strerror(res) << std::endl;
            
            // Try alternative health endpoint
            url = serverUrl_;
            if (url.back() == '/') {
                url += "api/health/ready";
            } else {
                url += "/api/health/ready";
            }
            
            std::cout << "Trying alternative health endpoint: " << url << std::endl;
            curl_easy_setopt(healthCheckCurl, CURLOPT_URL, url.c_str());
            res = curl_easy_perform(healthCheckCurl);
            
            if (res == CURLE_OK) {
                long httpCode = 0;
                curl_easy_getinfo(healthCheckCurl, CURLINFO_RESPONSE_CODE, &httpCode);
                
                if (httpCode == 200) {
                    available = true;
                } else {
                    std::cerr << "Alternative health check failed, HTTP code: " << httpCode << std::endl;
                }
            } else {
                std::cerr << "Alternative health check failed: " << curl_easy_strerror(res) << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error checking Triton server health: " << e.what() << std::endl;
    }
    
    // Clean up the temporary CURL handle
    curl_easy_cleanup(healthCheckCurl);
    
    // Update the server availability flag and check time
    serverAvailable_ = available;
    lastServerCheckTime_ = std::chrono::steady_clock::now();
    
    return available;
}

// =============================================================================
// New Improved Methods Implementation
// =============================================================================

Result<std::vector<ObjectDetectorProcessor::Detection>> ObjectDetectorProcessor::detectObjectsImproved(const cv::Mat& image) {
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Starting for processor " + getId());
    
    auto detectStartTime = std::chrono::high_resolution_clock::now();
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Checking server availability for processor " + getId());
    
    if (!checkServerAvailability()) {
        return Result<std::vector<Detection>>::error("Server is not available");
    }
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Getting model configuration for processor " + getId());
    
    // Get model configuration
    auto modelConfig = getModelConfiguration();
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Model config retrieved, preprocessing image for processor " + getId());
    
    // Preprocess image
    auto contextResult = preprocessImage(image, modelConfig);
    if (contextResult.isError()) {
        LOG_ERROR("ObjectDetectorProcessor", "detectObjectsImproved: Preprocessing failed for processor " + getId() + ": " + contextResult.getError());
        return Result<std::vector<Detection>>::error("Preprocessing failed: " + contextResult.getError());
    }
    auto context = contextResult.moveValue();
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Image preprocessed, preparing inference session for processor " + getId());
    
    // Prepare inference session
    auto sessionResult = prepareInferenceSession(context, modelConfig);
    if (sessionResult.isError()) {
        LOG_ERROR("ObjectDetectorProcessor", "detectObjectsImproved: Session preparation failed for processor " + getId() + ": " + sessionResult.getError());
        return Result<std::vector<Detection>>::error("Session preparation failed: " + sessionResult.getError());
    }
    auto session = sessionResult.moveValue();
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Session prepared, initializing inference client for processor " + getId());
    
    // Initialize inference client if needed
    if (!inferenceClient_) {
        LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Creating new inference client for processor " + getId());
        
        InferenceConfig::NetworkConfig networkConfig;
        networkConfig.serverUrl = serverUrl_;
        networkConfig.protocol = protocol_;
        networkConfig.verboseLogging = verboseLogging_;
        
        auto clientResult = InferenceClientFactory::create(networkConfig);
        if (clientResult.isError()) {
            LOG_ERROR("ObjectDetectorProcessor", "detectObjectsImproved: Failed to create inference client for processor " + getId() + ": " + clientResult.getError());
            return Result<std::vector<Detection>>::error("Failed to create inference client: " + clientResult.getError());
        }
        inferenceClient_ = clientResult.moveValue();
        LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Inference client created for processor " + getId());
    } else {
        LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Using existing inference client for processor " + getId());
    }
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Performing inference for processor " + getId());
    
    // Perform inference
    auto inferenceResult = inferenceClient_->performInference(session, modelId_);
    if (inferenceResult.isError()) {
        LOG_ERROR("ObjectDetectorProcessor", "detectObjectsImproved: Inference failed for processor " + getId() + ": " + inferenceResult.getError());
        return Result<std::vector<Detection>>::error("Inference failed: " + inferenceResult.getError());
    }
    auto result = inferenceResult.moveValue();
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Inference completed, parsing results for processor " + getId());
    
    // Parse results
    auto detectionsResult = parseInferenceResults(*result, context, modelConfig);
    if (detectionsResult.isError()) {
        LOG_ERROR("ObjectDetectorProcessor", "detectObjectsImproved: Result parsing failed for processor " + getId() + ": " + detectionsResult.getError());
        return Result<std::vector<Detection>>::error("Result parsing failed: " + detectionsResult.getError());
    }
    auto detections = detectionsResult.moveValue();
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Results parsed, applying NMS for processor " + getId());
    
    // Apply NMS
    detections = applyNonMaximumSuppression(detections, config_.getProcessingConfig().iouThreshold);
    
    // Update counters
    processedFrames_++;
    detectionCount_ += detections.size();
    
    // Log timing
    auto detectEndTime = std::chrono::high_resolution_clock::now();
    logInferenceLatency(detectStartTime, detectEndTime, detections.size());
    
    LOG_DEBUG("ObjectDetectorProcessor", "detectObjectsImproved: Successfully completed for processor " + getId() + ", found " + std::to_string(detections.size()) + " detections");
    
    return Result<std::vector<Detection>>::success(std::move(detections));
}

InferenceConfig::ModelConfig ObjectDetectorProcessor::getModelConfiguration() const {
    return InferenceConfig::ModelConfig::fromModelId(modelId_);
}

Result<ObjectDetectorProcessor::InferenceContext> ObjectDetectorProcessor::preprocessImage(
    const cv::Mat& image, const InferenceConfig::ModelConfig& config) {
    
    if (image.empty()) {
        return Result<InferenceContext>::error("Input image is empty");
    }
    
    InferenceContext context;
    context.inputSize = config.inputSize;
    context.inputFormat = config.inputFormat;
    
    // Calculate scale factors
    context.scaleX = static_cast<float>(image.cols) / config.inputSize;
    context.scaleY = static_cast<float>(image.rows) / config.inputSize;
    
    // Resize image
    cv::resize(image, context.preprocessedImage, cv::Size(config.inputSize, config.inputSize), 0, 0, cv::INTER_LINEAR);
    
    // Convert to float and normalize
    context.preprocessedImage.convertTo(context.preprocessedImage, CV_32FC3, 1.0f/255.0f);
    
    // Set input shape
    if (config.inputFormat == "NCHW") {
        context.inputShape = {1, static_cast<int64_t>(context.preprocessedImage.channels()), 
                             static_cast<int64_t>(context.preprocessedImage.rows), 
                             static_cast<int64_t>(context.preprocessedImage.cols)};
    } else {
        context.inputShape = {1, static_cast<int64_t>(context.preprocessedImage.rows), 
                             static_cast<int64_t>(context.preprocessedImage.cols), 
                             static_cast<int64_t>(context.preprocessedImage.channels())};
    }
    
    return Result<InferenceContext>::success(std::move(context));
}

Result<TritonInferenceSession> ObjectDetectorProcessor::prepareInferenceSession(
    const InferenceContext& context, const InferenceConfig::ModelConfig& config) {
    
    TritonInferenceSession session;
    
    // Add input
    auto inputResult = session.addInput(config.inputName, context.inputShape, "FP32");
    if (inputResult.isError()) {
        return Result<TritonInferenceSession>::error("Failed to add input: " + inputResult.getError());
    }
    
    // Add input data
    auto* input = session.getInput(0);
    if (!input) {
        return Result<TritonInferenceSession>::error("Failed to get input wrapper");
    }
    
    // Prepare data - CRITICAL: Store in session to keep alive during inference
    const int height = context.preprocessedImage.rows;
    const int width = context.preprocessedImage.cols;
    const int channels = context.preprocessedImage.channels();
    
    // Pre-allocate memory
    const size_t dataSizeBytes = height * width * channels * sizeof(float);
    
    // Store input data in session to keep it alive
    session.inputData.resize(dataSizeBytes);
    float* dataPtr = reinterpret_cast<float*>(session.inputData.data());
    
    // Copy data in the correct format
    if (config.inputFormat == "NCHW") {
        // Convert HWC to CHW format
        for (int c = 0; c < channels; ++c) {
            size_t chOffset = c * height * width;
            for (int h = 0; h < height; ++h) {
                for (int w = 0; w < width; ++w) {
                    const cv::Vec3f& pixel = context.preprocessedImage.at<cv::Vec3f>(h, w);
                    dataPtr[chOffset + h * width + w] = pixel[c];
                }
            }
        }
    } else {
        // NHWC format
        size_t idx = 0;
        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; ++w) {
                const cv::Vec3f& pixel = context.preprocessedImage.at<cv::Vec3f>(h, w);
                for (int c = 0; c < channels; ++c) {
                    dataPtr[idx++] = pixel[c];
                }
            }
        }
    }
    
    // Add data to input using the session-stored data
    auto dataResult = input->appendRaw(session.inputData);
    if (dataResult.isError()) {
        return Result<TritonInferenceSession>::error("Failed to add input data: " + dataResult.getError());
    }
    
    // Add outputs
    if (config.outputName.find(",") != std::string::npos) {
        // Multiple outputs
        std::stringstream ss(config.outputName);
        std::string item;
        while (std::getline(ss, item, ',')) {
            auto outputResult = session.addOutput(item);
            if (outputResult.isError()) {
                return Result<TritonInferenceSession>::error("Failed to add output '" + item + "': " + outputResult.getError());
            }
        }
    } else {
        // Single output
        auto outputResult = session.addOutput(config.outputName);
        if (outputResult.isError()) {
            return Result<TritonInferenceSession>::error("Failed to add output: " + outputResult.getError());
        }
    }
    
    return Result<TritonInferenceSession>::success(std::move(session));
}

Result<std::vector<ObjectDetectorProcessor::Detection>> ObjectDetectorProcessor::parseInferenceResults(
    const triton::client::InferResult& result, const InferenceContext& context, 
    const InferenceConfig::ModelConfig& config) {
    
    std::vector<Detection> detections;
    float actualConfidenceThreshold = confidenceThreshold_;
    
    try {
        // Check if this is YOLOv7/YOLOv7_QAT format with specific outputs
        if ((config.id == "yolov7" || config.id == "yolov7_qat") && 
            config.outputName.find(",") != std::string::npos) {
            
            // Parse YOLOv7 format with named outputs
            const uint8_t* num_dets_buf = nullptr;
            const uint8_t* boxes_buf = nullptr;
            const uint8_t* scores_buf = nullptr;
            const uint8_t* classes_buf = nullptr;
            
            size_t num_dets_byte_size = 0;
            size_t boxes_byte_size = 0;
            size_t scores_byte_size = 0;
            size_t classes_byte_size = 0;
            
            // Extract data from each tensor
            triton::client::Error err = result.RawData("num_dets", &num_dets_buf, &num_dets_byte_size);
            if (!err.IsOk()) {
                return Result<std::vector<Detection>>::error("Error getting num_dets data: " + err.Message());
            }
            
            err = result.RawData("det_boxes", &boxes_buf, &boxes_byte_size);
            if (!err.IsOk()) {
                return Result<std::vector<Detection>>::error("Error getting det_boxes data: " + err.Message());
            }
            
            err = result.RawData("det_scores", &scores_buf, &scores_byte_size);
            if (!err.IsOk()) {
                return Result<std::vector<Detection>>::error("Error getting det_scores data: " + err.Message());
            }
            
            err = result.RawData("det_classes", &classes_buf, &classes_byte_size);
            if (!err.IsOk()) {
                return Result<std::vector<Detection>>::error("Error getting det_classes data: " + err.Message());
            }
            
            if (num_dets_buf && boxes_buf && scores_buf && classes_buf) {
                int numDetections = reinterpret_cast<const int*>(num_dets_buf)[0];
                const float* boxes = reinterpret_cast<const float*>(boxes_buf);
                const float* scores = reinterpret_cast<const float*>(scores_buf);
                const int* classes = reinterpret_cast<const int*>(classes_buf);
                
                for (int i = 0; i < numDetections; ++i) {
                    float score = scores[i];
                    
                    if (score >= actualConfidenceThreshold) {
                        int classId = classes[i];
                        
                        // Extract bounding box coordinates (in x1,y1,x2,y2 format)
                        float x1 = boxes[i * 4 + 0];
                        float y1 = boxes[i * 4 + 1];
                        float x2 = boxes[i * 4 + 2];
                        float y2 = boxes[i * 4 + 3];
                        
                        // Scale to original image dimensions
                        x1 *= context.scaleX;
                        y1 *= context.scaleY;
                        x2 *= context.scaleX;
                        y2 *= context.scaleY;
                        
                        if (x1 < x2 && y1 < y2) {
                            Detection det;
                            det.bbox = cv::Rect(static_cast<int>(std::round(x1)), static_cast<int>(std::round(y1)), 
                                              static_cast<int>(std::round(x2 - x1)), static_cast<int>(std::round(y2 - y1)));
                            det.confidence = score;
                            det.className = getClassName(classId);
                            
                            // Filter by selected classes
                            if (classes_.empty() || 
                                std::find(classes_.begin(), classes_.end(), det.className) != classes_.end()) {
                                detections.push_back(det);
                            }
                        }
                    }
                }
            }
        } else {
            // Standard YOLO output format
            const uint8_t* output_buf = nullptr;
            size_t buf_size = 0;
            
            triton::client::Error err = result.RawData(config.outputName, &output_buf, &buf_size);
            if (!err.IsOk()) {
                return Result<std::vector<Detection>>::error("Error getting output data: " + err.Message());
            }
            
            // Parse standard YOLO format (implementation similar to existing code)
            // ... (This would contain the existing parsing logic)
        }
        
        return Result<std::vector<Detection>>::success(std::move(detections));
    } catch (const std::exception& e) {
        return Result<std::vector<Detection>>::error("Error parsing results: " + std::string(e.what()));
    }
}

std::vector<ObjectDetectorProcessor::Detection> ObjectDetectorProcessor::applyNonMaximumSuppression(
    const std::vector<Detection>& detections, float iouThreshold) const {
    
    if (detections.empty()) {
        return {};
    }
    
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> indices;
    
    for (size_t i = 0; i < detections.size(); ++i) {
        boxes.push_back(detections[i].bbox);
        confidences.push_back(detections[i].confidence);
        indices.push_back(static_cast<int>(i));
    }
    
    std::vector<int> keepIndices;
    cv::dnn::NMSBoxes(boxes, confidences, confidenceThreshold_, iouThreshold, keepIndices);
    
    std::vector<Detection> filteredDetections;
    for (int keepIdx : keepIndices) {
        filteredDetections.push_back(detections[keepIdx]);
    }
    
    return filteredDetections;
}

void ObjectDetectorProcessor::drawDetections(cv::Mat& image, const std::vector<Detection>& detections) const {
    for (size_t i = 0; i < detections.size(); ++i) {
        const auto& det = detections[i];
        
        cv::Scalar color(255, 255, 255); // White color for professional look
        
        // Draw bounding box
        cv::rectangle(image, det.bbox, color, 2);
        
        // Draw semi-transparent fill
        cv::Mat overlay;
        image.copyTo(overlay);
        cv::rectangle(overlay, det.bbox, color, cv::FILLED);
        cv::addWeighted(overlay, 0.1, image, 0.9, 0, image);
        
        // Draw label
        std::string label = det.className + " " + std::to_string(static_cast<int>(det.confidence * 100)) + "%";
        int baseLine;
        double fontScale = labelFontScale_;
        cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_DUPLEX, fontScale, 1, &baseLine);
        
        int padding = 5;
        cv::Point textOrg(det.bbox.x, det.bbox.y - padding);
        cv::Rect labelBg(
            textOrg.x - padding,
            textOrg.y - labelSize.height - padding,
            labelSize.width + (2 * padding),
            labelSize.height + (2 * padding)
        );
        
        // Draw label background
        cv::Mat labelOverlay;
        image.copyTo(labelOverlay);
        cv::rectangle(labelOverlay, labelBg, color, cv::FILLED);
        cv::addWeighted(labelOverlay, 0.8, image, 0.2, 0, image);
        
        // Draw text
        cv::putText(image, label, 
                   cv::Point(labelBg.x + padding, labelBg.y + labelSize.height),
                   cv::FONT_HERSHEY_DUPLEX, fontScale, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    }
}

void ObjectDetectorProcessor::logInferenceLatency(
    const std::chrono::high_resolution_clock::time_point& startTime,
    const std::chrono::high_resolution_clock::time_point& endTime,
    size_t detectionCount) const {
    
    std::chrono::duration<double, std::milli> duration = endTime - startTime;
    
    static int logCount = 0;
    bool shouldLog = verboseLogging_ || (logCount % 10 == 0);
    
    if (shouldLog) {
        std::cout << "[Improved Inference Latency] Total end-to-end detection: " 
                  << duration.count() << " ms (found " << detectionCount << " objects)" << std::endl;
    }
    logCount++;
}

} // namespace tapi 
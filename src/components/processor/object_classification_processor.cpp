#include "components/processor/object_classification_processor.h"
#include <iostream>
#include <random>
#include <string>
#include <cstring>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <chrono>
// For shared memory
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "utils/url_utils.h"
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

ObjectClassificationProcessor::ObjectClassificationProcessor(
    const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
    : ProcessorComponent(id, camera),
      type_(type),
      serverUrl_(GlobalConfig::getInstance().getAiServerUrl()),  // Always use GlobalConfig
      modelId_("image_classification"),
      modelType_("resnet50"), // Default to ResNet50 as it's generally better
      confidenceThreshold_(0.2), // Lower threshold for classifications
      drawClassification_(true),
      useSharedMemory_(true),
      textFontScale_(0.7f),
      sharedMemoryFd_(-1),
      curl_(nullptr),
      processedFrames_(0),
      classificationCount_(0),
      rng_(std::random_device()()) {
    
    // Apply initial configuration
    updateConfig(config);
    
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_ALL);
    initCurl();
}

ObjectClassificationProcessor::~ObjectClassificationProcessor() {
    stop();
    
    // Clean up CURL
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    curl_global_cleanup();
}

bool ObjectClassificationProcessor::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        std::cout << "Initializing Object Classification Processor (" << id_ << ")" << std::endl;
        
        // Initialize CURL if needed
        if (!curl_) {
            initCurl();
        }
        
        // Check server connection
        std::cout << "Checking connection to AI server: " << serverUrl_ << std::endl;
        
        // Get module health to check server availability
        auto health = getModelHealth(serverUrl_);
        std::cout << "AI server connection OK, checking available models..." << std::endl;
        
        // Get available models
        auto availableModels = getAvailableModels();
        if (availableModels.empty()) {
            throw std::runtime_error("No models found on server");
        }
        
        // Check if selected model is available
        if (std::find(availableModels.begin(), availableModels.end(), modelId_) == availableModels.end()) {
            if (!availableModels.empty()) {
                std::cout << "Selected model '" << modelId_ << "' not available. Available models: ";
                for (const auto& model : availableModels) {
                    std::cout << model << " ";
                }
                std::cout << std::endl;
                std::cout << "Falling back to first available model: " << availableModels[0] << std::endl;
                modelId_ = availableModels[0];
            } else {
                throw std::runtime_error("No models available on server");
            }
        }
        
        // Get available model types for the selected model
        auto availableModelTypes = getAvailableModelTypes(serverUrl_, modelId_);
        if (availableModelTypes.empty()) {
            throw std::runtime_error("No model types found for " + modelId_);
        }
        
        // Check if selected model type is available
        if (std::find(availableModelTypes.begin(), availableModelTypes.end(), modelType_) == availableModelTypes.end()) {
            // Look for ResNet50 first (better quality)
            auto resnetIt = std::find(availableModelTypes.begin(), availableModelTypes.end(), "resnet50");
            if (resnetIt != availableModelTypes.end()) {
                std::cout << "Selected model type '" << modelType_ << "' not available. Using ResNet50." << std::endl;
                modelType_ = "resnet50";
            } else if (!availableModelTypes.empty()) {
                std::cout << "Selected model type '" << modelType_ << "' not available. Available types: ";
                for (const auto& type : availableModelTypes) {
                    std::cout << type << " ";
                }
                std::cout << std::endl;
                std::cout << "Falling back to first available type: " << availableModelTypes[0] << std::endl;
                modelType_ = availableModelTypes[0];
            } else {
                throw std::runtime_error("No model types available for " + modelId_);
            }
        }
        
        std::cout << "Using model: " << modelId_ << " with type: " << modelType_ << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("Initialization error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return false;
    }
}

bool ObjectClassificationProcessor::start() {
    if (running_) {
        return true; // Already running
    }
    
    if (!initialize()) {
        return false;
    }
    
    running_ = true;
    std::cout << "Object Classification processor started: " << getId() << std::endl;
    return true;
}

bool ObjectClassificationProcessor::stop() {
    if (!running_) {
        return true; // Already stopped
    }
    
    running_ = false;
    
    // Clean up any shared memory resources
    cleanupSharedMemory();
    
    std::cout << "Object Classification processor stopped: " << getId() << std::endl;
    return true;
}

bool ObjectClassificationProcessor::updateConfig(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Always use GlobalConfig for the server URL
    bool serverUrlChanged = false;
    std::string newServerUrl = GlobalConfig::getInstance().getAiServerUrl();
    if (newServerUrl != serverUrl_) {
        serverUrl_ = newServerUrl;
        serverUrlChanged = true;
        std::cout << "UPDATE CONFIG: Using AI server URL from GlobalConfig: " << serverUrl_ << std::endl;
    }
    
    // Check if model ID is changing
    bool modelIdChanged = false;
    if (config.contains("model_id") && config["model_id"] != modelId_) {
        modelId_ = config["model_id"];
        modelIdChanged = true;
    }
    
    // Check if model type is changing
    bool modelTypeChanged = false;
    if (config.contains("model_type") && config["model_type"] != modelType_) {
        modelType_ = config["model_type"];
        modelTypeChanged = true;
    }
    
    if (config.contains("confidence_threshold")) {
        confidenceThreshold_ = config["confidence_threshold"];
        // Clamp to valid range
        confidenceThreshold_ = std::max(0.0f, std::min(1.0f, confidenceThreshold_));
    }
    
    if (config.contains("draw_classification")) {
        drawClassification_ = config["draw_classification"];
    }
    
    if (config.contains("use_shared_memory")) {
        useSharedMemory_ = config["use_shared_memory"];
    }
    
    if (config.contains("text_font_scale")) {
        textFontScale_ = config["text_font_scale"];
    }
    
    // Save the configuration
    config_ = config;
    
    // Reinitialize if server URL, model ID, or model type changed and we're running
    if ((serverUrlChanged || modelIdChanged || modelTypeChanged) && running_) {
        std::cout << "Server URL, model ID, or model type changed, reinitializing..." << std::endl;
        if (!initialize()) {
            std::cerr << "Failed to reinitialize after config change" << std::endl;
            return false;
        }
    }
    
    return true;
}

nlohmann::json ObjectClassificationProcessor::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create a new configuration JSON
    nlohmann::json config;
    
    // Add processor-specific configuration
    // Don't include server_url in the config as it comes from GlobalConfig
    config["model_id"] = modelId_;
    config["model_type"] = modelType_;
    config["confidence_threshold"] = confidenceThreshold_;
    config["draw_classification"] = drawClassification_;
    config["use_shared_memory"] = useSharedMemory_;
    config["text_font_scale"] = textFontScale_;
    
    return config;
}

nlohmann::json ObjectClassificationProcessor::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get base status from Component class
    auto status = Component::getStatus();
    
    // Override the generic "processor" type with the specific processor type
    status["type"] = "object_classification";
    
    status["model_id"] = modelId_;
    status["model_type"] = modelType_;
    status["server_url"] = serverUrl_;
    status["confidence_threshold"] = confidenceThreshold_;
    status["processed_frames"] = processedFrames_;
    status["classification_count"] = classificationCount_;
    status["use_shared_memory"] = useSharedMemory_;
    status["text_font_scale"] = textFontScale_;
    
    if (!lastError_.empty()) {
        status["last_error"] = lastError_;
    }
    
    return status;
}

std::pair<cv::Mat, std::vector<ObjectClassificationProcessor::Classification>> ObjectClassificationProcessor::processFrame(const cv::Mat& frame) {
    if (!running_ || frame.empty()) {
        return {frame, {}};
    }
    
    try {
        // Classify the image
        auto classifications = classifyImage(frame);
        
        // Create a copy of the frame for drawing
        cv::Mat outputFrame = frame.clone();
        
        // Draw classification text if enabled
        if (drawClassification_ && !classifications.empty()) {
            int padding = 10;
            int y = padding;
            int lineHeight = 30;
            
            // Calculate background size based on number of classifications and title
            int numLines = std::min(3, (int)classifications.size()) + 1; // Add 1 for title
            
            // Create background for classification text
            cv::Rect bgRect(padding, y, outputFrame.cols - 2 * padding, lineHeight * numLines + padding);
            cv::Scalar bgColor(0, 0, 0, 150); // Semi-transparent black background
            
            // Draw semi-transparent overlay
            cv::Mat overlay;
            outputFrame.copyTo(overlay);
            cv::rectangle(overlay, bgRect, bgColor, cv::FILLED);
            cv::addWeighted(overlay, 0.7, outputFrame, 0.3, 0, outputFrame);
            
            // Add title with model type information
            std::string title = "Classification (" + modelType_ + ")";
            cv::putText(outputFrame, title, cv::Point(padding * 2, y + lineHeight - padding/2), 
                       cv::FONT_HERSHEY_SIMPLEX, textFontScale_, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
            
            y += lineHeight;
            
            // Draw classification text (top 3)
            for (size_t i = 0; i < std::min(3, (int)classifications.size()); ++i) {
                const auto& cls = classifications[i];
                
                // Format text with confidence percentage
                std::string text = std::to_string(i+1) + ". " + cls.className;
                
                // Find last word in className and truncate if needed
                size_t maxTextLen = 25; // Limit text length to prevent overflow
                if (text.length() > maxTextLen) {
                    text = text.substr(0, maxTextLen) + "...";
                }
                
                // Add confidence as percentage
                std::string confText = " (" + std::to_string(static_cast<int>(cls.confidence * 100)) + "%)";
                text += confText;
                
                // Calculate text color based on confidence
                cv::Scalar textColor;
                if (cls.confidence > 0.8f) {
                    textColor = cv::Scalar(50, 255, 50); // Strong green for high confidence
                } else if (cls.confidence > 0.5f) {
                    textColor = cv::Scalar(255, 255, 50); // Yellow for medium confidence
                } else {
                    textColor = cv::Scalar(255, 165, 0); // Orange for lower confidence
                }
                
                // Draw text
                cv::putText(outputFrame, text, cv::Point(padding * 2, y + lineHeight - padding/2), 
                           cv::FONT_HERSHEY_SIMPLEX, textFontScale_, textColor, 2, cv::LINE_AA);
                
                y += lineHeight;
            }
        }
        
        // Update statistics
        processedFrames_++;
        classificationCount_ += classifications.size();
        
        return {outputFrame, classifications};
        
    } catch (const std::exception& e) {
        lastError_ = std::string("Processing error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return {frame, {}};
    }
}

std::vector<ObjectClassificationProcessor::Classification> ObjectClassificationProcessor::classifyImage(const cv::Mat& image) {
    std::vector<Classification> classifications;
    std::string sharedMemKey;
    bool sharedMemCreated = false;
    
    try {
        // Ensure CURL is initialized
        if (!curl_) {
            initCurl();
        }
        
        // Create request JSON
        nlohmann::json requestJson;
        requestJson["model_id"] = modelId_;
        requestJson["model_type"] = modelType_; // Include model type in the request
        
        // Use shared memory or base64 encoding for image transfer
        if (useSharedMemory_) {
            try {
                // Create shared memory and get key
                sharedMemKey = createSharedMemory(image);
                sharedMemCreated = true;
                requestJson["use_shared_memory"] = true;
                requestJson["shared_memory_key"] = sharedMemKey;
            } catch (const std::exception& e) {
                // Fall back to base64 if shared memory creation fails
                std::cerr << "Shared memory creation failed, falling back to base64: " << e.what() << std::endl;
                requestJson["image"] = imageToBase64(image);
            }
        } else {
            // Use base64 encoding
            requestJson["image"] = imageToBase64(image);
        }
        
        // Send POST request to classify endpoint
        auto start = std::chrono::high_resolution_clock::now();
        
        nlohmann::json responseJson;
        bool success = curlPost("/classify", requestJson, responseJson);
        
        auto end = std::chrono::high_resolution_clock::now();
        
        // Clean up shared memory after request is completed
        if (sharedMemCreated) {
            cleanupSharedMemory();
            sharedMemCreated = false;
        }
        
        std::chrono::duration<double, std::milli> duration = end - start;
        // std::cout << "Classification request took " << duration.count() << " ms" << std::endl;
        
        if (!success) {
            throw std::runtime_error("Failed to connect to AI server");
        }
        
        // Process classifications
        if (!responseJson.is_array()) {
            throw std::runtime_error("Expected array response from server");
        }
        
        // Process classifications
        for (const auto& cls : responseJson) {
            // Verify required fields exist
            if (!cls.contains("class_name") || !cls.contains("confidence")) {
                std::cerr << "Warning: Classification missing required fields: " << cls.dump() << std::endl;
                continue;
            }
            
            std::string className = cls["class_name"];
            float confidence = cls["confidence"];
            
            // Skip if confidence is below threshold
            if (confidence < confidenceThreshold_) {
                continue;
            }
            
            // Create classification
            Classification classification;
            classification.className = className;
            classification.confidence = confidence;
            
            classifications.push_back(classification);
        }
    } catch (const std::exception& e) {
        // Clean up shared memory on error
        if (sharedMemCreated) {
            cleanupSharedMemory();
        }
        
        lastError_ = std::string("Classification error: ") + e.what();
        std::cerr << lastError_ << std::endl;
    }
    
    return classifications;
}

std::string ObjectClassificationProcessor::createSharedMemory(const cv::Mat& image) {
    // Clean up any existing shared memory first
    cleanupSharedMemory();
    
    try {
        // Generate a random key for the shared memory segment
        sharedMemoryKey_ = generateRandomKey();
        
        // Calculate needed size
        size_t dataSize = image.total() * image.elemSize();
        size_t totalSize = sizeof(SharedMemoryImage) + dataSize;
        
        // Create shared memory segment
        sharedMemoryFd_ = shm_open(sharedMemoryKey_.c_str(), O_CREAT | O_RDWR, 0666);
        if (sharedMemoryFd_ == -1) {
            throw std::runtime_error("Failed to create shared memory: " + std::string(strerror(errno)));
        }
        
        // Set the size of the shared memory segment
        if (ftruncate(sharedMemoryFd_, totalSize) == -1) {
            close(sharedMemoryFd_);
            shm_unlink(sharedMemoryKey_.c_str());
            sharedMemoryFd_ = -1;
            throw std::runtime_error("Failed to set shared memory size: " + std::string(strerror(errno)));
        }
        
        // Map the shared memory segment
        void* addr = mmap(NULL, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, sharedMemoryFd_, 0);
        if (addr == MAP_FAILED) {
            close(sharedMemoryFd_);
            shm_unlink(sharedMemoryKey_.c_str());
            sharedMemoryFd_ = -1;
            throw std::runtime_error("Failed to map shared memory: " + std::string(strerror(errno)));
        }
        
        // Write header information
        SharedMemoryImage* header = static_cast<SharedMemoryImage*>(addr);
        header->width = image.cols;
        header->height = image.rows;
        header->channels = image.channels();
        header->step = static_cast<int>(image.step);
        header->dataSize = dataSize;
        
        // Write image data after the header
        unsigned char* dataStart = static_cast<unsigned char*>(addr) + sizeof(SharedMemoryImage);
        if (image.isContinuous()) {
            // Continuous data, can copy all at once
            memcpy(dataStart, image.data, dataSize);
        } else {
            // Copy row by row
            for (int i = 0; i < image.rows; ++i) {
                memcpy(dataStart + i * image.step, image.data + i * image.step, image.cols * image.elemSize());
            }
        }
        
        // Unmap the shared memory (we're done writing)
        munmap(addr, totalSize);
        
        return sharedMemoryKey_;
    } catch (const std::exception& e) {
        // Clean up on any error
        cleanupSharedMemory();
        throw; // Rethrow the exception
    }
}

void ObjectClassificationProcessor::cleanupSharedMemory() {
    // Close the file descriptor if it's open
    if (sharedMemoryFd_ != -1) {
        close(sharedMemoryFd_);
        sharedMemoryFd_ = -1;
    }
    
    // Unlink the shared memory if it exists
    if (!sharedMemoryKey_.empty()) {
        shm_unlink(sharedMemoryKey_.c_str());
        sharedMemoryKey_.clear();
    }
}

std::string ObjectClassificationProcessor::generateRandomKey(size_t length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    
    std::uniform_int_distribution<int> dist(0, sizeof(alphanum) - 2);
    
    std::string key = "/tapi_";
    for (size_t i = 0; i < length; ++i) {
        key += alphanum[dist(rng_)];
    }
    
    return key;
}

std::string ObjectClassificationProcessor::imageToBase64(const cv::Mat& image) {
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    return base64_encode(buffer.data(), buffer.size());
}

std::vector<std::string> ObjectClassificationProcessor::getAvailableModels() {
    return getAvailableModels(serverUrl_);
}

std::vector<std::string> ObjectClassificationProcessor::getAvailableModels(const std::string& serverUrl) {
    std::vector<std::string> models;
    
    try {
        // Create a temporary CURL handle for this request
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        // Set up URL for the request
        std::string url = serverUrl;
        if (url.back() == '/') {
            url += "module_health";
        } else {
            url += "/module_health";
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
        
        // Extract models
        if (responseJson.contains("models") && responseJson["models"].is_array()) {
            for (const auto& model : responseJson["models"]) {
                // Only include image classification models that are loaded
                if (model.contains("type") && model["type"] == "image_classification" &&
                    model.contains("status") && model["status"] == "loaded" &&
                    model.contains("id") && model["id"].is_string()) {
                    
                    models.push_back(model["id"]);
                    std::cout << "Found classification model: " << model["id"] << std::endl;
                }
            }
        }
        
        if (models.empty()) {
            std::cerr << "No image classification models available on server at " << serverUrl << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error getting available models: " << e.what() << std::endl;
    }
    
    return models;
}

std::vector<std::string> ObjectClassificationProcessor::getAvailableModelTypes(
    const std::string& serverUrl, const std::string& modelId) {
    std::vector<std::string> modelTypes = {"resnet50"}; // Default to at least have googlenet
    
    try {
        // Get model health info from server
        auto health = getModelHealth(serverUrl);
        
        // Check if models information is available
        if (health.contains("models") && health["models"].is_array()) {
            for (const auto& model : health["models"]) {
                // Find the classification model
                if (model.contains("id") && model["id"] == modelId) {
                    // Check if model_type information is available
                    if (model.contains("model_type")) {
                        // Single currently loaded model type
                        modelTypes.clear();
                        modelTypes.push_back(model["model_type"]);
                    }
                    
                    // Break after finding the right model
                    break;
                }
            }
        }
        
        // Add known model types if not found in health
        if (modelTypes.empty()) {
            modelTypes = {"googlenet", "resnet50", "mobilenet"};
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to get available model types: " << e.what() << std::endl;
        // Return default model types on error
        modelTypes = {"googlenet", "resnet50", "mobilenet"};
    }
    
    return modelTypes;
}

// Get model health information
nlohmann::json ObjectClassificationProcessor::getModelHealth(const std::string& serverUrl) {
    try {
        // Create a temporary CURL handle for this request
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        // Set up URL for the request
        std::string url = serverUrl;
        if (url.back() == '/') {
            url += "module_health";
        } else {
            url += "/module_health";
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
        
        // Parse and return the response
        return nlohmann::json::parse(response);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to get model health: ") + e.what());
    }
}

void ObjectClassificationProcessor::initCurl() {
    // Clean up existing CURL handle if any
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    
    // Create a new CURL handle
    curl_ = curl_easy_init();
    if (!curl_) {
        throw std::runtime_error("Failed to initialize CURL");
    }
}

bool ObjectClassificationProcessor::curlGet(const std::string& endpoint, nlohmann::json& responseJson) {
    if (!curl_) {
        initCurl();
    }
    
    try {
        // Reset all previous options
        curl_easy_reset(curl_);
        
        // Construct full URL
        std::string url = serverUrl_;
        if (url.back() != '/' && endpoint.front() != '/') {
            url += '/';
        }
        url += endpoint;
        
        // Set request options
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 5L); // 5 seconds timeout
        
        // Capture response data
        std::string response;
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        CURLcode res = curl_easy_perform(curl_);
        
        // Check if request was successful
        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
        }
        
        // Get HTTP response code
        long httpCode = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
        
        if (httpCode != 200) {
            throw std::runtime_error("Server error: " + std::to_string(httpCode) + " " + response);
        }
        
        // Parse the response
        responseJson = nlohmann::json::parse(response);
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("GET request error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return false;
    }
}

bool ObjectClassificationProcessor::curlPost(const std::string& endpoint, const nlohmann::json& requestJson, 
                                           nlohmann::json& responseJson) {
    if (!curl_) {
        initCurl();
    }
    
    try {
        // Reset all previous options
        curl_easy_reset(curl_);
        
        // Construct full URL
        std::string url = serverUrl_;
        if (url.back() != '/' && endpoint.front() != '/') {
            url += '/';
        }
        url += endpoint;
        
        // Prepare request body
        std::string requestBody = requestJson.dump();
        
        // Create headers
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        // Set request options
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, requestBody.c_str());
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L); // 30 seconds timeout
        
        // Capture response data
        std::string response;
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, utils::curlWriteCallback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        
        // Perform the request
        CURLcode res = curl_easy_perform(curl_);
        
        // Clean up headers
        curl_slist_free_all(headers);
        
        // Check if request was successful
        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
        }
        
        // Get HTTP response code
        long httpCode = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
        
        if (httpCode != 200) {
            throw std::runtime_error("Server error: " + std::to_string(httpCode) + " " + response);
        }
        
        // Parse the response
        responseJson = nlohmann::json::parse(response);
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("POST request error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return false;
    }
}

size_t ObjectClassificationProcessor::curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    return utils::curlWriteCallback(contents, size, nmemb, response);
}

std::string ObjectClassificationProcessor::getServerUrlFromEnvOrConfig() {
    return utils::getServerUrlFromEnvOrConfig();
}

} // namespace tapi 
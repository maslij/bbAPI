#include "components/processor/age_gender_detection_processor.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include "logger.h"
#include "base64.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <random>
#include "components/telemetry.h"
#include "utils/url_utils.h"

namespace tapi {

AgeGenderDetectionProcessor::AgeGenderDetectionProcessor(
    const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
    : ProcessorComponent(id, camera),
      type_(type),
      processedFrames_(0),
      detectionCount_(0),
      confidenceThreshold_(0.5f),
      drawDetections_(true),
      useSharedMemory_(false),
      textFontScale_(0.6f),
      serverUrl_(GlobalConfig::getInstance().getAiServerUrl()),  // Always use GlobalConfig
      modelId_("age_gender_detection"),
      curl_(nullptr),
      sharedMemoryFd_(-1) {
    
    // Initialize random number generator with random seed
    std::random_device rd;
    rng_ = std::mt19937(rd());
    
    // Apply initial configuration
    updateConfig(config);
    
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_ALL);
    initCurl();
}

AgeGenderDetectionProcessor::~AgeGenderDetectionProcessor() {
    stop();
    cleanupSharedMemory();
    
    // Clean up CURL
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    curl_global_cleanup();
}

bool AgeGenderDetectionProcessor::initialize() {
    std::cout << "Initializing AgeGenderDetectionProcessor: " << getId() << std::endl;
    
    try {
        // Initialize CURL if needed
        if (!curl_) {
            initCurl();
        }
        
        LOG_INFO("AgeGenderDetectionProcessor", "Initialized with server URL: " + serverUrl_);
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("Initialization error: ") + e.what();
        LOG_ERROR("AgeGenderDetectionProcessor", lastError_);
        return false;
    }
}

bool AgeGenderDetectionProcessor::start() {
    if (running_) {
        return true; // Already running
    }
    
    if (!initialize()) {
        return false;
    }
    
    running_ = true;
    LOG_INFO("AgeGenderDetectionProcessor", "Started: " + getId());
    return true;
}

bool AgeGenderDetectionProcessor::stop() {
    if (!running_) {
        return true; // Already stopped
    }
    
    running_ = false;
    cleanupSharedMemory();
    LOG_INFO("AgeGenderDetectionProcessor", "Stopped: " + getId());
    return true;
}

bool AgeGenderDetectionProcessor::updateConfig(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Always use GlobalConfig for the server URL
    serverUrl_ = GlobalConfig::getInstance().getAiServerUrl();
    
    if (config.contains("model_id")) {
        modelId_ = config["model_id"];
    }
    
    if (config.contains("confidence_threshold")) {
        confidenceThreshold_ = config["confidence_threshold"];
    }
    
    if (config.contains("draw_detections")) {
        drawDetections_ = config["draw_detections"];
    }
    
    if (config.contains("use_shared_memory")) {
        useSharedMemory_ = config["use_shared_memory"];
    }
    
    if (config.contains("text_font_scale")) {
        textFontScale_ = config["text_font_scale"];
    }
    
    // Save the configuration
    config_ = config;
    
    return true;
}

nlohmann::json AgeGenderDetectionProcessor::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

nlohmann::json AgeGenderDetectionProcessor::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto status = Component::getStatus();
    
    // Override the generic "processor" type with the specific processor type
    status["type"] = "age_gender_detection";
    
    status["processed_frames"] = processedFrames_;
    status["detection_count"] = detectionCount_;
    status["server_url"] = serverUrl_;
    status["model_id"] = modelId_;
    
    if (!lastError_.empty()) {
        status["last_error"] = lastError_;
    }
    
    return status;
}

std::pair<cv::Mat, std::vector<AgeGenderDetectionProcessor::AgeGenderResult>> 
AgeGenderDetectionProcessor::processFrame(const cv::Mat& frame) {
    if (!running_ || frame.empty()) {
        return {frame, {}};
    }
    
    try {
        // Process the frame
        cv::Mat outputFrame = frame.clone();
        std::vector<AgeGenderResult> results = detectAgeGender(frame);
        
        // Draw detections if enabled
        if (drawDetections_ && !results.empty()) {
            for (const auto& result : results) {
                // Draw bounding box
                cv::rectangle(outputFrame, result.bbox, cv::Scalar(0, 255, 0), 2);
                
                // Prepare text with age and gender
                std::string labelText = result.gender + ", " + std::to_string(result.age);
                
                // Create background for text
                int baseline = 0;
                cv::Size textSize = cv::getTextSize(labelText, cv::FONT_HERSHEY_SIMPLEX, 
                                                  textFontScale_, 2, &baseline);
                cv::rectangle(outputFrame, 
                              cv::Point(result.bbox.x, result.bbox.y - textSize.height - 5),
                              cv::Point(result.bbox.x + textSize.width, result.bbox.y),
                              cv::Scalar(0, 255, 0), -1);
                
                // Draw text
                cv::putText(outputFrame, labelText, 
                           cv::Point(result.bbox.x, result.bbox.y - 5),
                           cv::FONT_HERSHEY_SIMPLEX, textFontScale_, 
                           cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
            }
        }
        
        // Update statistics
        processedFrames_++;
        detectionCount_ += results.size();
        
        return {outputFrame, results};
        
    } catch (const std::exception& e) {
        lastError_ = std::string("Processing error: ") + e.what();
        LOG_ERROR("AgeGenderDetectionProcessor", lastError_);
        return {frame, {}};
    }
}

std::vector<AgeGenderDetectionProcessor::AgeGenderResult> 
AgeGenderDetectionProcessor::detectAgeGender(const cv::Mat& image) {
    std::vector<AgeGenderResult> results;
    
    if (!curl_) {
        initCurl();
    }
    
    try {
        // Prepare the request payload
        nlohmann::json payload;
        payload["model_id"] = modelId_;
        
        // Use shared memory or base64 encoding
        if (useSharedMemory_) {
            std::string sharedMemKey = createSharedMemory(image);
            if (sharedMemKey.empty()) {
                throw std::runtime_error("Failed to create shared memory");
            }
            payload["shared_memory_key"] = sharedMemKey;
        } else {
            // Convert image to base64
            std::string base64Image = imageToBase64(image);
            payload["image"] = base64Image;
        }
        
        // Make the request to the AI server
        std::string endpoint = "/detect_age_gender";
        
        // Send the request and get the response
        nlohmann::json responseJson;
        bool success = curlPost(endpoint, payload, responseJson);
        
        // Clean up shared memory
        cleanupSharedMemory();
        
        if (!success) {
            throw std::runtime_error("HTTP request failed");
        }
        
        // Process the results
        for (const auto& item : responseJson) {
            AgeGenderResult result;
            
            // Extract age and confidence
            result.age = item["age"];
            result.ageConfidence = item["age_confidence"];
            
            // Extract gender and confidence
            result.gender = item["gender"];
            result.genderConfidence = item["gender_confidence"];
            
            // Extract bounding box
            auto& bbox = item["bbox"];
            result.bbox = cv::Rect(
                bbox["x"], 
                bbox["y"], 
                bbox["width"], 
                bbox["height"]
            );
            
            // Filter by confidence threshold
            if (result.ageConfidence >= confidenceThreshold_ && 
                result.genderConfidence >= confidenceThreshold_) {
                results.push_back(result);
            }
        }
        
    } catch (const std::exception& e) {
        lastError_ = std::string("Detection error: ") + e.what();
        LOG_ERROR("AgeGenderDetectionProcessor", lastError_);
    }
    
    return results;
}

std::string AgeGenderDetectionProcessor::imageToBase64(const cv::Mat& image) {
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    return base64_encode(buffer.data(), buffer.size());
}

std::string AgeGenderDetectionProcessor::createSharedMemory(const cv::Mat& image) {
    // Clean up any existing shared memory
    cleanupSharedMemory();
    
    // Generate a random key
    sharedMemoryKey_ = "/tapi_age_gender_" + generateRandomKey();
    
    // Calculate the size needed for the image
    size_t dataSize = image.total() * image.elemSize();
    
    // Create a shared memory object
    sharedMemoryFd_ = shm_open(sharedMemoryKey_.c_str(), O_CREAT | O_RDWR, 0666);
    if (sharedMemoryFd_ == -1) {
        LOG_ERROR("AgeGenderDetectionProcessor", "Failed to create shared memory: " + std::string(strerror(errno)));
        return "";
    }
    
    // Set the size of the shared memory object
    if (ftruncate(sharedMemoryFd_, dataSize) == -1) {
        LOG_ERROR("AgeGenderDetectionProcessor", "Failed to resize shared memory: " + std::string(strerror(errno)));
        cleanupSharedMemory();
        return "";
    }
    
    // Map the shared memory object
    void* memPtr = mmap(NULL, dataSize, PROT_READ | PROT_WRITE, MAP_SHARED, sharedMemoryFd_, 0);
    if (memPtr == MAP_FAILED) {
        LOG_ERROR("AgeGenderDetectionProcessor", "Failed to map shared memory: " + std::string(strerror(errno)));
        cleanupSharedMemory();
        return "";
    }
    
    // Copy the image data to shared memory
    memcpy(memPtr, image.data, dataSize);
    
    // Unmap the memory
    munmap(memPtr, dataSize);
    
    return sharedMemoryKey_;
}

void AgeGenderDetectionProcessor::cleanupSharedMemory() {
    if (sharedMemoryFd_ != -1) {
        close(sharedMemoryFd_);
        sharedMemoryFd_ = -1;
    }
    
    if (!sharedMemoryKey_.empty()) {
        shm_unlink(sharedMemoryKey_.c_str());
        sharedMemoryKey_ = "";
    }
}

std::string AgeGenderDetectionProcessor::generateRandomKey(size_t length) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<size_t> dist(0, chars.size() - 1);
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += chars[dist(rng_)];
    }
    
    return result;
}

void AgeGenderDetectionProcessor::initCurl() {
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

bool AgeGenderDetectionProcessor::curlGet(const std::string& endpoint, nlohmann::json& responseJson) {
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
        LOG_ERROR("AgeGenderDetectionProcessor", lastError_);
        return false;
    }
}

bool AgeGenderDetectionProcessor::curlPost(const std::string& endpoint, const nlohmann::json& requestJson, 
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
        LOG_ERROR("AgeGenderDetectionProcessor", lastError_);
        return false;
    }
}

size_t AgeGenderDetectionProcessor::curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    return utils::curlWriteCallback(contents, size, nmemb, response);
}

std::string AgeGenderDetectionProcessor::getServerUrlFromEnvOrConfig() {
    return utils::getServerUrlFromEnvOrConfig();
}

} // namespace tapi 
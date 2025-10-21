#include "components/sink/file_sink.h"
#include <iostream>
#include <filesystem>

namespace tapi {

FileSink::FileSink(const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
    : SinkComponent(id, camera), 
      type_(type),
      frameWidth_(640),
      frameHeight_(480),
      fps_(30),
      fourcc_("mp4v"),
      useRawFrame_(false),
      isInitialized_(false),
      frameCount_(0) {
    
    // Set initial configuration
    config_ = config;
    
    // Parse config
    if (config_.contains("path")) {
        filePath_ = config_["path"];
    } else {
        // Default file path
        filePath_ = "/tmp/output.mp4";
    }
    
    if (config_.contains("width")) {
        frameWidth_ = config_["width"];
    }
    
    if (config_.contains("height")) {
        frameHeight_ = config_["height"];
    }
    
    if (config_.contains("fps")) {
        fps_ = config_["fps"];
    }
    
    if (config_.contains("fourcc")) {
        fourcc_ = config_["fourcc"];
    }
    
    if (config_.contains("use_raw_frame")) {
        useRawFrame_ = config_["use_raw_frame"];
    }
    
    std::cout << "Created FileSink with ID: " << id << ", path: " << filePath_ 
              << ", raw frame: " << (useRawFrame_ ? "yes" : "no") << std::endl;
}

FileSink::~FileSink() {
    if (running_) {
        stop();
    }
}

bool FileSink::initialize() {
    std::cout << "Initializing FileSink: " << getId() << std::endl;
    
    try {
        // Ensure directory exists
        std::filesystem::path path(filePath_);
        std::filesystem::create_directories(path.parent_path());
        
        // Initialize video writer
        if (!isInitialized_) {
            std::lock_guard<std::mutex> lock(videoWriterMutex_);
            
            // Get FourCC code
            int fourcc = cv::VideoWriter::fourcc(
                fourcc_[0], fourcc_[1], fourcc_[2], fourcc_[3]);
            
            // Open video writer
            videoWriter_.open(filePath_, fourcc, fps_, cv::Size(frameWidth_, frameHeight_));
            
            if (!videoWriter_.isOpened()) {
                std::cerr << "Failed to open video writer for file: " << filePath_ << std::endl;
                return false;
            }
            
            isInitialized_ = true;
            frameCount_ = 0;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error initializing FileSink: " << e.what() << std::endl;
        return false;
    }
}

bool FileSink::start() {
    if (running_) {
        return true; // Already running
    }
    
    if (!isInitialized_ && !initialize()) {
        return false;
    }
    
    std::cout << "Starting FileSink: " << getId() << std::endl;
    running_ = true;
    return true;
}

bool FileSink::stop() {
    if (!running_) {
        return true; // Already stopped
    }
    
    std::cout << "Stopping FileSink: " << getId() << std::endl;
    
    // Close video writer
    {
        std::lock_guard<std::mutex> lock(videoWriterMutex_);
        videoWriter_.release();
        isInitialized_ = false;
    }
    
    running_ = false;
    return true;
}

bool FileSink::updateConfig(const nlohmann::json& config) {
    bool needReinit = false;
    
    if (config.contains("path") && config["path"] != filePath_) {
        filePath_ = config["path"];
        needReinit = true;
    }
    
    if (config.contains("width") && config["width"] != frameWidth_) {
        frameWidth_ = config["width"];
        needReinit = true;
    }
    
    if (config.contains("height") && config["height"] != frameHeight_) {
        frameHeight_ = config["height"];
        needReinit = true;
    }
    
    if (config.contains("fps") && config["fps"] != fps_) {
        fps_ = config["fps"];
        needReinit = true;
    }
    
    if (config.contains("fourcc") && config["fourcc"] != fourcc_) {
        fourcc_ = config["fourcc"];
        needReinit = true;
    }
    
    if (config.contains("use_raw_frame")) {
        useRawFrame_ = config["use_raw_frame"];
    }
    
    // Update config object
    for (auto it = config.begin(); it != config.end(); ++it) {
        config_[it.key()] = it.value();
    }
    
    // Reinitialize if necessary
    if (needReinit && running_) {
        stop();
        return initialize() && start();
    }
    
    return true;
}

nlohmann::json FileSink::getConfig() const {
    return config_;
}

nlohmann::json FileSink::getStatus() const {
    auto status = Component::getStatus();
    
    // Override the generic "sink" type with the specific sink type
    status["type"] = "file";
    
    status["file_path"] = filePath_;
    status["frame_count"] = frameCount_;
    status["initialized"] = isInitialized_;
    status["resolution"] = {
        {"width", frameWidth_},
        {"height", frameHeight_}
    };
    status["fps"] = fps_;
    status["fourcc"] = fourcc_;
    status["use_raw_frame"] = useRawFrame_;
    
    return status;
}

bool FileSink::processFrame(const cv::Mat& frame) {
    if (!running_ || !isInitialized_) {
        return false;
    }
    
    try {
        // Resize frame if necessary
        cv::Mat outputFrame;
        if (frame.cols != frameWidth_ || frame.rows != frameHeight_) {
            cv::resize(frame, outputFrame, cv::Size(frameWidth_, frameHeight_));
        } else {
            outputFrame = frame.clone();
        }
        
        // Draw frame number in bottom right corner
        std::string frameText = std::to_string(frameCount_);
        int fontFace = cv::FONT_HERSHEY_SIMPLEX;
        double fontScale = 0.7;
        int thickness = 2;
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(frameText, fontFace, fontScale, thickness, &baseline);
        cv::Point textOrg(outputFrame.cols - textSize.width - 10, outputFrame.rows - 10);
        
        // Draw text shadow (for better visibility)
        cv::putText(outputFrame, frameText, cv::Point(textOrg.x + 1, textOrg.y + 1), 
                    fontFace, fontScale, cv::Scalar(0, 0, 0), thickness);
        // Draw text in white
        cv::putText(outputFrame, frameText, textOrg, 
                    fontFace, fontScale, cv::Scalar(255, 255, 255), thickness);
        
        // Write frame to video
        {
            std::lock_guard<std::mutex> lock(videoWriterMutex_);
            if (videoWriter_.isOpened()) {
                videoWriter_.write(outputFrame);
                frameCount_++;
                return true;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing frame in FileSink: " << e.what() << std::endl;
    }
    
    return false;
}

std::string FileSink::getFilePath() const {
    return filePath_;
}

} // namespace tapi 
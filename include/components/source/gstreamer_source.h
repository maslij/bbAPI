#pragma once

#include "component.h"
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>

namespace tapi {

// Add the enum declaration here
enum class SourceProtocol {
    RTSP,
    HTTP,
    V4L2,
    FILE,
    CSI,
    IMAGE
};

/**
 * @brief GStreamer source component for video input
 * 
 * Supports multiple input types: rtsp, file, usb, http
 * Uses GStreamer with hardware acceleration when available:
 * - NVIDIA GPU acceleration (via nvvidconv, nvv4l2decoder)
 * - VA-API acceleration (via vaapidecode, vaapipostproc)
 * - OMX hardware acceleration (via omxh264dec, omxh265dec) for devices like Raspberry Pi
 */
class GStreamerSource : public SourceComponent {
public:
    /**
     * @brief Construct a new GStreamer Source component
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Source type (rtsp, file, usb, http)
     * @param config Component configuration
     */
    GStreamerSource(const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config);
    
    /**
     * @brief Destructor
     */
    ~GStreamerSource() override;

    /**
     * @brief Initialize the source component
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override;
    
    /**
     * @brief Start the source component
     * 
     * @return true if start succeeded, false otherwise
     */
    bool start() override;
    
    /**
     * @brief Stop the source component
     * 
     * @return true if stop succeeded, false otherwise
     */
    bool stop() override;
    
    /**
     * @brief Update component configuration
     * 
     * @param config JSON configuration
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override;
    
    /**
     * @brief Get component configuration
     * 
     * @return nlohmann::json Current configuration
     */
    nlohmann::json getConfig() const override;
    
    /**
     * @brief Get component status
     * 
     * @return nlohmann::json Component status
     */
    nlohmann::json getStatus() const override;
    
    /**
     * @brief Get the latest frame
     * 
     * @return cv::Mat The latest frame
     */
    cv::Mat getFrame();
    
    /**
     * @brief Enable or disable adaptive timing - no longer used, kept for compatibility
     * 
     * @param enable Whether to enable adaptive timing
     */
    void setAdaptiveTiming(bool enable);
    
    /**
     * @brief Signal that a frame has been processed - no longer used, kept for compatibility
     */
    void signalFrameProcessed();

private:
    /**
     * @brief Build the GStreamer pipeline string based on configuration
     * 
     * Creates an optimized pipeline based on source type and available hardware acceleration
     * 
     * @return std::string The GStreamer pipeline string
     */
    std::string buildPipeline();
    
    /**
     * @brief Parse source URL to determine protocol type
     * 
     * Analyzes the URL to identify the correct protocol (RTSP, HTTP, V4L2, etc.)
     * regardless of the specified source type
     * 
     * @param uri The source URI to parse
     * @return SourceProtocol The detected protocol
     */
    SourceProtocol parseSourceProtocol(const std::string& uri) const;
    
    /**
     * @brief Thread function for capturing frames
     */
    void captureThread();
    
    /**
     * @brief Helper function to reopen RTSP stream when connection is lost
     */
    void reopenRtspStream();
    
    /**
     * @brief Detect available hardware acceleration
     * 
     * Uses GStreamer inspection to find hardware-specific elements:
     * - NVIDIA: nvvidconv, nvv4l2decoder
     * - VA-API: vaapidecode, vaapipostproc
     * - OMX: omxh264dec, omxh265dec
     * 
     * Falls back to system checks if GStreamer inspection fails
     */
    void detectHardwareAcceleration();

private:
    std::string type_;                 ///< Source type (rtsp, file, usb, http)
    std::string url_;                  ///< Source URL or file path
    int width_;                        ///< Desired width
    int height_;                       ///< Desired height
    int fps_;                          ///< Desired FPS
    std::string format_;               ///< Video format
    float quality_;                    ///< Video quality (0.0-1.0)
    
    cv::VideoCapture cap_;             ///< OpenCV video capture
    std::thread captureThread_;        ///< Thread for frame capturing
    std::mutex frameMutex_;            ///< Mutex for thread-safe frame access
    std::atomic<bool> stopRequested_;  ///< Flag to signal thread to stop
    cv::Mat latestFrame_;              ///< Latest captured frame
    
    // Hardware acceleration options
    bool useHardwareAccel_;            ///< Whether to use hardware acceleration
    std::string hwAccelType_;          ///< Type of hardware acceleration (nvidia, vaapi, omx, none, auto)
    
    // Statistics
    int frameCount_;                   ///< Number of frames captured
    double avgFps_;                    ///< Average FPS
    std::string lastError_;            ///< Last error message
    
    // Kept for backward compatibility but not used in simplified version
    std::atomic<bool> adaptiveTiming_; ///< Whether to adapt to processing speed
    
    // RTSP specific settings
    std::string rtspTransport_;        ///< RTSP transport protocol (tcp, udp, http, etc.)
    int latency_;                      ///< RTSP latency in milliseconds
    
    // Frame skipping for real-time behavior
    bool skipBufferedFrames_;          ///< Whether to skip buffered frames to stay real-time
    
    // File source handling
    bool isFileSource_;                ///< Whether this is a file source (uses direct reading)
    bool needsScaling_;                ///< Whether frames need to be scaled to match requested dimensions
};

} // namespace tapi 
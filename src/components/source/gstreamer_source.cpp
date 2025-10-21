#include "components/source/gstreamer_source.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <array>
#include <memory>
#include <logger.h>

namespace tapi {

GStreamerSource::GStreamerSource(const std::string& id, Camera* camera, 
                               const std::string& type, const nlohmann::json& config)
    : SourceComponent(id, camera),
      type_(type),
      width_(640),    // Changed from 1280 to 640 to match UI defaults
      height_(640),   // Changed from 720 to 480 to match UI defaults
      fps_(30),
      format_("h264"),
      quality_(0.8),
      stopRequested_(false),
      useHardwareAccel_(true),
      hwAccelType_("auto"),  // Auto-detect hardware acceleration by default
      frameCount_(0),
      avgFps_(0.0),
      adaptiveTiming_(false), // Kept for compatibility but no longer used
      rtspTransport_("tcp"),  // Default to TCP for RTSP
      latency_(0),         // Default latency to 0ms for minimal delay
      skipBufferedFrames_(true),  // Enable frame skipping by default to maintain real-time
      isFileSource_(false) {  // Initialize file source flag
    
    std::cout << "Creating GStreamer source: " << getId() << " of type: " << type_ << std::endl;
    
    // Apply provided configuration
    updateConfig(config);
    
    // Detect hardware acceleration capabilities
    detectHardwareAcceleration();
    
    std::cout << "GStreamer source created with resolution " << width_ << "x" << height_ 
              << ", hardware acceleration: " << (useHardwareAccel_ ? hwAccelType_ : "disabled") << std::endl;
}

GStreamerSource::~GStreamerSource() {
    stop();
}

bool GStreamerSource::initialize() {
    std::cout << "Initializing GStreamer source: " << getId() << " of type: " << type_ << std::endl;
    
    try {
        // Determine if this is a file source
        SourceProtocol protocol = parseSourceProtocol(url_);
        isFileSource_ = (protocol == SourceProtocol::FILE || type_ == "file");
        
        // Build GStreamer pipeline based on configuration
        std::string pipeline = buildPipeline();
        std::cout << "Opening pipeline: " << pipeline << std::endl;
        
        if (isFileSource_) {
            std::cout << "File source detected - using direct frame-by-frame reading mode" << std::endl;
            
            // For file sources, we'll use a simpler approach without background thread
            // Try GStreamer pipeline first, then fall back to direct opening
            cap_.open(pipeline, cv::CAP_GSTREAMER);
            
            if (!cap_.isOpened()) {
                // Fall back to direct file opening if GStreamer fails
                cap_.release();
                std::cout << "GStreamer pipeline failed, falling back to direct file open..." << std::endl;
                cap_.open(url_);
                
                if (cap_.isOpened()) {
                    std::cout << "Successfully opened file directly: " << url_ << std::endl;
                } else {
                    std::cerr << "Failed to open video file: " << url_ << std::endl;
                    lastError_ = "Failed to open video file";
                    return false;
                }
            } else {
                std::cout << "Successfully opened file with GStreamer pipeline" << std::endl;
            }
        } else {
            // For live sources, use the existing approach
            if (type_ == "rtsp") {
                std::cout << "RTSP settings: transport=" << rtspTransport_ 
                          << ", latency=" << latency_ << "ms"
                          << ", hardware acceleration=" << (useHardwareAccel_ ? hwAccelType_ : "disabled") << std::endl;
                          
                cap_.open(pipeline, cv::CAP_GSTREAMER);
                
                if (!cap_.isOpened()) {
                    std::cerr << "Failed to open RTSP stream: " << url_ << std::endl;
                    std::cerr << "Common RTSP issues:" << std::endl;
                    std::cerr << "- Check if the URL is correct" << std::endl;
                    std::cerr << "- Try a different transport protocol (TCP instead of UDP or vice versa)" << std::endl;
                    std::cerr << "- Check if the camera is accessible from this network" << std::endl;
                    std::cerr << "- Verify that the RTSP port isn't blocked by a firewall" << std::endl;
                    std::cerr << "- Try increasing the latency value" << std::endl;
                    std::cerr << "- Try disabling hardware acceleration" << std::endl;
                    
                    lastError_ = "Failed to open RTSP stream";
                    return false;
                }
                
                std::cout << "Successfully opened RTSP stream with GStreamer pipeline" << std::endl;
            } else if (type_ == "usb") {
                std::cout << "Opening USB camera with hardware acceleration=" 
                          << (useHardwareAccel_ ? hwAccelType_ : "disabled") << std::endl;
                          
                cap_.open(pipeline, cv::CAP_GSTREAMER);
                
                if (cap_.isOpened()) {
                    std::cout << "Successfully opened USB camera" << std::endl;
                }
            } else if (type_ == "http") {
                std::cout << "Opening HTTP stream with hardware acceleration=" 
                          << (useHardwareAccel_ ? hwAccelType_ : "disabled") << std::endl;
                          
                cap_.open(pipeline, cv::CAP_GSTREAMER);
                
                if (cap_.isOpened()) {
                    std::cout << "Successfully opened HTTP stream" << std::endl;
                }
            } else {
                // For other types, use GStreamer pipeline
                cap_.open(pipeline, cv::CAP_GSTREAMER);
                
                if (cap_.isOpened()) {
                    std::cout << "Successfully opened " << type_ << " stream" << std::endl;
                }
            }
            
            if (!cap_.isOpened()) {
                std::cerr << "Failed to open video source: " << url_ << std::endl;
                lastError_ = "Failed to open video source";
                return false;
            }
        }
        
        // Check properties
        double actualWidth = cap_.get(cv::CAP_PROP_FRAME_WIDTH);
        double actualHeight = cap_.get(cv::CAP_PROP_FRAME_HEIGHT);
        double actualFps = cap_.get(cv::CAP_PROP_FPS);
        
        std::cout << "Stream properties - Width: " << actualWidth 
                  << ", Height: " << actualHeight 
                  << ", FPS: " << actualFps << std::endl;
                  
        // Check if we need to do runtime scaling
        needsScaling_ = ((int)actualWidth != width_ || (int)actualHeight != height_);
        if (needsScaling_) {
            std::cout << "Runtime scaling enabled: Source frames (" << actualWidth << "x" << actualHeight 
                      << ") will be scaled to " << width_ << "x" << height_ << std::endl;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error initializing source: " << e.what() << std::endl;
        lastError_ = std::string("Initialization error: ") + e.what();
        return false;
    }
}

bool GStreamerSource::start() {
    if (running_) {
        return true; // Already running
    }
    
    if (!cap_.isOpened() && !initialize()) {
        return false;
    }
    
    stopRequested_ = false;
    running_ = true;
    
    if (isFileSource_) {
        // For file sources, no background thread needed
        std::cout << "File source started in direct read mode - no background thread" << std::endl;
    } else {
        // For live sources, start background capture thread
        captureThread_ = std::thread(&GStreamerSource::captureThread, this);
        std::cout << "Live source started with background capture thread" << std::endl;
    }
    
    return true;
}

bool GStreamerSource::stop() {
    if (!running_) {
        return true; // Already stopped
    }
    
    // Signal thread to stop
    stopRequested_ = true;
    
    if (!isFileSource_) {
        // Wait for thread to finish (only for live sources)
        if (captureThread_.joinable()) {
            captureThread_.join();
        }
    }
    
    // Release resources
    cap_.release();
    
    running_ = false;
    return true;
}

bool GStreamerSource::updateConfig(const nlohmann::json& config) {
    bool needsRestart = running_;
    bool hwAccelChanged = false;
    
    // Stop if running
    if (needsRestart) {
        stop();
    }
    
    // Update configuration
    if (config.contains("url")) {
        url_ = config["url"];
        // Re-evaluate if this is a file source when URL changes
        SourceProtocol protocol = parseSourceProtocol(url_);
        isFileSource_ = (protocol == SourceProtocol::FILE || type_ == "file");
    }
    
    if (config.contains("width")) {
        width_ = config["width"];
    }
    
    if (config.contains("height")) {
        height_ = config["height"];
    }
    
    if (config.contains("fps")) {
        fps_ = config["fps"];
    }
    
    if (config.contains("format")) {
        format_ = config["format"];
    }
    
    if (config.contains("quality")) {
        quality_ = config["quality"];
    }
    
    // Handle hardware acceleration settings
    if (config.contains("use_hw_accel")) {
        bool newUseHardwareAccel = config["use_hw_accel"];
        if (newUseHardwareAccel != useHardwareAccel_) {
            useHardwareAccel_ = newUseHardwareAccel;
            hwAccelChanged = true;
        }
    }
    
    if (config.contains("hw_accel_type")) {
        std::string newHwAccelType = config["hw_accel_type"];
        if (newHwAccelType != hwAccelType_) {
            hwAccelType_ = newHwAccelType;
            hwAccelChanged = true;
        }
    }
    
    // Re-detect hardware acceleration if settings changed
    if (hwAccelChanged && hwAccelType_ == "auto") {
        detectHardwareAcceleration();
        std::cout << "Hardware acceleration settings changed, detected: " 
                  << (useHardwareAccel_ ? hwAccelType_ : "disabled") << std::endl;
    }
    
    if (config.contains("adaptive_timing")) {
        adaptiveTiming_ = config["adaptive_timing"];
    }
    
    // Add support for RTSP-specific parameters
    if (config.contains("rtsp_transport")) {
        rtspTransport_ = config["rtsp_transport"];
    }
    
    if (config.contains("latency")) {
        latency_ = config["latency"];
    }
    
    // Add support for frame skipping
    if (config.contains("skip_buffered_frames")) {
        skipBufferedFrames_ = config["skip_buffered_frames"];
    }
    
    // Save the new configuration
    config_ = config;
    
    // Restart if it was running
    if (needsRestart) {
        return start();
    }
    
    return true;
}

nlohmann::json GStreamerSource::getConfig() const {
    return config_;
}

nlohmann::json GStreamerSource::getStatus() const {
    auto status = Component::getStatus();
    
    // Override the generic "source" type with the specific source type (rtsp, file, etc.)
    status["type"] = type_;
    
    status["frames_processed"] = frameCount_;
    status["average_fps"] = avgFps_;
    status["width"] = width_;
    status["height"] = height_;
    status["target_fps"] = fps_;
    status["url"] = url_;
    status["hardware_acceleration"] = useHardwareAccel_ ? "enabled" : "disabled";
    status["is_file_source"] = isFileSource_;
    status["frame_reading_mode"] = isFileSource_ ? "direct" : "background_thread";
    
    // Add more detailed hardware acceleration information
    if (useHardwareAccel_) {
        status["hw_accel_type"] = hwAccelType_;
        
        // Add more specific information based on hardware type
        if (hwAccelType_ == "nvidia") {
            status["hw_accel_details"] = "NVIDIA GPU hardware acceleration (nvvidconv, nvv4l2decoder)";
        } else if (hwAccelType_ == "vaapi") {
            status["hw_accel_details"] = "VA-API hardware acceleration (vaapidecode, vaapipostproc)";
        } else if (hwAccelType_ == "omx") {
            status["hw_accel_details"] = "OMX hardware acceleration (omxh264dec, omxh265dec)";
        } else if (hwAccelType_ == "none") {
            status["hw_accel_details"] = "Hardware acceleration not available, using software decoding";
        }
    }
    
    status["adaptive_timing"] = adaptiveTiming_ ? "enabled" : "disabled";
    status["skip_buffered_frames"] = skipBufferedFrames_ ? "enabled" : "disabled";
    
    // Add RTSP specific information if this is an RTSP source
    if (type_ == "rtsp") {
        status["rtsp_transport"] = rtspTransport_;
        status["latency"] = latency_;
    }
    
    if (!lastError_.empty()) {
        status["last_error"] = lastError_;
    }
    
    return status;
}

cv::Mat GStreamerSource::getFrame() {
    if (!running_ || !cap_.isOpened()) {
        return cv::Mat();
    }
    
    if (isFileSource_) {
        // For file sources: directly read the next frame
        std::lock_guard<std::mutex> lock(frameMutex_);
        
        cv::Mat frame;
        bool ret = cap_.read(frame);
        
        if (!ret || frame.empty()) {
            // Check if we've reached the end of the file
            if (cap_.get(cv::CAP_PROP_POS_FRAMES) >= cap_.get(cv::CAP_PROP_FRAME_COUNT)) {
                // End of file - restart by reopening the file source
                std::cout << "End of file reached, restarting from beginning" << std::endl;
                cap_.release();
                
                // Reopen the file source
                std::string pipeline = buildPipeline();
                cap_.open(pipeline, cv::CAP_GSTREAMER);
                
                if (!cap_.isOpened()) {
                    // Fall back to direct file opening if GStreamer fails
                    cap_.open(url_);
                }
                
                if (cap_.isOpened()) {
                    ret = cap_.read(frame);
                } else {
                    std::cerr << "Failed to reopen video file for looping" << std::endl;
                    return cv::Mat();
                }
            }
            
            if (!ret || frame.empty()) {
                std::cerr << "Failed to read frame from file" << std::endl;
                return cv::Mat();
            }
        }
        
        // Scale the frame if needed
        if (needsScaling_ && !frame.empty()) {
            cv::Mat scaledFrame;
            cv::resize(frame, scaledFrame, cv::Size(width_, height_), 0, 0, cv::INTER_LINEAR);
            frame = scaledFrame;
        }
        
        // Update statistics
        frameCount_++;
        
        return frame.clone();
    } else {
        // For live sources: return the latest frame captured by the background thread
        std::lock_guard<std::mutex> lock(frameMutex_);
        return latestFrame_.clone();
    }
}

void GStreamerSource::setAdaptiveTiming(bool enable) {
    // This method is now a no-op, kept for backward compatibility
    // We store the value but don't use it in our simplified model
    adaptiveTiming_ = enable;
}

void GStreamerSource::signalFrameProcessed() {
    // This method is now a no-op, kept for backward compatibility
    // In our simplified model, file sources read directly and live sources
    // continuously read frames without waiting for processing signals
}

void GStreamerSource::detectHardwareAcceleration() {
    if (!useHardwareAccel_ || hwAccelType_ != "auto") {
        return;
    }
    
    // Check if we can use GStreamer inspection
    std::array<char, 128> buffer;
    std::string gstElements;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen("gst-inspect-1.0 2>/dev/null", "r"), pclose);
    
    if (!pipe) {
        std::cout << "Warning: Failed to run gst-inspect-1.0, falling back to basic detection" << std::endl;
        goto fallback_detection;
    }
    
    // Read all available GStreamer elements
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        gstElements += buffer.data();
    }
    
    // Check for NVIDIA hardware elements
    if (gstElements.find("nvvidconv") != std::string::npos &&
        gstElements.find("nvv4l2decoder") != std::string::npos) {
        std::cout << "NVIDIA GPU elements detected in GStreamer, using NVIDIA hardware acceleration" << std::endl;
        hwAccelType_ = "nvidia";
        return;
    }
    
    // Check for VA-API elements
    if (gstElements.find("vaapidecode") != std::string::npos &&
        gstElements.find("vaapipostproc") != std::string::npos) {
        std::cout << "VA-API elements detected in GStreamer, using VA-API hardware acceleration" << std::endl;
        hwAccelType_ = "vaapi";
        return;
    }
    
    // Check for OMX hardware encoders (like on Raspberry Pi)
    if (gstElements.find("omxh264dec") != std::string::npos) {
        std::cout << "OMX hardware decoder detected in GStreamer, using OMX hardware acceleration" << std::endl;
        hwAccelType_ = "omx";
        return;
    }
    
fallback_detection:
    // Fall back to traditional detection methods
    // Check for NVIDIA hardware
    if (system("which nvidia-smi > /dev/null 2>&1") == 0) {
        std::cout << "NVIDIA GPU detected, using NVIDIA hardware acceleration" << std::endl;
        hwAccelType_ = "nvidia";
        return;
    }
    
    // Check for VAAPI compatibility
    if (system("test -e /dev/dri/renderD128 > /dev/null 2>&1") == 0) {
        std::cout << "VA-API compatible device detected, using VAAPI hardware acceleration" << std::endl;
        hwAccelType_ = "vaapi";
        return;
    }
    
    // Fallback to software decoding
    std::cout << "No hardware acceleration detected, using software decoding" << std::endl;
    hwAccelType_ = "none";
    useHardwareAccel_ = false;
}

SourceProtocol GStreamerSource::parseSourceProtocol(const std::string& uri) const {
    // Check for explicit schemes/protocols
    if (uri.find("rtsp://") == 0) {
        return SourceProtocol::RTSP;
    } else if (uri.find("http://") == 0 || uri.find("https://") == 0) {
        return SourceProtocol::HTTP;
    } else if (uri.find("csi://") == 0) {
        return SourceProtocol::CSI;
    }
    
    // Check for device paths and file patterns
    if (uri.find("/dev/video") != std::string::npos) {
        return SourceProtocol::V4L2;
    } else if (uri.find("%") != std::string::npos) {
        return SourceProtocol::IMAGE; // Image sequence pattern
    }
    
    // Default to file
    return SourceProtocol::FILE;
}

std::string GStreamerSource::buildPipeline() {
    std::ostringstream pipeline;
    
    // Detect the actual protocol from the URL
    SourceProtocol protocol = parseSourceProtocol(url_);
    
    // Log the detected protocol
    std::cout << "Source protocol detected: ";
    switch (protocol) {
        case SourceProtocol::RTSP: std::cout << "RTSP"; break;
        case SourceProtocol::HTTP: std::cout << "HTTP"; break;
        case SourceProtocol::V4L2: std::cout << "V4L2"; break;
        case SourceProtocol::CSI: std::cout << "CSI"; break;
        case SourceProtocol::IMAGE: std::cout << "IMAGE"; break;
        case SourceProtocol::FILE: std::cout << "FILE"; break;
    }
    std::cout << " (type specified as: " << type_ << ")" << std::endl;
    
    // Build pipeline based on protocol, regardless of the specified type
    if (protocol == SourceProtocol::RTSP || (type_ == "rtsp" && protocol != SourceProtocol::HTTP)) {
        // RTSP stream
        // Simpler, more reliable RTSP pipeline
        pipeline << "rtspsrc location=" << url_ 
                 << " latency=" << std::to_string(latency_) 
                 << " protocols=" << rtspTransport_
                 << " drop-on-latency=false"  // Changed to false to prioritize reliable frame delivery over latency
                 << " buffer-mode=auto"       // Auto buffer mode for reliability
                 << " do-retransmission=true" // Request packet retransmission for missed packets
                 << " retry=5"                // Retry connection 5 times before giving up
                 << " timeout=5000000"        // 5 second timeout (in microseconds)
                 << " ! ";
        
        if (format_ == "h264") {
            pipeline << "rtph264depay ! h264parse ! ";
            
            if (useHardwareAccel_) {
                if (hwAccelType_ == "nvidia") {
                    // NVIDIA hardware acceleration for H.264
                    pipeline << "nvv4l2decoder ! nvvidconv ! ";
                    pipeline << "video/x-raw, format=BGRx ! videoconvert ! videoscale ! ";
                } else if (hwAccelType_ == "vaapi") {
                    // VA-API hardware acceleration for H.264
                    pipeline << "vaapidecode ! vaapipostproc ! ";
                    pipeline << "video/x-raw, format=BGRx ! videoconvert ! videoscale ! ";
                } else if (hwAccelType_ == "omx") {
                    // OMX hardware acceleration for H.264
                    pipeline << "omxh264dec ! ";
                    pipeline << "videoconvert ! videoscale ! ";
                } else {
                    // Software decoding fallback
                    pipeline << "avdec_h264 ! videoconvert ! videoscale ! ";
                }
            } else {
                // Software decoding
                pipeline << "avdec_h264 ! videoconvert ! videoscale ! ";
            }
            
            pipeline << "video/x-raw, width=" << std::to_string(width_) 
                     << ", height=" << std::to_string(height_) << ", format=BGR ! ";
        } else if (format_ == "h265") {
            pipeline << "rtph265depay ! h265parse ! ";
            
            if (useHardwareAccel_) {
                if (hwAccelType_ == "nvidia") {
                    // NVIDIA hardware acceleration for H.265
                    pipeline << "nvv4l2decoder ! nvvidconv ! ";
                    pipeline << "video/x-raw, format=BGRx ! videoconvert ! videoscale ! ";
                } else if (hwAccelType_ == "vaapi") {
                    // VA-API hardware acceleration for H.265
                    pipeline << "vaapidecode ! vaapipostproc ! ";
                    pipeline << "video/x-raw, format=BGRx ! videoconvert ! videoscale ! ";
                } else if (hwAccelType_ == "omx") {
                    // OMX hardware acceleration for H.265
                    pipeline << "omxh265dec ! ";
                    pipeline << "videoconvert ! videoscale ! ";
                } else {
                    // Software decoding fallback
                    pipeline << "avdec_h265 ! videoconvert ! videoscale ! ";
                }
            } else {
                // Software decoding
                pipeline << "avdec_h265 ! videoconvert ! videoscale ! ";
            }
            
            pipeline << "video/x-raw, width=" << std::to_string(width_) 
                     << ", height=" << std::to_string(height_) << ", format=BGR ! ";
        } else {
            // Generic decoder path
            pipeline << "decodebin ! videoconvert ! videoscale ! ";
            pipeline << "video/x-raw, width=" << std::to_string(width_) 
                     << ", height=" << std::to_string(height_) << ", format=BGR ! ";
        }
    } else if (protocol == SourceProtocol::HTTP || (type_ == "http")) {
        // HTTP stream with hardware acceleration if available
        pipeline << "souphttpsrc location=" << url_
                 << " timeout=10"          // 10 second timeout for HTTP requests
                 << " retries=3"           // Retry 3 times on failure
                 << " keep-alive=true"     // Enable HTTP keep-alive
                 << " ! ";
        
        if (url_.find(".m3u8") != std::string::npos) {
            // HLS stream
            pipeline << "hlsdemux ! decodebin ! ";
        } else {
            // Generic HTTP stream - add queue for better buffering
            pipeline << "queue max-size-buffers=100 max-size-time=5000000000 ! decodebin ! ";
        }
        
        if (useHardwareAccel_) {
            if (hwAccelType_ == "nvidia") {
                pipeline << "nvvidconv ! ";
                pipeline << "video/x-raw, format=BGRx ! videoconvert ! videoscale ! ";
            } else if (hwAccelType_ == "vaapi") {
                pipeline << "vaapipostproc ! ";
                pipeline << "video/x-raw, format=BGRx ! videoconvert ! videoscale ! ";
            } else {
                pipeline << "videoconvert ! videoscale ! ";
            }
        } else {
            pipeline << "videoconvert ! videoscale ! ";
        }
        
        pipeline << "video/x-raw, width=" << std::to_string(width_) 
                 << ", height=" << std::to_string(height_) << ", format=BGR ! ";
    } else if (protocol == SourceProtocol::V4L2 || type_ == "usb") {
        // USB camera
        int device_id = 0;
        
        // Extract device number from path or try to convert the URL to a number
        if (protocol == SourceProtocol::V4L2) {
            std::string dev_path = url_;
            size_t video_pos = dev_path.find("/dev/video");
            if (video_pos != std::string::npos) {
                try {
                    device_id = std::stoi(dev_path.substr(video_pos + 10)); // Skip "/dev/video"
                } catch (...) {
                    // Use default if parsing fails
                }
            }
        } else {
            try {
                device_id = std::stoi(url_);
            } catch (...) {
                // Default to 0 if parsing fails
            }
        }
        
        pipeline << "v4l2src device=/dev/video" << std::to_string(device_id) << " ! ";
        pipeline << "video/x-raw, width=" << std::to_string(width_) 
                << ", height=" << std::to_string(height_) 
                << ", framerate=" << std::to_string(fps_) << "/1 ! ";
                
        // Apply hardware conversion if available
        if (useHardwareAccel_ && hwAccelType_ == "nvidia") {
            pipeline << "nvvidconv ! video/x-raw(memory:NVMM), format=I420 ! ";
            pipeline << "nvvidconv ! video/x-raw, format=BGRx ! videoconvert ! ";
        } else {
            pipeline << "videoconvert ! ";
        }
        
        pipeline << "video/x-raw, format=BGR ! ";
    } else if (protocol == SourceProtocol::FILE || type_ == "file") {
        // File source with hardware acceleration if available
        pipeline << "filesrc location=" << url_ << " ! decodebin ! ";
        
        if (useHardwareAccel_) {
            if (hwAccelType_ == "nvidia") {
                pipeline << "nvvidconv interpolation-method=5 ! ";
                pipeline << "video/x-raw, format=BGRx ! videoconvert ! videoscale ! ";
            } else if (hwAccelType_ == "vaapi") {
                pipeline << "vaapipostproc ! ";
                pipeline << "video/x-raw, format=BGRx ! videoconvert ! videoscale ! ";
            } else {
                pipeline << "videoconvert ! videoscale ! ";
            }
        } else {
            pipeline << "videoconvert ! videoscale ! ";
        }
        
        pipeline << "video/x-raw, width=" << std::to_string(width_) 
                 << ", height=" << std::to_string(height_) << ", format=BGR ! ";
    } else {
        // Default fallback for unrecognized protocols
        pipeline << "filesrc location=" << url_ << " ! decodebin ! videoconvert ! videoscale ! ";
        pipeline << "video/x-raw, width=" << std::to_string(width_) 
                 << ", height=" << std::to_string(height_) << ", format=BGR ! ";
    }
    
    // Add appsink with different settings for file vs live sources
    if (protocol == SourceProtocol::FILE || type_ == "file") {
        // For files, we still need appsink but won't use background thread
        pipeline << "appsink drop=false max-buffers=1 sync=false emit-signals=false";
        std::cout << "Using direct file mode: sync=false, drop=false for frame-by-frame processing" << std::endl;
    } else {
        // For live sources, keep existing low-latency settings
        pipeline << "appsink drop=true max-buffers=1 sync=false";
        std::cout << "Using live timing mode: sync=false, drop=true for low latency" << std::endl;
    }
    
    return pipeline.str();
}

void GStreamerSource::captureThread() {
    // This method is only called for live sources (not file sources)
    LOG_INFO("Camera", "Background processing thread for camera " + getId() + " started");
    
    cv::Mat frame;
    auto startTime = std::chrono::steady_clock::now();
    int localFrameCount = 0;
    int consecutiveFailures = 0;
    
    std::cout << "Live source detected - using minimal sleep for CPU efficiency" << std::endl;
    
    while (!stopRequested_) {
        // Simple, direct frame reading approach
        bool ret = cap_.read(frame);
        
        // Check for stop request after read
        if (stopRequested_) break;
        
        if (!ret || frame.empty()) {
            consecutiveFailures++;
            
            // Don't spam logs for repeated failures
            if (consecutiveFailures <= 1) {
                std::cerr << "Failed to read frame" << std::endl;
            }
            
            // Handle different source types - reopen if needed
            if (type_ == "rtsp" && consecutiveFailures <= 10) {
                reopenRtspStream();
            } else {
                // For other source types or after too many failures, just sleep a bit
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            // Check for stop requests after handling failures
            if (stopRequested_) break;
            continue;
        }
        
        // Reset failure counter when we successfully read a frame
        consecutiveFailures = 0;
        
        // Scale the frame if needed (when dimensions don't match requested)
        if (needsScaling_ && !frame.empty()) {
            cv::Mat scaledFrame;
            cv::resize(frame, scaledFrame, cv::Size(width_, height_), 0, 0, cv::INTER_LINEAR);
            frame = scaledFrame;
        }
        
        // Update frame with simple lock
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            latestFrame_ = frame.clone();
        }
        
        // Update statistics
        localFrameCount++;
        frameCount_++;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        
        if (elapsed >= 1) {
            avgFps_ = static_cast<double>(localFrameCount) / elapsed;
            startTime = now;
            localFrameCount = 0;
        }
        
        // Add small sleep to prevent CPU overload
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    LOG_INFO("Camera", "Background processing thread for camera " + getId() + " exiting");
}

// Helper function to reopen RTSP stream
void GStreamerSource::reopenRtspStream() {
    std::cout << "RTSP connection lost, attempting to reconnect..." << std::endl;
    
    cap_.release();
    if (stopRequested_) return;
    
    // Use GStreamer pipeline to ensure dimensions are respected
    std::string pipeline = buildPipeline();
    cap_.open(pipeline, cv::CAP_GSTREAMER);
    
    if (!cap_.isOpened()) {
        std::cerr << "Failed to reconnect to RTSP stream" << std::endl;
    } else {
        std::cout << "Successfully reconnected to RTSP stream" << std::endl;
    }
}

} // namespace tapi 
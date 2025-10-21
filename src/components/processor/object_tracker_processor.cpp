#include "components/processor/object_tracker_processor.h"
#include <iostream>
#include <random>
#include <algorithm>
#include <chrono>
#include <mutex>

namespace tapi {

// Generate random colors for visualization
static std::vector<cv::Scalar> generateColors(size_t numColors) {
    std::vector<cv::Scalar> colors;
    std::mt19937 rng(42); // Fixed seed for reproducibility
    std::uniform_int_distribution<int> dist(50, 255);
    
    for (size_t i = 0; i < numColors; ++i) {
        colors.push_back(cv::Scalar(dist(rng), dist(rng), dist(rng)));
    }
    
    return colors;
}

// Generate HSV-based colors for better visual separation
static std::vector<cv::Scalar> generateDistinctColors(size_t numColors) {
    std::vector<cv::Scalar> colors;
    
    if (numColors == 0) return colors;
    
    // Divide the hue range (0-180 in OpenCV) by number of classes needed
    for (size_t i = 0; i < numColors; ++i) {
        double hue = 180.0 * i / numColors;
        // Use high saturation and value for vibrant colors
        cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar(hue, 255, 255));
        cv::Mat rgb;
        cv::cvtColor(hsv, rgb, cv::COLOR_HSV2BGR);
        
        // Extract RGB values
        cv::Vec3b color = rgb.at<cv::Vec3b>(0, 0);
        colors.push_back(cv::Scalar(color[0], color[1], color[2]));
    }
    
    return colors;
}

// Generate a visually pleasing color based on class name hash
static cv::Scalar generateColorFromClassName(const std::string& className) {
    uint32_t hash = std::hash<std::string>{}(className);
    float hue = (hash % 360) / 360.0f;
    float saturation = 0.8f;
    float value = 1.0f;
    
    // Convert HSV to BGR
    float c = value * saturation;
    float x = c * (1 - std::abs(std::fmod(hue * 6, 2) - 1));
    float m = value - c;
    
    float r, g, b;
    if (hue < 1.0f/6) { r = c; g = x; b = 0; }
    else if (hue < 2.0f/6) { r = x; g = c; b = 0; }
    else if (hue < 3.0f/6) { r = 0; g = c; b = x; }
    else if (hue < 4.0f/6) { r = 0; g = x; b = c; }
    else if (hue < 5.0f/6) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    
    return cv::Scalar(
        (b + m) * 255,
        (g + m) * 255,
        (r + m) * 255
    );
}

// Helper function to generate consistent numeric ID for class names
static int getConsistentClassId(const std::string& className) {
    // Use a deterministic hash function to ensure the same className 
    // always maps to the same numeric ID
    return std::hash<std::string>{}(className) % 10000;
}

ObjectTrackerProcessor::ObjectTrackerProcessor(
    const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
    : ProcessorComponent(id, camera),
      type_(type),
      frameRate_(30),
      trackBuffer_(30),
      trackThresh_(0.5),
      highThresh_(0.6),
      matchThresh_(0.8),
      drawTracking_(true),
      drawTrackId_(true),
      drawTrackTrajectory_(true),
      drawSemiTransparentBoxes_(true),
      labelFontScale_(0.5f),
      totalTrackedObjects_(0),
      activeTrackedObjects_(0),
      processedFrames_(0),
      trajectoryMaxLength_(60),
      maxAllowedDistanceRatio_(0.2),
      trajectoryCleanupThreshold_(30) {
    
    // Apply initial configuration
    updateConfig(config);
    
    // Initialize with an empty color map
    // Colors will be generated dynamically as new classes are encountered
}

ObjectTrackerProcessor::~ObjectTrackerProcessor() {
    stop();
}

bool ObjectTrackerProcessor::initialize() {
    std::cout << "Initializing Object Tracker processor: " << getId() << std::endl;
    
    try {
        // Create ByteTracker instance with configured parameters
        tracker_ = std::make_unique<BYTETracker>(
            frameRate_, trackBuffer_);
        
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string("Initialization error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return false;
    }
}

bool ObjectTrackerProcessor::start() {
    if (running_) {
        return true; // Already running
    }
    
    if (!initialize()) {
        return false;
    }
    
    running_ = true;
    std::cout << "Object Tracker processor started: " << getId() << std::endl;
    return true;
}

bool ObjectTrackerProcessor::stop() {
    if (!running_) {
        return true; // Already stopped
    }
    
    running_ = false;
    std::cout << "Object Tracker processor stopped: " << getId() << std::endl;
    return true;
}

bool ObjectTrackerProcessor::updateConfig(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update configuration
    if (config.contains("frame_rate")) {
        frameRate_ = config["frame_rate"];
    }
    
    if (config.contains("track_buffer")) {
        trackBuffer_ = config["track_buffer"];
    }
    
    if (config.contains("track_thresh")) {
        trackThresh_ = config["track_thresh"];
        // Clamp to valid range
        trackThresh_ = std::max(0.0f, std::min(1.0f, trackThresh_));
    }
    
    if (config.contains("high_thresh")) {
        highThresh_ = config["high_thresh"];
        // Clamp to valid range
        highThresh_ = std::max(0.0f, std::min(1.0f, highThresh_));
    }
    
    if (config.contains("match_thresh")) {
        matchThresh_ = config["match_thresh"];
        // Clamp to valid range
        matchThresh_ = std::max(0.0f, std::min(1.0f, matchThresh_));
    }
    
    if (config.contains("draw_tracking")) {
        drawTracking_ = config["draw_tracking"];
    }
    
    // New visualization options
    if (config.contains("draw_track_id")) {
        drawTrackId_ = config["draw_track_id"];
    }
    
    if (config.contains("draw_track_trajectory")) {
        drawTrackTrajectory_ = config["draw_track_trajectory"];
    }
    
    if (config.contains("draw_semi_transparent_boxes")) {
        drawSemiTransparentBoxes_ = config["draw_semi_transparent_boxes"];
    }
    
    if (config.contains("label_font_scale")) {
        labelFontScale_ = config["label_font_scale"];
    }

    // New trajectory configuration parameters
    if (config.contains("trajectory_max_length")) {
        trajectoryMaxLength_ = config["trajectory_max_length"];
    }
    
    if (config.contains("max_allowed_distance_ratio")) {
        maxAllowedDistanceRatio_ = config["max_allowed_distance_ratio"];
        // Clamp to valid range (1-100% of frame width)
        maxAllowedDistanceRatio_ = std::max(0.01f, std::min(1.0f, maxAllowedDistanceRatio_));
    }
    
    if (config.contains("trajectory_cleanup_threshold")) {
        trajectoryCleanupThreshold_ = config["trajectory_cleanup_threshold"];
    }
    
    // Save the configuration
    config_ = config;
    
    // If already running, reinitialize tracker with new parameters
    if (running_ && tracker_) {
        tracker_ = std::make_unique<BYTETracker>(
            frameRate_, trackBuffer_);
    }
    
    return true;
}

nlohmann::json ObjectTrackerProcessor::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

nlohmann::json ObjectTrackerProcessor::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto status = Component::getStatus();
    status["type"] = "object_tracking";
    status["frame_rate"] = frameRate_;
    status["track_buffer"] = trackBuffer_;
    status["track_thresh"] = trackThresh_;
    status["high_thresh"] = highThresh_;
    status["match_thresh"] = matchThresh_;
    status["draw_tracking"] = drawTracking_;
    status["draw_track_id"] = drawTrackId_;
    status["draw_track_trajectory"] = drawTrackTrajectory_;
    status["draw_semi_transparent_boxes"] = drawSemiTransparentBoxes_;
    status["label_font_scale"] = labelFontScale_;
    status["processed_frames"] = processedFrames_;
    status["total_tracked_objects"] = totalTrackedObjects_;
    status["active_tracked_objects"] = activeTrackedObjects_;
    
    if (!lastError_.empty()) {
        status["last_error"] = lastError_;
    }
    
    return status;
}

Object ObjectTrackerProcessor::convertDetection(
    const ObjectDetectorProcessor::Detection& detection) {
    // Convert OpenCV Rect to ByteTracker Object
    Object obj;
    obj.rect = cv::Rect_<float>(
        detection.bbox.x,
        detection.bbox.y,
        detection.bbox.width,
        detection.bbox.height
    );
    
    // Get a consistent numeric label from the class name
    int label = getConsistentClassId(detection.className);
    obj.label = label;
    obj.prob = detection.confidence;
    
    // Store in the persistent class mapping
    labelToClassMap_[label] = detection.className;
    
    // Add to unique class names if not already present
    if (std::find(uniqueClassNames_.begin(), uniqueClassNames_.end(), detection.className) == uniqueClassNames_.end()) {
        uniqueClassNames_.push_back(detection.className);
        
        // Generate new distinct colors if needed
        if (uniqueClassNames_.size() > classColorMap_.size()) {
            classColorMap_ = generateDistinctColors(uniqueClassNames_.size());
        }
    }
    
    return obj;
}

std::pair<cv::Mat, std::vector<ObjectTrackerProcessor::TrackedObject>> 
ObjectTrackerProcessor::processFrame(
    const cv::Mat& frame, 
    const std::vector<ObjectDetectorProcessor::Detection>& detections) {
    
    if (!running_ || frame.empty() || !tracker_) {
        return {frame, {}};
    }
    
    try {
        // Convert detections to ByteTracker format
        std::vector<Object> byteTrackObjects;
        
        // Create a mapping from detection index to class name for later use
        std::map<int, std::string> detectionClassMap;
        for (int i = 0; i < detections.size(); i++) {
            auto obj = convertDetection(detections[i]);
            byteTrackObjects.push_back(obj);
            detectionClassMap[i] = detections[i].className;
        }
        
        // Update tracker with new detections
        auto trackResult = tracker_->update(byteTrackObjects);
        
        // Use the class mutex instead of a static mutex
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Keep track of which IDs are present in the current frame
        std::unordered_set<int> currentTrackIds;
        
        // Convert ByteTracker results to TrackedObject format
        std::vector<TrackedObject> trackedObjects;
        for (const auto& track : trackResult) {
            TrackedObject obj;
            obj.trackId = track.track_id;
            currentTrackIds.insert(obj.trackId);
            
            // Get track rectangle and bounding box for all tracks
            auto trackRect = track.tlwh;
            cv::Rect trackBbox(
                static_cast<int>(trackRect[0]),
                static_cast<int>(trackRect[1]),
                static_cast<int>(trackRect[2]),
                static_cast<int>(trackRect[3])
            );
            
            // Check if this track already has a persistent class assigned
            if (trackClassMap_.find(obj.trackId) != trackClassMap_.end()) {
                // Use the persistent class name
                obj.className = trackClassMap_[obj.trackId];
            } else {
                // New track - determine class from current detections
                float bestOverlap = 0.0f;
                int bestDetectionIndex = -1;
                
                for (int i = 0; i < detections.size(); i++) {
                    const auto& det = detections[i];
                    cv::Rect detBbox = det.bbox;
                    
                    // Calculate intersection over union (IoU)
                    cv::Rect intersection = trackBbox & detBbox;
                    if (intersection.area() > 0) {
                        float unionArea = trackBbox.area() + detBbox.area() - intersection.area();
                        float overlap = static_cast<float>(intersection.area()) / unionArea;
                        
                        if (overlap > bestOverlap) {
                            bestOverlap = overlap;
                            bestDetectionIndex = i;
                        }
                    }
                }
                
                // Assign class name for new track
                if (bestDetectionIndex >= 0 && bestOverlap > 0.3f) {
                    obj.className = detectionClassMap[bestDetectionIndex];
                    // Store this class as persistent for this track
                    trackClassMap_[obj.trackId] = obj.className;
                } else {
                    obj.className = "unknown";
                    trackClassMap_[obj.trackId] = obj.className;
                }
                
                // Initialize confidence tracking for new track
                trackClassConfidence_[obj.trackId] = {};
            }
            
            obj.confidence = track.score;
            obj.bbox = trackBbox;
            obj.age = track.frame_id - track.start_frame;
            
            // Add center point to the trajectory history - always calculate this
            cv::Point center(
                static_cast<int>(trackRect[0] + trackRect[2]/2),
                static_cast<int>(trackRect[1] + trackRect[3]/2)
            );
            
            // Initialize trajectory if this is a new track ID
            if (trajectoryHistory_.find(obj.trackId) == trajectoryHistory_.end()) {
                trajectoryHistory_[obj.trackId] = std::vector<cv::Point>();
                // Also initialize last known position information
                lastKnownPositions_[obj.trackId] = std::make_pair(center, obj.className);
            }
            
            // Additional verification: check if this is a new object with a reused ID
            // by comparing the current class and position with the last known values
            auto& lastKnownPos = lastKnownPositions_[obj.trackId];
            bool isClassChanged = (lastKnownPos.second != obj.className);
            
            if (!trajectoryHistory_[obj.trackId].empty()) {
                cv::Point lastPoint = trajectoryHistory_[obj.trackId].back();
                
                // Calculate distance between current position and last position
                double distance = std::sqrt(std::pow(center.x - lastPoint.x, 2) + std::pow(center.y - lastPoint.y, 2));
                
                // Define maximum allowed distance based on frame width
                double maxAllowedDistance = frame.cols * maxAllowedDistanceRatio_;
                
                // If distance is too large or class has changed, this is likely a ID reuse
                if (distance > maxAllowedDistance || isClassChanged) {
                    // Clear trajectory for this ID and start fresh
                    trajectoryHistory_[obj.trackId].clear();
                }
            }
            
            // Update last known position information
            lastKnownPositions_[obj.trackId] = std::make_pair(center, obj.className);
            
            // Add current center to the trajectory
            trajectoryHistory_[obj.trackId].push_back(center);
            
            // Limit trajectory length
            if (trajectoryHistory_[obj.trackId].size() > trajectoryMaxLength_) {
                trajectoryHistory_[obj.trackId].erase(
                    trajectoryHistory_[obj.trackId].begin(),
                    trajectoryHistory_[obj.trackId].begin() + 
                    (trajectoryHistory_[obj.trackId].size() - trajectoryMaxLength_)
                );
            }
            
            // Assign trajectory points to the tracked object
            obj.trajectory = trajectoryHistory_[obj.trackId];
            
            trackedObjects.push_back(obj);
        }
        
        // Increment disappear counter for tracks not in current frame
        for (auto it = trackDisappearCounter_.begin(); it != trackDisappearCounter_.end(); ) {
            if (currentTrackIds.find(it->first) == currentTrackIds.end()) {
                // Track not present in current frame
                it->second++;
                
                // Remove trajectory data if track has been missing for too long
                if (it->second > trajectoryCleanupThreshold_) {
                    trajectoryHistory_.erase(it->first);
                    lastKnownPositions_.erase(it->first);
                    // Clean up persistent class mapping for disappeared tracks
                    trackClassMap_.erase(it->first);
                    trackClassConfidence_.erase(it->first);
                    it = trackDisappearCounter_.erase(it);
                } else {
                    ++it;
                }
            } else {
                // Reset counter for tracks present in current frame
                it->second = 0;
                ++it;
            }
        }
        
        // Initialize disappear counter for new tracks
        for (int trackId : currentTrackIds) {
            if (trackDisappearCounter_.find(trackId) == trackDisappearCounter_.end()) {
                trackDisappearCounter_[trackId] = 0;
            }
        }
        
        // Create a copy of the frame for drawing
        cv::Mat outputFrame = frame.clone();
        
        // Draw tracking information if enabled
        if (drawTracking_) {
            outputFrame = drawTracking(outputFrame, trackedObjects);
        }
        
        // Update statistics
        processedFrames_++;
        activeTrackedObjects_ = trackedObjects.size();
        
        // Update total tracked objects count 
        // Note: The new implementation doesn't provide a direct way to get the next ID
        // so we'll use a simple counter based on the maximum track ID seen
        int maxTrackId = 0;
        for (const auto& track : trackResult) {
            maxTrackId = std::max(maxTrackId, track.track_id);
        }
        totalTrackedObjects_ = maxTrackId;
        
        return {outputFrame, trackedObjects};
        
    } catch (const std::exception& e) {
        lastError_ = std::string("Processing error: ") + e.what();
        std::cerr << lastError_ << std::endl;
        return {frame, {}};
    }
}

cv::Mat ObjectTrackerProcessor::drawTracking(
    const cv::Mat& frame, 
    const std::vector<TrackedObject>& trackedObjects) {
    
    cv::Mat outputFrame = frame.clone();
    
    // Professional color scheme for common object classes
    static const std::map<std::string, cv::Scalar> colorMap = {
        {"person", cv::Scalar(0, 165, 255)},     // Orange
        {"car", cv::Scalar(0, 255, 255)},        // Yellow
        {"truck", cv::Scalar(250, 170, 30)},     // Blue
        {"bicycle", cv::Scalar(0, 255, 0)},      // Green
        {"motorcycle", cv::Scalar(255, 0, 0)},   // Blue
        {"bus", cv::Scalar(255, 191, 0)},        // Deep Sky Blue
        {"dog", cv::Scalar(180, 105, 255)},      // Pink
        {"cat", cv::Scalar(255, 0, 255)},        // Magenta
        // Add more class-specific colors as needed
    };
    
    for (const auto& obj : trackedObjects) {
        // Get color based on class name (consistent color per class)
        cv::Scalar color;
        
        // Check if class has a predefined color
        auto colorIt = colorMap.find(obj.className);
        if (colorIt != colorMap.end()) {
            color = colorIt->second;
        } else {
            // Generate a visually pleasing color based on class name hash
            color = generateColorFromClassName(obj.className);
        }
        
        // Draw bounding box with double-line effect
        cv::rectangle(outputFrame, obj.bbox, color, 2);
        
        // Draw semi-transparent fill if enabled
        if (drawSemiTransparentBoxes_) {
            cv::Mat overlay;
            outputFrame.copyTo(overlay);
            cv::rectangle(overlay, obj.bbox, color, cv::FILLED);
            cv::addWeighted(overlay, 0.1, outputFrame, 0.9, 0, outputFrame);
        }
        
        // Draw corner markers
        int markerLength = std::min(30, std::min(obj.bbox.width, obj.bbox.height) / 4);
        int thickness = 2;
        
        // Top-left corner
        cv::line(outputFrame, cv::Point(obj.bbox.x, obj.bbox.y), 
                cv::Point(obj.bbox.x + markerLength, obj.bbox.y), color, thickness);
        cv::line(outputFrame, cv::Point(obj.bbox.x, obj.bbox.y), 
                cv::Point(obj.bbox.x, obj.bbox.y + markerLength), color, thickness);
        
        // Top-right corner
        cv::line(outputFrame, cv::Point(obj.bbox.x + obj.bbox.width, obj.bbox.y), 
                cv::Point(obj.bbox.x + obj.bbox.width - markerLength, obj.bbox.y), color, thickness);
        cv::line(outputFrame, cv::Point(obj.bbox.x + obj.bbox.width, obj.bbox.y), 
                cv::Point(obj.bbox.x + obj.bbox.width, obj.bbox.y + markerLength), color, thickness);
        
        // Bottom-left corner
        cv::line(outputFrame, cv::Point(obj.bbox.x, obj.bbox.y + obj.bbox.height), 
                cv::Point(obj.bbox.x + markerLength, obj.bbox.y + obj.bbox.height), color, thickness);
        cv::line(outputFrame, cv::Point(obj.bbox.x, obj.bbox.y + obj.bbox.height), 
                cv::Point(obj.bbox.x, obj.bbox.y + obj.bbox.height - markerLength), color, thickness);
        
        // Bottom-right corner
        cv::line(outputFrame, cv::Point(obj.bbox.x + obj.bbox.width, obj.bbox.y + obj.bbox.height), 
                cv::Point(obj.bbox.x + obj.bbox.width - markerLength, obj.bbox.y + obj.bbox.height), color, thickness);
        cv::line(outputFrame, cv::Point(obj.bbox.x + obj.bbox.width, obj.bbox.y + obj.bbox.height), 
                cv::Point(obj.bbox.x + obj.bbox.width, obj.bbox.y + obj.bbox.height - markerLength), color, thickness);
        
        // Prepare label for the object
        std::string label = obj.className;
        
        // Add track ID if enabled (but not Age as requested)
        if (drawTrackId_) {
            label += " ID:" + std::to_string(obj.trackId);
        }
        
        // Draw label background
        int baseLine;
        cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_DUPLEX, labelFontScale_, 1, &baseLine);
        
        // Position label at top of bounding box with padding
        int padding = 5;
        cv::Point textOrg(obj.bbox.x, obj.bbox.y - padding);
        cv::Rect labelBg(
            textOrg.x - padding,
            textOrg.y - labelSize.height - padding,
            labelSize.width + (2 * padding),
            labelSize.height + (2 * padding)
        );
        
        // Draw fully opaque background using the same color as the bounding box
        cv::rectangle(outputFrame, labelBg, color, cv::FILLED);
        
        // Draw white text
        cv::putText(outputFrame, label, 
                   cv::Point(labelBg.x + padding, labelBg.y + labelSize.height),
                   cv::FONT_HERSHEY_DUPLEX, labelFontScale_, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
        
        // Calculate center point of bounding box for consistent centroid location
        cv::Point center(
            obj.bbox.x + obj.bbox.width / 2,
            obj.bbox.y + obj.bbox.height / 2
        );
        
        // Always draw centroid (regardless of trajectory setting)
        // Draw a professional-looking circular centroid
        int centerRadius = 4;
        
        // First draw a slightly larger white circle as border/outline
        cv::circle(outputFrame, center, centerRadius + 1, cv::Scalar(255, 255, 255), -1, cv::LINE_AA);
        
        // Then draw the inner circle with the object's color
        cv::circle(outputFrame, center, centerRadius, color, -1, cv::LINE_AA);
        
        // Draw track trajectory if enabled and available
        if (drawTrackTrajectory_ && obj.trajectory.size() > 1) {
            // Draw trajectory line with gradient effect
            for (size_t i = 1; i < obj.trajectory.size(); i++) {
                // Calculate alpha for gradient effect - newer points have higher alpha (more visible)
                float alpha = static_cast<float>(i) / obj.trajectory.size();
                // Create a darker version of the color for older points
                cv::Scalar lineColor = color * alpha;
                
                // Draw line segment
                cv::line(outputFrame, obj.trajectory[i-1], obj.trajectory[i], lineColor, 2, cv::LINE_AA);
                
                // Draw small circles at trajectory points (every few points to avoid clutter)
                if (i % 5 == 0 && i < obj.trajectory.size() - 1) { 
                    cv::circle(outputFrame, obj.trajectory[i], 1, lineColor, cv::FILLED, cv::LINE_AA);
                }
            }
        }
    }
    
    return outputFrame;
}

} // namespace tapi 
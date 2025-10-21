#include "geometry/polygon_zone.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace tapi {

// Constructor
PolygonZone::PolygonZone(const std::string& id,
                       const std::vector<cv::Point2f>& polygon,
                       const std::string& streamId,
                       const std::vector<std::string>& triggeringAnchorStrings,
                       const std::vector<std::string>& triggeringClasses)
    : id_(id),
      streamId_(streamId),
      polygon_(polygon),
      inCount_(0),
      outCount_(0),
      initialized_(false) {
          
    // Set up triggering anchors
    if (triggeringAnchorStrings.empty()) {
        // Default to bottom center
        triggeringAnchors_ = {
            Position::BOTTOM_CENTER
        };
    } else {
        // Convert string anchors to Position enum values
        for (const auto& anchorStr : triggeringAnchorStrings) {
            triggeringAnchors_.push_back(StringToPosition(anchorStr));
        }
        
        // If no valid anchors were provided, use default
        if (triggeringAnchors_.empty()) {
            triggeringAnchors_ = {
                Position::BOTTOM_CENTER
            };
        }
    }
    
    // Set up triggering classes
    if (triggeringClasses.empty()) {
        // Default to all classes (empty means accept all)
        triggeringClasses_ = {};
    } else {
        triggeringClasses_ = triggeringClasses;
    }
}

// Initialize the polygon zone
bool PolygonZone::initialize() {
    if (initialized_) {
        return true;
    }
    
    try {
        // Calculate the frame resolution based on the polygon
        int maxX = 0, maxY = 0;
        for (const auto& point : polygon_) {
            maxX = std::max(maxX, static_cast<int>(point.x));
            maxY = std::max(maxY, static_cast<int>(point.y));
        }
        frameResolution_ = cv::Size(maxX + 2, maxY + 2);
        
        // Create the mask for point-in-polygon testing
        createMask();
        
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize PolygonZone: " << e.what() << std::endl;
        return false;
    }
}

// Reset the polygon zone
void PolygonZone::reset() {
    // Reset all counts and state
    std::lock_guard<std::mutex> lock(countMutex_);
    inCount_ = 0;
    outCount_ = 0;
    inCountPerClass_.clear();
    outCountPerClass_.clear();
    zoneStateHistory_.clear();
    classIdToName_.clear();
}

// Get polygon vertices
const std::vector<cv::Point2f>& PolygonZone::getPolygon() const {
    return polygon_;
}

// Set polygon vertices
void PolygonZone::setPolygon(const std::vector<cv::Point2f>& polygon) {

    polygon_ = polygon;
    
    // Recalculate frame resolution and update mask
    int maxX = 0, maxY = 0;
    for (const auto& point : polygon_) {
        maxX = std::max(maxX, static_cast<int>(point.x));
        maxY = std::max(maxY, static_cast<int>(point.y));
    }
    frameResolution_ = cv::Size(maxX + 2, maxY + 2);
        
    // Create new mask
    createMask();
}

// Update polygon zone configuration
bool PolygonZone::updateConfig(const nlohmann::json& config) {
    try {
        // Check for polygon changes
        if (config.contains("polygon") && config["polygon"].is_array()) {
            std::vector<cv::Point2f> newPolygon;
            for (const auto& point : config["polygon"]) {
                if (point.contains("x") && point.contains("y")) {
                    // Parse as float and use directly
                    float x = point["x"].get<float>();
                    float y = point["y"].get<float>();
                    
                    // Store value directly without scaling
                    newPolygon.emplace_back(x, y);
                }
            }
            
            if (!newPolygon.empty()) {
                setPolygon(newPolygon);
            }
        }
        
        // Check for triggering anchors change
        if (config.contains("triggering_anchors") && config["triggering_anchors"].is_array()) {
            std::vector<Position> newAnchors;
            for (const auto& anchor : config["triggering_anchors"]) {
                if (anchor.is_string()) {
                    newAnchors.push_back(StringToPosition(anchor.get<std::string>()));
                }
            }
            
            if (!newAnchors.empty()) {
                std::lock_guard<std::mutex> lock(countMutex_);
                triggeringAnchors_ = newAnchors;
            }
        }
        
        // Check for triggering classes change
        if (config.contains("triggering_classes") && config["triggering_classes"].is_array()) {
            std::vector<std::string> newTriggeringClasses;
            for (const auto& className : config["triggering_classes"]) {
                if (className.is_string()) {
                    newTriggeringClasses.push_back(className.get<std::string>());
                }
            }
            
            std::lock_guard<std::mutex> lock(countMutex_);
            triggeringClasses_ = newTriggeringClasses;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error updating polygon zone config: " << e.what() << std::endl;
        return false;
    }
}

// Process tracks to detect zone entry/exit
std::vector<Event> PolygonZone::processTracks(const std::vector<Track>& tracks) {
    std::vector<Event> zoneEvents;
    
    if (!initialized_) {
        if (!initialize()) {
            return zoneEvents;
        }
    }
    
    if (tracks.empty()) {
        return zoneEvents;
    }
    
    // Convert tracks to detections for processing, filtering by class
    std::vector<Detection> detections;
    std::vector<size_t> trackIndices; // Keep track of original track indices
    
    for (size_t i = 0; i < tracks.size(); i++) {
        const auto& track = tracks[i];
        
        // Filter by triggering classes if specified
        if (!triggeringClasses_.empty()) {
            bool classMatches = false;
            for (const auto& triggeringClass : triggeringClasses_) {
                if (track.className == triggeringClass) {
                    classMatches = true;
                    break;
                }
            }
            if (!classMatches) {
                continue; // Skip this track as it's not in the triggering classes
            }
        }
        
        Detection det;
        det.bbox = track.bbox;
        det.confidence = track.confidence;
        det.classId = track.classId;
        det.className = track.className;
        det.timestamp = track.timestamp;
        detections.push_back(det);
        trackIndices.push_back(i);
    }
    
    // If no detections remain after filtering, return empty events
    if (detections.empty()) {
        return zoneEvents;
    }
    
    // Compute which detections are in the zone
    std::vector<bool> inZone = computeAnchorsInZone(detections);
    
    // Update class ID to name mapping
    updateClassIdToName(detections);
    
    // Check for zone entry/exit events
    for (size_t i = 0; i < detections.size(); i++) {
        size_t originalTrackIndex = trackIndices[i];
        const auto& track = tracks[originalTrackIndex];
        
        int trackId = track.trackId;
        bool currentState = inZone[i];
        
        // Get previous state from history
        bool previousState = false;
        bool stateChanged = false;
        
        auto it = zoneStateHistory_.find(trackId);
        if (it != zoneStateHistory_.end() && !it->second.empty()) {
            previousState = it->second.back();
            stateChanged = currentState != previousState;
        } else {
            // No history for this track, so initialize
            zoneStateHistory_[trackId] = std::deque<bool>();
        }
        
        // Update state history
        zoneStateHistory_[trackId].push_back(currentState);
        
        // Keep history manageable
        if (zoneStateHistory_[trackId].size() > 10) {
            zoneStateHistory_[trackId].pop_front();
        }
        
        // Skip if state hasn't changed
        if (!stateChanged && it != zoneStateHistory_.end()) {
            continue;
        }
        
        // If this is a new track or state changed, check for events
        if (currentState && (it == zoneStateHistory_.end() || stateChanged)) {
            // Object entered the zone
            int classId = 0;
            try {
                classId = std::stoi(track.classId);
            } catch (...) {
                classId = 0; // Default if not a number
            }
            
            // Create zone entry event
            Event zoneEvent;
            zoneEvent.timestamp = getCurrentTimestamp();
            zoneEvent.objectId = std::to_string(trackId);
            zoneEvent.className = track.className;
            
            // Get center of bounding box for location
            cv::Rect bbox = track.bbox;
            zoneEvent.location = cv::Point(
                bbox.x + bbox.width / 2,
                bbox.y + bbox.height / 2
            );
            
            // Set zone ID
            zoneEvent.zoneId = id_;
            
            // Lock for count operations
            {
                std::lock_guard<std::mutex> lock(countMutex_);
                
                // Object entered the zone
                inCount_++;
                inCountPerClass_[classId]++;
                
                // Create zone event
                zoneEvent.type = "zone_entry";
                zoneEvent.metadata["direction"] = "in";
                zoneEvent.metadata["in_count"] = std::to_string(inCount_);
                zoneEvent.metadata["out_count"] = std::to_string(outCount_);
                zoneEvent.metadata["current_count"] = std::to_string(inCount_ - outCount_);
                zoneEvents.push_back(zoneEvent);
            } // Lock released here
        } else if (!currentState && stateChanged) {
            // Object exited the zone
            int classId = 0;
            try {
                classId = std::stoi(track.classId);
            } catch (...) {
                classId = 0; // Default if not a number
            }
            
            // Create zone exit event
            Event zoneEvent;
            zoneEvent.timestamp = getCurrentTimestamp();
            zoneEvent.objectId = std::to_string(trackId);
            zoneEvent.className = track.className;
            
            // Get center of bounding box for location
            cv::Rect bbox = track.bbox;
            zoneEvent.location = cv::Point(
                bbox.x + bbox.width / 2,
                bbox.y + bbox.height / 2
            );
            
            // Set zone ID
            zoneEvent.zoneId = id_;
            
            // Lock for count operations
            {
                std::lock_guard<std::mutex> lock(countMutex_);
                
                // Object exited the zone
                outCount_++;
                outCountPerClass_[classId]++;
                
                // Create zone event
                zoneEvent.type = "zone_exit";
                zoneEvent.metadata["direction"] = "out";
                zoneEvent.metadata["in_count"] = std::to_string(inCount_);
                zoneEvent.metadata["out_count"] = std::to_string(outCount_);
                zoneEvent.metadata["current_count"] = std::to_string(inCount_ - outCount_);
                zoneEvents.push_back(zoneEvent);
            } // Lock released here
        }
    }
    
    if (!zoneEvents.empty()) {
    }
    
    return zoneEvents;
}

// Create mask from polygon for efficient point-in-polygon testing
void PolygonZone::createMask() {

    mask_ = cv::Mat::zeros(frameResolution_, CV_8UC1);
    
    // Convert floating point polygon to integer polygon for drawing
    std::vector<cv::Point> intPolygon;
    for (const auto& pt : polygon_) {
        intPolygon.push_back(cv::Point(static_cast<int>(pt.x), static_cast<int>(pt.y)));
    }
    
    std::vector<std::vector<cv::Point>> polygons = {intPolygon};
    cv::fillPoly(mask_, polygons, cv::Scalar(255));
    
}

// Check if detections are in the zone
std::vector<bool> PolygonZone::computeAnchorsInZone(const std::vector<Detection>& detections) const {
    std::vector<bool> isInZone(detections.size(), false);
    
    if (detections.empty() || !initialized_) {
        return isInZone;
    }
    

    
    // Process each detection
    for (size_t i = 0; i < detections.size(); i++) {
        const auto& bbox = detections[i].bbox;
        
        
        // Get anchor points for this bounding box
        std::vector<cv::Point> anchorPoints;
        for (const auto& anchorPos : triggeringAnchors_) {
            switch (anchorPos) {
                case Position::TOP_LEFT:
                    anchorPoints.emplace_back(bbox.x, bbox.y);
                    break;
                case Position::TOP_RIGHT:
                    anchorPoints.emplace_back(bbox.x + bbox.width, bbox.y);
                    break;
                case Position::BOTTOM_LEFT:
                    anchorPoints.emplace_back(bbox.x, bbox.y + bbox.height);
                    break;
                case Position::BOTTOM_RIGHT:
                    anchorPoints.emplace_back(bbox.x + bbox.width, bbox.y + bbox.height);
                    break;
                case Position::CENTER:
                    anchorPoints.emplace_back(bbox.x + bbox.width / 2, bbox.y + bbox.height / 2);
                    break;
                case Position::TOP_CENTER:
                    anchorPoints.emplace_back(bbox.x + bbox.width / 2, bbox.y);
                    break;
                case Position::BOTTOM_CENTER:
                    anchorPoints.emplace_back(bbox.x + bbox.width / 2, bbox.y + bbox.height);
                    break;
                case Position::CENTER_LEFT:
                    anchorPoints.emplace_back(bbox.x, bbox.y + bbox.height / 2);
                    break;
                case Position::CENTER_RIGHT:
                    anchorPoints.emplace_back(bbox.x + bbox.width, bbox.y + bbox.height / 2);
                    break;
                default:
                    // For CENTER_OF_MASS or any other case, use center
                    anchorPoints.emplace_back(bbox.x + bbox.width / 2, bbox.y + bbox.height / 2);
                    break;
            }
        }
        
        // Check if all anchor points are inside the polygon
        bool allAnchorsInZone = true;
        for (size_t j = 0; j < anchorPoints.size(); ++j) {
            const auto& point = anchorPoints[j];
            
            // Check if point is within image boundaries
            if (point.x < 0 || point.y < 0 || 
                point.x >= frameResolution_.width || point.y >= frameResolution_.height) {
                allAnchorsInZone = false;
                break;
            }
            
            // Check if point is inside polygon using the mask
            try {
                bool insidePolygon = mask_.at<uchar>(point.y, point.x) > 0;
                
                if (!insidePolygon) {
                    allAnchorsInZone = false;
                    break;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error checking if point is inside polygon: " << e.what() 
                         << " at point (" << point.x << "," << point.y << ")" << std::endl;
                allAnchorsInZone = false;
                break;
            }
        }
        
        isInZone[i] = allAnchorsInZone;
        
    }
    
    return isInZone;
}

// Update class ID to name mapping
void PolygonZone::updateClassIdToName(const std::vector<Detection>& detections) {
    for (const auto& detection : detections) {
        try {
            int classId = std::stoi(detection.classId);
            classIdToName_[classId] = detection.className;
        } catch (...) {
            // Skip if class ID is not an integer
        }
    }
}

// Get list of triggering anchors as strings
std::vector<std::string> PolygonZone::getTriggeringAnchors() const {
    std::vector<std::string> anchorStrings;
    for (const auto& anchorPos : triggeringAnchors_) {
        anchorStrings.push_back(PositionToString(anchorPos));
    }
    return anchorStrings;
}

// Get list of triggering classes
std::vector<std::string> PolygonZone::getTriggeringClasses() const {
    return triggeringClasses_;
}

// PolygonZoneAnnotator implementation

PolygonZoneAnnotator::PolygonZoneAnnotator(
    const PolygonZone& zone,
    const cv::Scalar& color,
    int thickness,
    const cv::Scalar& textColor,
    float textScale,
    int textThickness,
    int textPadding,
    bool displayCount,
    float opacity
) : zone_(zone),
    color_(color),
    thickness_(thickness),
    textColor_(textColor),
    textScale_(textScale),
    textThickness_(textThickness),
    textPadding_(textPadding),
    font_(cv::FONT_HERSHEY_SIMPLEX),
    displayCount_(displayCount),
    opacity_(std::max(0.0f, std::min(1.0f, opacity))) {
    
    // Get the polygon from the zone (now in cv::Point2f)
    const auto& polygon2f = zone.getPolygon();
    
    // Convert to cv::Point for drawing
    std::vector<cv::Point> polygon;
    for (const auto& pt : polygon2f) {
        polygon.push_back(cv::Point(static_cast<int>(pt.x), static_cast<int>(pt.y)));
    }
    
    // Calculate polygon center
    center_ = getPolygonCenter(polygon);
}

// Annotate an image with the polygon zone
cv::Mat PolygonZoneAnnotator::annotate(cv::Mat scene, const std::string& label) {
    cv::Mat annotatedFrame = scene.clone();
    
    // Get polygon vertices from the zone (now in cv::Point2f)
    const auto& polygon2f = zone_.getPolygon();
    
    // Convert to cv::Point for drawing
    std::vector<cv::Point> polygon;
    for (const auto& pt : polygon2f) {
        polygon.push_back(cv::Point(static_cast<int>(pt.x), static_cast<int>(pt.y)));
    }
    
    // Draw filled polygon with opacity if needed
    if (opacity_ > 0.0f) {
        cv::Mat overlay = scene.clone();
        std::vector<std::vector<cv::Point>> polygons = {polygon};
        cv::fillPoly(overlay, polygons, color_);
        
        // Blend with original using opacity
        cv::addWeighted(overlay, opacity_, scene, 1.0 - opacity_, 0, annotatedFrame);
    }
    
    // Draw polygon outline
    std::vector<std::vector<cv::Point>> contours = {polygon};
    cv::polylines(annotatedFrame, contours, true, color_, thickness_);
    
    // Draw count or label if enabled
    if (displayCount_) {
        std::string text = label.empty() ? std::to_string(zone_.getCurrentCount()) : label;
        
        // Calculate text size for the background rectangle
        int baseLine;
        cv::Size textSize = cv::getTextSize(text, font_, textScale_, textThickness_, &baseLine);
        
        // Draw background rectangle
        cv::Rect rect(
            center_.x - textSize.width / 2 - textPadding_,
            center_.y - textSize.height / 2 - textPadding_,
            textSize.width + 2 * textPadding_,
            textSize.height + 2 * textPadding_
        );
        cv::rectangle(annotatedFrame, rect, color_, -1);
        
        // Draw text
        cv::putText(
            annotatedFrame,
            text,
            cv::Point(
                center_.x - textSize.width / 2,
                center_.y + textSize.height / 2
            ),
            font_,
            textScale_,
            textColor_,
            textThickness_
        );
    }
    
    return annotatedFrame;
}

// Calculate polygon center
cv::Point PolygonZoneAnnotator::getPolygonCenter(const std::vector<cv::Point>& polygon) const {
    cv::Moments m = cv::moments(polygon);
    if (m.m00 != 0) {
        return cv::Point(static_cast<int>(m.m10 / m.m00), static_cast<int>(m.m01 / m.m00));
    } else {
        // Fallback: calculate average of all points
        int sumX = 0, sumY = 0;
        for (const auto& p : polygon) {
            sumX += p.x;
            sumY += p.y;
        }
        return cv::Point(sumX / polygon.size(), sumY / polygon.size());
    }
}

} // namespace tapi 
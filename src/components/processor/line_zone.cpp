#include "geometry/line_zone.h"
#include "vision_manager.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace tapi {

LineZone::LineZone(const std::string& id,
                 float startX, float startY,
                 float endX, float endY,
                 const std::string& streamId,
                 int minCrossingThreshold,
                 const std::vector<std::string>& triggeringAnchorStrings)
    : id_(id),
      streamId_(streamId),
      inCount_(0),
      outCount_(0),
      initialized_(false) {
    
    std::cout << "LineZone constructor called for ID: " << id << ", stream: " << streamId << std::endl;
    
    // Set up line and coordinates
    startPoint_ = Point(startX, startY);
    endPoint_ = Point(endX, endY);
    line_ = Vector(startPoint_, endPoint_);
    
    // Set minimum crossing threshold
    minCrossingThreshold_ = std::max(1, minCrossingThreshold);
    
    // Set up triggering anchors
    if (triggeringAnchorStrings.empty()) {
        // Default to all four corners
        triggeringAnchors_ = {
            Position::TOP_LEFT,
            Position::TOP_RIGHT,
            Position::BOTTOM_LEFT,
            Position::BOTTOM_RIGHT
        };
    } else {
        // Convert string anchors to Position enum values
        for (const auto& anchorStr : triggeringAnchorStrings) {
            try {
                triggeringAnchors_.push_back(StringToPosition(anchorStr));
            } catch (const std::exception& e) {
                std::cerr << "Invalid triggering anchor: " << anchorStr << std::endl;
            }
        }
        
        // If no valid anchors were provided, use defaults
        if (triggeringAnchors_.empty()) {
            triggeringAnchors_ = {
                Position::TOP_LEFT,
                Position::TOP_RIGHT,
                Position::BOTTOM_LEFT,
                Position::BOTTOM_RIGHT
            };
        }
    }
    
    // Set up history length
    crossingHistoryLength_ = std::max(2, minCrossingThreshold_ + 1);
    
    std::cout << "LineZone created with ID=" << id_ << ", start=(" << startX << "," << startY 
              << "), end=(" << endX << "," << endY 
              << "), min_threshold=" << minCrossingThreshold_
              << ", history_length=" << crossingHistoryLength_ << std::endl;
}

// Get line endpoints
const std::pair<Point, Point> LineZone::getLineEndpoints() const {
    return std::make_pair(startPoint_, endPoint_);
}

bool LineZone::initialize() {
    if (initialized_) {
        return true;
    }
    
    try {
        // Calculate region of interest limits
        auto limits = calculateRegionOfInterestLimits(line_);
        startRegionLimit_ = limits.first;
        endRegionLimit_ = limits.second;
        
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize LineZone: " << e.what() << std::endl;
        return false;
    }
}

void LineZone::reset() {
    // Reset all counts and state
    std::lock_guard<std::mutex> lock(countMutex_);
    inCount_ = 0;
    outCount_ = 0;
    inCountPerClass_.clear();
    outCountPerClass_.clear();
    crossingStateHistory_.clear();
    classIdToName_.clear();
}

bool LineZone::updateConfig(const std::map<std::string, nlohmann::json>& newConfig) {
    try {
        // Check if line points were changed
        bool lineChanged = false;
        
        // Check for explicit start/end coordinates
        if (newConfig.find("start_x") != newConfig.end() || 
            newConfig.find("start_y") != newConfig.end()) {
            float startX = startPoint_.x;
            float startY = startPoint_.y;
            
            auto startXIt = newConfig.find("start_x");
            if (startXIt != newConfig.end()) {
                if (startXIt->second.is_number()) {
                    startX = startXIt->second.get<float>();
                } else {
                    std::cerr << "LineZone::updateConfig - Invalid start_x type for zone " << id_ << std::endl;
                    return false;
                }
            }
            
            auto startYIt = newConfig.find("start_y");
            if (startYIt != newConfig.end()) {
                if (startYIt->second.is_number()) {
                    startY = startYIt->second.get<float>();
                } else {
                    std::cerr << "LineZone::updateConfig - Invalid start_y type for zone " << id_ << std::endl;
                    return false;
                }
            }
            
            startPoint_ = Point(startX, startY);
            lineChanged = true;
        }
        
        if (newConfig.find("end_x") != newConfig.end() || 
            newConfig.find("end_y") != newConfig.end()) {
            float endX = endPoint_.x;
            float endY = endPoint_.y;
            
            auto endXIt = newConfig.find("end_x");
            if (endXIt != newConfig.end()) {
                if (endXIt->second.is_number()) {
                    endX = endXIt->second.get<float>();
                } else {
                    std::cerr << "LineZone::updateConfig - Invalid end_x type for zone " << id_ << std::endl;
                    return false;
                }
            }
            
            auto endYIt = newConfig.find("end_y");
            if (endYIt != newConfig.end()) {
                if (endYIt->second.is_number()) {
                    endY = endYIt->second.get<float>();
                } else {
                    std::cerr << "LineZone::updateConfig - Invalid end_y type for zone " << id_ << std::endl;
                    return false;
                }
            }
            
            endPoint_ = Point(endX, endY);
            lineChanged = true;
        }
        
        if (lineChanged) {
            std::cout << "LineZone " << id_ << " updated from (" 
                      << startPoint_.x << "," << startPoint_.y << ") to ("
                      << endPoint_.x << "," << endPoint_.y << ")" << std::endl;
            
            line_ = Vector(startPoint_, endPoint_);
            auto limits = calculateRegionOfInterestLimits(line_);
            startRegionLimit_ = limits.first;
            endRegionLimit_ = limits.second;
        }
        
        // Check if minimum crossing threshold was changed
        auto thresholdIt = newConfig.find("minimum_crossing_threshold");
        if (thresholdIt != newConfig.end()) {
            if (thresholdIt->second.is_number()) {
                int threshold = thresholdIt->second.get<int>();
                if (threshold > 0) {
                    minCrossingThreshold_ = threshold;
                } else {
                    std::cerr << "LineZone::updateConfig - Invalid minimum_crossing_threshold value: " 
                              << threshold << " for zone " << id_ << std::endl;
                }
            } else {
                std::cerr << "LineZone::updateConfig - Invalid minimum_crossing_threshold type for zone " 
                         << id_ << std::endl;
            }
        }
        
        // Check if triggering anchors were changed
        auto anchorsIt = newConfig.find("triggering_anchors");
        if (anchorsIt != newConfig.end() && anchorsIt->second.is_array()) {
            std::vector<std::string> triggeringAnchorStrings;
            for (const auto& anchor : anchorsIt->second) {
                if (anchor.is_string()) {
                    triggeringAnchorStrings.push_back(anchor.get<std::string>());
                }
            }
            
            // Only update if we have valid anchors
            if (!triggeringAnchorStrings.empty()) {
                // Convert string anchors to Position enum values
                std::vector<Position> newAnchors;
                for (const auto& anchorStr : triggeringAnchorStrings) {
                    try {
                        newAnchors.push_back(StringToPosition(anchorStr));
                    } catch (const std::exception& e) {
                        std::cerr << "Invalid triggering anchor: " << anchorStr << std::endl;
                    }
                }
                
                // If we have valid anchors, update the triggering anchors
                if (!newAnchors.empty()) {
                    triggeringAnchors_ = newAnchors;
                    std::cout << "LineZone " << id_ << ": Updated triggering anchors, count=" 
                              << triggeringAnchors_.size() << std::endl;
                } else {
                    std::cerr << "LineZone::updateConfig - No valid triggering anchors could be parsed from input" 
                             << " for zone " << id_ << ", keeping existing ones" << std::endl;
                }
            } else {
                std::cerr << "LineZone::updateConfig - No valid triggering anchors specified for zone " 
                         << id_ << ", keeping existing ones" << std::endl;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error updating line zone config: " << e.what() << std::endl;
        return false;
    }
}

std::vector<Event> LineZone::processTracks(const std::vector<Track>& tracks) {
    std::vector<Event> crossingEvents;
    
    if (!initialized_) {
        std::cerr << "LineZone not initialized, trying to initialize now..." << std::endl;
        if (!initialize()) {
            std::cerr << "Failed to initialize LineZone" << std::endl;
            return crossingEvents;
        }
    }
    
    if (tracks.empty()) {
        return crossingEvents; // No tracks to process
    }
    
    // Convert tracks to detections for processing
    std::vector<Detection> detections;
    for (const auto& track : tracks) {
        Detection det;
        det.bbox = track.bbox;
        det.confidence = track.confidence;
        det.classId = track.classId;
        det.className = track.className;
        det.timestamp = track.timestamp;
        detections.push_back(det);
    }
    
    // Process detections with track IDs
    std::vector<bool> crossedIn(detections.size(), false);
    std::vector<bool> crossedOut(detections.size(), false);
    
    // Compute which side of the line each track is on
    auto [inLimits, hasAnyLeftTrigger, hasAnyRightTrigger] = computeAnchorSides(detections);
    
    // Update class ID to name mapping
    updateClassIdToName(detections);
    
    // Process each track for crossing events
    for (size_t i = 0; i < tracks.size(); i++) {
        if (!inLimits[i]) {
            continue; // Skip tracks not in line zone area
        }
        
        if (hasAnyLeftTrigger[i] && hasAnyRightTrigger[i]) {
            continue; // Skip tracks that have anchors on both sides
        }
        
        int trackId = tracks[i].trackId;
        bool trackerState = hasAnyLeftTrigger[i];
        
        auto& crossingHistory = crossingStateHistory_[trackId];
        crossingHistory.push_back(trackerState);
        
        // Keep history at specified length
        while (crossingHistory.size() > crossingHistoryLength_) {
            crossingHistory.pop_front();
        }
        
        // Need enough history to make a decision
        if (crossingHistory.size() < crossingHistoryLength_) {
            continue;
        }
        
        // Get the oldest state and check if it's been consistent
        bool oldestState = crossingHistory.front();
        
        // If the state appears more than once, it means there was no clean transition
        size_t oldestStateCount = 0;
        for (const auto& state : crossingHistory) {
            if (state == oldestState) {
                oldestStateCount++;
            }
        }
        
        if (oldestStateCount > 1) {
            continue; // No clean transition
        }
        
        // Determine direction of crossing
        int classId = 0;
        try {
            classId = std::stoi(tracks[i].classId);
        } catch (...) {
            classId = 0; // Default if not a number
        }
        
        // Record crossing event
        Event crossingEvent;
        crossingEvent.timestamp = getCurrentTimestamp();
        crossingEvent.objectId = std::to_string(trackId);
        crossingEvent.className = tracks[i].className;
        
        // Get center of bounding box for location
        cv::Rect bbox = tracks[i].bbox;
        crossingEvent.location = cv::Point(
            bbox.x + bbox.width / 2,
            bbox.y + bbox.height / 2
        );
        
        // Set zone ID to component node ID
        crossingEvent.zoneId = id_;
        
        // Lock for count operations
        {
            std::lock_guard<std::mutex> lock(countMutex_);
            
            if (trackerState) {
                // Crossed from right to left (in)
                inCount_++;
                inCountPerClass_[classId]++;
                crossedIn[i] = true;
                
                // Create crossing event
                crossingEvent.type = "line_crossing_in";
                crossingEvent.metadata["direction"] = "in";
                crossingEvent.metadata["in_count"] = inCount_;
                crossingEvent.metadata["out_count"] = outCount_;
                crossingEvents.push_back(crossingEvent);
                
                std::cout << "LineZone " << id_ << ": Object " << trackId << " (" << tracks[i].className 
                          << ") crossed IN, total in=" << inCount_ 
                          << ", out=" << outCount_ << std::endl;
            } else {
                // Crossed from left to right (out)
                outCount_++;
                outCountPerClass_[classId]++;
                crossedOut[i] = true;
                
                // Create crossing event
                crossingEvent.type = "line_crossing_out";
                crossingEvent.metadata["direction"] = "out";
                crossingEvent.metadata["in_count"] = inCount_;
                crossingEvent.metadata["out_count"] = outCount_;
                crossingEvents.push_back(crossingEvent);
                
                std::cout << "LineZone " << id_ << ": Object " << trackId << " (" << tracks[i].className 
                          << ") crossed OUT, total in=" << inCount_ 
                          << ", out=" << outCount_ << std::endl;
            }
        } // Lock released here
    }
    
    return crossingEvents;
}

std::pair<Vector, Vector> LineZone::calculateRegionOfInterestLimits(const Vector& vector) {
    float magnitude = vector.Magnitude();
    
    if (magnitude == 0) {
        throw std::invalid_argument("The magnitude of the vector cannot be zero.");
    }
    
    float deltaX = vector.end.x - vector.start.x;
    float deltaY = vector.end.y - vector.start.y;
    
    float unitVectorX = deltaX / magnitude;
    float unitVectorY = deltaY / magnitude;
    
    // Perpendicular vector (90 degrees CCW rotation)
    float perpVectorX = -unitVectorY;
    float perpVectorY = unitVectorX;
    
    // Create limit vectors - use a large enough value to ensure they extend far
    float limitLength = 10000.0f; // Arbitrary large number
    
    Vector startRegionLimit(
        vector.start,
        Point(
            vector.start.x + perpVectorX * limitLength,
            vector.start.y + perpVectorY * limitLength
        )
    );
    
    Vector endRegionLimit(
        vector.end,
        Point(
            vector.end.x - perpVectorX * limitLength,
            vector.end.y - perpVectorY * limitLength
        )
    );
    
    return std::make_pair(startRegionLimit, endRegionLimit);
}

std::tuple<std::vector<bool>, std::vector<bool>, std::vector<bool>> 
LineZone::computeAnchorSides(const std::vector<Detection>& detections) const {
    std::vector<bool> inLimits(detections.size(), false);
    std::vector<bool> hasAnyLeftTrigger(detections.size(), false);
    std::vector<bool> hasAnyRightTrigger(detections.size(), false);
    
    if (detections.empty()) {
        return std::make_tuple(inLimits, hasAnyLeftTrigger, hasAnyRightTrigger);
    }
    
    // Process each detection
    for (size_t i = 0; i < detections.size(); i++) {
        const auto& bbox = detections[i].bbox;
        
        // Get anchor points for this bounding box
        std::vector<Point> anchorPoints;
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
        
        // For each anchor, determine if it's within limits and on which side of the line
        std::vector<bool> anchorInLimits;
        std::vector<bool> anchorIsLeft;
        
        for (const auto& point : anchorPoints) {
            // Check if anchor is within limits (between the two perpendicular lines)
            // Works because limit vectors are in opposite directions
            float crossProduct1 = startRegionLimit_.CrossProduct(point);
            float crossProduct2 = endRegionLimit_.CrossProduct(point);
            bool withinLimits = (crossProduct1 > 0) == (crossProduct2 > 0);
            
            // Check which side of the line the anchor is on
            float crossProductLine = line_.CrossProduct(point);
            bool isLeft = crossProductLine < 0;
            
            anchorInLimits.push_back(withinLimits);
            anchorIsLeft.push_back(isLeft);
        }
        
        // Detection is within limits if all anchors are within limits
        bool allAnchorsInLimits = std::all_of(anchorInLimits.begin(), anchorInLimits.end(), 
                                             [](bool val) { return val; });
        inLimits[i] = allAnchorsInLimits;
        
        // Detection has left/right triggers if any anchor is on that side
        hasAnyLeftTrigger[i] = std::any_of(anchorIsLeft.begin(), anchorIsLeft.end(), 
                                          [](bool val) { return val; });
        hasAnyRightTrigger[i] = std::any_of(anchorIsLeft.begin(), anchorIsLeft.end(), 
                                           [](bool val) { return !val; });
    }
    
    return std::make_tuple(inLimits, hasAnyLeftTrigger, hasAnyRightTrigger);
}

void LineZone::updateClassIdToName(const std::vector<Detection>& detections) {
    for (const auto& detection : detections) {
        try {
            int classId = std::stoi(detection.classId);
            classIdToName_[classId] = detection.className;
        } catch (...) {
            // Skip if class ID is not an integer
        }
    }
}

} // namespace tapi 
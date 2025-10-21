#include "geometry/line_zone.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace tapi {

// Implementation of the StringToPosition function
Position StringToPosition(const std::string& posStr) {
    if (posStr == "TOP_LEFT") return Position::TOP_LEFT;
    if (posStr == "TOP_RIGHT") return Position::TOP_RIGHT;
    if (posStr == "BOTTOM_LEFT") return Position::BOTTOM_LEFT;
    if (posStr == "BOTTOM_RIGHT") return Position::BOTTOM_RIGHT;
    if (posStr == "CENTER") return Position::CENTER;
    if (posStr == "TOP_CENTER") return Position::TOP_CENTER;
    if (posStr == "BOTTOM_CENTER") return Position::BOTTOM_CENTER;
    if (posStr == "CENTER_LEFT") return Position::CENTER_LEFT;
    if (posStr == "CENTER_RIGHT") return Position::CENTER_RIGHT;
    if (posStr == "CENTER_OF_MASS") return Position::CENTER_OF_MASS;
    
    // Default to CENTER if not recognized
    return Position::CENTER;
}

// Implementation of getCurrentTimestamp
int64_t getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Convert Position enum to string
std::string PositionToString(Position pos) {
    switch (pos) {
        case Position::TOP_LEFT: return "TOP_LEFT";
        case Position::TOP_RIGHT: return "TOP_RIGHT";
        case Position::BOTTOM_LEFT: return "BOTTOM_LEFT";
        case Position::BOTTOM_RIGHT: return "BOTTOM_RIGHT";
        case Position::CENTER: return "CENTER";
        case Position::TOP_CENTER: return "TOP_CENTER";
        case Position::BOTTOM_CENTER: return "BOTTOM_CENTER";
        case Position::CENTER_LEFT: return "CENTER_LEFT";
        case Position::CENTER_RIGHT: return "CENTER_RIGHT";
        case Position::CENTER_OF_MASS: return "CENTER_OF_MASS";
        default: return "CENTER";
    }
}

LineZone::LineZone(const std::string& id,
                 float startX, float startY,
                 float endX, float endY,
                 const std::string& streamId,
                 int minCrossingThreshold,
                 const std::vector<std::string>& triggeringAnchorStrings,
                 const std::vector<std::string>& triggeringClasses)
    : id_(id),
      streamId_(streamId),
      inCount_(0),
      outCount_(0),
      initialized_(false) {
    
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
            triggeringAnchors_.push_back(StringToPosition(anchorStr));
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
    
    // Set up triggering classes
    if (triggeringClasses.empty()) {
        // Default to all classes (empty means accept all)
        triggeringClasses_ = {};
    } else {
        triggeringClasses_ = triggeringClasses;
    }
    
    // Set up history length
    crossingHistoryLength_ = std::max(2, minCrossingThreshold_ + 1);
}

// Get line endpoints
const std::pair<Point, Point> LineZone::getLineEndpoints() const {
    return std::make_pair(startPoint_, endPoint_);
}

// Set line endpoints
void LineZone::setLineEndpoints(const Point& start, const Point& end) {
    startPoint_ = start;
    endPoint_ = end;
    line_ = Vector(startPoint_, endPoint_);
    
    // Recalculate region of interest limits
    auto limits = calculateRegionOfInterestLimits(line_);
    startRegionLimit_ = limits.first;
    endRegionLimit_ = limits.second;
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

bool LineZone::updateConfig(const nlohmann::json& config) {
    try {
        // Create a local copy to work with
        float newStartX = startPoint_.x;
        float newStartY = startPoint_.y;
        float newEndX = endPoint_.x;
        float newEndY = endPoint_.y;
        int newMinCrossingThreshold = minCrossingThreshold_;
        std::vector<Position> newAnchors;
        std::vector<std::string> newTriggeringClasses;
        bool endpointsChanged = false;
        bool thresholdChanged = false;
        bool anchorsChanged = false;
        bool classesChanged = false;
        
        // Check for line points changes
        if (config.contains("start_x")) {
            newStartX = config["start_x"].get<float>();
            endpointsChanged = true;
        }
        
        if (config.contains("start_y")) {
            newStartY = config["start_y"].get<float>();
            endpointsChanged = true;
        }
        
        if (config.contains("end_x")) {
            newEndX = config["end_x"].get<float>();
            endpointsChanged = true;
        }
        
        if (config.contains("end_y")) {
            newEndY = config["end_y"].get<float>();
            endpointsChanged = true;
        }
        
        // Check for threshold change
        if (config.contains("min_crossing_threshold")) {
            newMinCrossingThreshold = std::max(1, config["min_crossing_threshold"].get<int>());
            thresholdChanged = true;
        }
        
        // Check for triggering anchors change
        if (config.contains("triggering_anchors") && config["triggering_anchors"].is_array()) {
            for (const auto& anchor : config["triggering_anchors"]) {
                if (anchor.is_string()) {
                    newAnchors.push_back(StringToPosition(anchor.get<std::string>()));
                }
            }
            
            if (!newAnchors.empty()) {
                anchorsChanged = true;
            }
        }
        
        // Check for triggering classes change
        if (config.contains("triggering_classes") && config["triggering_classes"].is_array()) {
            for (const auto& className : config["triggering_classes"]) {
                if (className.is_string()) {
                    newTriggeringClasses.push_back(className.get<std::string>());
                }
            }
            classesChanged = true;
        }
        
        // Now apply all changes atomically
        {
            // Only very brief lock is needed here
            std::lock_guard<std::mutex> lock(countMutex_);
            
            if (endpointsChanged) {
                startPoint_ = Point(newStartX, newStartY);
                endPoint_ = Point(newEndX, newEndY);
                line_ = Vector(startPoint_, endPoint_);
                
                auto limits = calculateRegionOfInterestLimits(line_);
                startRegionLimit_ = limits.first;
                endRegionLimit_ = limits.second;
            }
            
            if (thresholdChanged) {
                minCrossingThreshold_ = newMinCrossingThreshold;
                crossingHistoryLength_ = std::max(2, minCrossingThreshold_ + 1);
            }
            
            if (anchorsChanged && !newAnchors.empty()) {
                triggeringAnchors_ = newAnchors;
            }
            
            if (classesChanged) {
                triggeringClasses_ = newTriggeringClasses;
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
        if (!initialize()) {
            return crossingEvents;
        }
    }
    
    if (tracks.empty()) {
        return crossingEvents; // No tracks to process
    }
    
    // Convert tracks to detections for processing
    std::vector<Detection> detections;
    for (const auto& track : tracks) {
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
    }
    
    // If no detections remain after filtering, return empty events
    if (detections.empty()) {
        return crossingEvents;
    }
    
    // Compute which side of the line each track is on
    auto [inLimits, hasAnyLeftTrigger, hasAnyRightTrigger] = computeAnchorSides(detections);
    
    // Update class ID to name mapping
    updateClassIdToName(detections);
    
    // Process each track for crossing events
    for (size_t i = 0; i < tracks.size(); i++) {
        // Skip tracks that were filtered out
        if (!triggeringClasses_.empty()) {
            bool classMatches = false;
            for (const auto& triggeringClass : triggeringClasses_) {
                if (tracks[i].className == triggeringClass) {
                    classMatches = true;
                    break;
                }
            }
            if (!classMatches) {
                continue;
            }
        }
        
        // Find the corresponding detection index (since we filtered)
        size_t detIndex = 0;
        for (size_t j = 0; j < tracks.size(); j++) {
            if (j == i) break;
            
            // Check if track at index j was included in detections
            if (triggeringClasses_.empty()) {
                detIndex++;
            } else {
                bool classMatches = false;
                for (const auto& triggeringClass : triggeringClasses_) {
                    if (tracks[j].className == triggeringClass) {
                        classMatches = true;
                        break;
                    }
                }
                if (classMatches) {
                    detIndex++;
                }
            }
        }
        
        if (detIndex >= inLimits.size()) {
            continue; // Safety check
        }
        
        if (!inLimits[detIndex]) {
            continue; // Skip tracks not in line zone area
        }
        
        if (hasAnyLeftTrigger[detIndex] && hasAnyRightTrigger[detIndex]) {
            continue; // Skip tracks that have anchors on both sides
        }
        
        int trackId = tracks[i].trackId;
        bool trackerState = hasAnyLeftTrigger[detIndex];
        
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
        
        // Set zone ID
        crossingEvent.zoneId = id_;
        
        // Lock for count operations
        {
            std::lock_guard<std::mutex> lock(countMutex_);
            
            if (trackerState) {
                // Crossed from right to left (in)
                inCount_++;
                inCountPerClass_[classId]++;
                
                // Create crossing event
                crossingEvent.type = "line_crossing_in";
                crossingEvent.metadata["direction"] = "in";
                crossingEvent.metadata["in_count"] = std::to_string(inCount_);
                crossingEvent.metadata["out_count"] = std::to_string(outCount_);
                crossingEvents.push_back(crossingEvent);
            } else {
                // Crossed from left to right (out)
                outCount_++;
                outCountPerClass_[classId]++;
                
                // Create crossing event
                crossingEvent.type = "line_crossing_out";
                crossingEvent.metadata["direction"] = "out";
                crossingEvent.metadata["in_count"] = std::to_string(inCount_);
                crossingEvent.metadata["out_count"] = std::to_string(outCount_);
                crossingEvents.push_back(crossingEvent);
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

std::vector<std::string> LineZone::getTriggeringAnchors() const {
    std::vector<std::string> anchorStrings;
    for (const auto& anchorPos : triggeringAnchors_) {
        anchorStrings.push_back(PositionToString(anchorPos));
    }
    return anchorStrings;
}

std::vector<std::string> LineZone::getTriggeringClasses() const {
    return triggeringClasses_;
}

} // namespace tapi 
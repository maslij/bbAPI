#include "components/processor/zone_timer.h"
#include "logger.h"
#include <unordered_set>

namespace tapi {

ZoneTimer::ZoneTimer() {
}

void ZoneTimer::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    zoneEntryTimes_.clear();
    accumulatedTimes_.clear();
}

std::unordered_map<int, double> ZoneTimer::update(const std::string& zoneId, const std::vector<int>& objectIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get current time
    auto currentTime = std::chrono::steady_clock::now();
    
    // Create set of object IDs for quick lookups
    std::unordered_set<int> currentObjectIds(objectIds.begin(), objectIds.end());
    
    // Check which objects have left the zone
    if (zoneEntryTimes_.find(zoneId) != zoneEntryTimes_.end()) {
        auto& entryTimes = zoneEntryTimes_[zoneId];
        std::vector<int> objectsToRemove;
        
        for (const auto& [objectId, entryTime] : entryTimes) {
            if (currentObjectIds.find(objectId) == currentObjectIds.end()) {
                // Object has left the zone, calculate time spent
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - entryTime).count() / 1000.0;
                
                // Add to accumulated time
                if (accumulatedTimes_[zoneId].find(objectId) == accumulatedTimes_[zoneId].end()) {
                    accumulatedTimes_[zoneId][objectId] = duration;
                } else {
                    accumulatedTimes_[zoneId][objectId] += duration;
                }
                
                objectsToRemove.push_back(objectId);
            }
        }
        
        // Remove objects that have left
        for (int objectId : objectsToRemove) {
            entryTimes.erase(objectId);
        }
    }
    
    // Record entry times for new objects
    for (int objectId : objectIds) {
        if (zoneEntryTimes_[zoneId].find(objectId) == zoneEntryTimes_[zoneId].end()) {
            // Object is new to the zone, record entry time
            zoneEntryTimes_[zoneId][objectId] = currentTime;
        }
    }
    
    // Calculate current durations for objects in the zone
    std::unordered_map<int, double> currentDurations;
    for (int objectId : objectIds) {
        double accumulatedTime = 0.0;
        if (accumulatedTimes_[zoneId].find(objectId) != accumulatedTimes_[zoneId].end()) {
            accumulatedTime = accumulatedTimes_[zoneId][objectId];
        }
        
        auto entryTime = zoneEntryTimes_[zoneId][objectId];
        double currentSessionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - entryTime).count() / 1000.0;
        
        currentDurations[objectId] = accumulatedTime + currentSessionDuration;
    }
    
    return currentDurations;
}

double ZoneTimer::getTimeInZone(const std::string& zoneId, int objectId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    double totalTime = 0.0;
    
    // Add accumulated time (from previous zone entries/exits)
    if (accumulatedTimes_.find(zoneId) != accumulatedTimes_.end() &&
        accumulatedTimes_.at(zoneId).find(objectId) != accumulatedTimes_.at(zoneId).end()) {
        totalTime += accumulatedTimes_.at(zoneId).at(objectId);
    }
    
    // If object is currently in the zone, add current session time
    if (zoneEntryTimes_.find(zoneId) != zoneEntryTimes_.end() &&
        zoneEntryTimes_.at(zoneId).find(objectId) != zoneEntryTimes_.at(zoneId).end()) {
        auto currentTime = std::chrono::steady_clock::now();
        auto entryTime = zoneEntryTimes_.at(zoneId).at(objectId);
        double currentSessionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - entryTime).count() / 1000.0;
        
        totalTime += currentSessionDuration;
    }
    
    return totalTime;
}

std::unordered_map<int, double> ZoneTimer::getAllTimesInZone(const std::string& zoneId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::unordered_map<int, double> result;
    
    // Get current time for calculating durations of objects still in the zone
    auto currentTime = std::chrono::steady_clock::now();
    
    // First add all accumulated times
    if (accumulatedTimes_.find(zoneId) != accumulatedTimes_.end()) {
        for (const auto& [objectId, time] : accumulatedTimes_.at(zoneId)) {
            result[objectId] = time;
        }
    }
    
    // Then add current session durations for objects still in the zone
    if (zoneEntryTimes_.find(zoneId) != zoneEntryTimes_.end()) {
        for (const auto& [objectId, entryTime] : zoneEntryTimes_.at(zoneId)) {
            double currentSessionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime - entryTime).count() / 1000.0;
            
            if (result.find(objectId) != result.end()) {
                result[objectId] += currentSessionDuration;
            } else {
                result[objectId] = currentSessionDuration;
            }
        }
    }
    
    return result;
}

} // namespace tapi 
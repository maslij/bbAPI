#pragma once

#include <unordered_map>
#include <string>
#include <chrono>
#include <mutex>
#include <vector>

namespace tapi {

/**
 * @brief Class for tracking how long objects stay in zones
 * 
 * This class keeps track of the time that objects spend inside polygon zones.
 */
class ZoneTimer {
public:
    /**
     * @brief Constructor
     */
    ZoneTimer();
    
    /**
     * @brief Reset all timers
     */
    void reset();
    
    /**
     * @brief Update timers for objects in a zone
     * 
     * @param zoneId ID of the zone
     * @param objectIds List of track IDs currently in the zone
     * @return Map of object IDs to durations in seconds
     */
    std::unordered_map<int, double> update(const std::string& zoneId, const std::vector<int>& objectIds);
    
    /**
     * @brief Get time spent in zone for a specific object
     * 
     * @param zoneId ID of the zone
     * @param objectId Object track ID
     * @return Time in seconds, or 0 if not found
     */
    double getTimeInZone(const std::string& zoneId, int objectId) const;
    
    /**
     * @brief Get all times for objects in a zone
     * 
     * @param zoneId ID of the zone
     * @return Map of object IDs to durations in seconds
     */
    std::unordered_map<int, double> getAllTimesInZone(const std::string& zoneId) const;
    
private:
    // Map of zone IDs to maps of object IDs to entry timestamps
    std::unordered_map<std::string, std::unordered_map<int, std::chrono::time_point<std::chrono::steady_clock>>> zoneEntryTimes_;
    
    // Map of zone IDs to maps of object IDs to accumulated durations (for objects that leave and re-enter)
    std::unordered_map<std::string, std::unordered_map<int, double>> accumulatedTimes_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
};

} // namespace tapi 
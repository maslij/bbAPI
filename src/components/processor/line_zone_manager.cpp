#include "components/processor/line_zone_manager.h"
#include "geometry/line_zone.h"
#include "logger.h"
#include <iostream>
#include <sstream>

namespace tapi {

LineZoneManager::LineZoneManager(const std::string& id, Camera* camera,
                               const std::string& type, const nlohmann::json& config)
    : ProcessorComponent(id, camera),
      drawZones_(true),
      lineColor_(255, 255, 255),   // White line to match example
      lineThickness_(2),
      drawCounts_(true),
      textColor_(0, 0, 0),         // Black text on white background
      textScale_(0.5f),             // Smaller text scale to match example
      textThickness_(1),           // Thinner text to match example
      drawDirectionArrows_(true),  // Enable direction arrows by default
      arrowColor_(255, 255, 0),    // Yellow arrows by default
      arrowSize_(20.0f),
      arrowHeadSize_(10.0f),
      arrowAngleDegrees_(30.0f),
      drawEndpointCircles_(true),  // Draw circles at line endpoints
      circleColor_(0, 0, 0),       // Black circles to match example
      circleRadius_(5),
      textBackgroundColor_(255, 255, 255), // White background for text
      textPadding_(5),             // Less padding to match example
      displayTextBox_(true),       // Display box around text
      inText_("in"),               // Simple lowercase labels
      outText_("out"),             // Simple lowercase labels
      textOrientToLine_(false),    // Don't orient text along line by default
      textCentered_(true),         // Center text on line
      frameWidth_(0),
      frameHeight_(0),
      useNormalizedCoords_(true) {  // Added flag for normalized coordinates
    
    config_ = config;
    
    LOG_INFO("LineZoneManager", "Created LineZoneManager with ID: " + id);
}

LineZoneManager::~LineZoneManager() {
    if (isRunning()) {
        stop();
    }
}

bool LineZoneManager::initialize() {
    LOG_INFO("LineZoneManager", "Initializing LineZoneManager with ID: " + getId());
    
    try {
        // Create temporary variables to hold what we need to do
        nlohmann::json configCopy;
        std::map<std::string, std::shared_ptr<LineZone>> zonesToAdd;
        
        // Get a brief lock to copy the data we need
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Make a copy of our config
            configCopy = config_;
            
            // Parse configuration settings that don't require zones
            if (configCopy.contains("draw_zones")) {
                drawZones_ = configCopy["draw_zones"].get<bool>();
            }
            
            if (configCopy.contains("line_color")) {
                auto& colorArray = configCopy["line_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    lineColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (configCopy.contains("line_thickness")) {
                lineThickness_ = configCopy["line_thickness"].get<int>();
            }
            
            if (configCopy.contains("draw_counts")) {
                drawCounts_ = configCopy["draw_counts"].get<bool>();
            }
            
            if (configCopy.contains("text_color")) {
                auto& colorArray = configCopy["text_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    textColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (configCopy.contains("text_scale")) {
                textScale_ = configCopy["text_scale"].get<double>();
            }
            
            if (configCopy.contains("text_thickness")) {
                textThickness_ = configCopy["text_thickness"].get<int>();
            }
            
            // New configuration parameters for direction arrows
            if (configCopy.contains("draw_direction_arrows")) {
                drawDirectionArrows_ = configCopy["draw_direction_arrows"].get<bool>();
            }
            
            if (configCopy.contains("arrow_color")) {
                auto& colorArray = configCopy["arrow_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    arrowColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (configCopy.contains("arrow_size")) {
                arrowSize_ = configCopy["arrow_size"].get<float>();
            }
            
            if (configCopy.contains("arrow_head_size")) {
                arrowHeadSize_ = configCopy["arrow_head_size"].get<float>();
            }
            
            if (configCopy.contains("arrow_angle_degrees")) {
                arrowAngleDegrees_ = configCopy["arrow_angle_degrees"].get<float>();
            }
            
            // Additional styling options
            if (configCopy.contains("draw_endpoint_circles")) {
                drawEndpointCircles_ = configCopy["draw_endpoint_circles"].get<bool>();
            }
            
            if (configCopy.contains("circle_color")) {
                auto& colorArray = configCopy["circle_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    circleColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (configCopy.contains("circle_radius")) {
                circleRadius_ = configCopy["circle_radius"].get<int>();
            }
            
            if (configCopy.contains("text_background_color")) {
                auto& colorArray = configCopy["text_background_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    textBackgroundColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (configCopy.contains("text_padding")) {
                textPadding_ = configCopy["text_padding"].get<int>();
            }
            
            if (configCopy.contains("display_text_box")) {
                displayTextBox_ = configCopy["display_text_box"].get<bool>();
            }
            
            if (configCopy.contains("in_text")) {
                inText_ = configCopy["in_text"].get<std::string>();
            }
            
            if (configCopy.contains("out_text")) {
                outText_ = configCopy["out_text"].get<std::string>();
            }
            
            if (configCopy.contains("text_orient_to_line")) {
                textOrientToLine_ = configCopy["text_orient_to_line"].get<bool>();
            }
            
            if (configCopy.contains("text_centered")) {
                textCentered_ = configCopy["text_centered"].get<bool>();
            }
        }
        
        // Outside the lock, prepare zones to be added
        if (configCopy.contains("zones") && configCopy["zones"].is_array()) {
            for (const auto& zoneConfig : configCopy["zones"]) {
                // Get zone parameters from config
                std::string id = zoneConfig.contains("id") ? 
                                 zoneConfig["id"].get<std::string>() : "zone_" + std::to_string(zonesToAdd.size() + 1);
                
                // Get coordinates (now as normalized 0-1 values)
                float startX = zoneConfig.contains("start_x") ? zoneConfig["start_x"].get<float>() : 0.0f;
                float startY = zoneConfig.contains("start_y") ? zoneConfig["start_y"].get<float>() : 0.0f;
                float endX = zoneConfig.contains("end_x") ? zoneConfig["end_x"].get<float>() : 0.0f;
                float endY = zoneConfig.contains("end_y") ? zoneConfig["end_y"].get<float>() : 0.0f;
                
                int minCrossingThreshold = zoneConfig.contains("min_crossing_threshold") ? 
                                          zoneConfig["min_crossing_threshold"].get<int>() : 1;
                
                std::vector<std::string> triggeringAnchors;
                if (zoneConfig.contains("triggering_anchors") && zoneConfig["triggering_anchors"].is_array()) {
                    for (const auto& anchor : zoneConfig["triggering_anchors"]) {
                        if (anchor.is_string()) {
                            triggeringAnchors.push_back(anchor.get<std::string>());
                        }
                    }
                }
                
                std::vector<std::string> triggeringClasses;
                if (zoneConfig.contains("triggering_classes") && zoneConfig["triggering_classes"].is_array()) {
                    for (const auto& className : zoneConfig["triggering_classes"]) {
                        if (className.is_string()) {
                            triggeringClasses.push_back(className.get<std::string>());
                        }
                    }
                }
                
                // Create the zone with normalized coordinates
                auto zone = std::make_shared<LineZone>(
                    id, startX, startY, endX, endY,
                    getId(), minCrossingThreshold, triggeringAnchors, triggeringClasses);
                
                // Initialize the zone
                if (!zone->initialize()) {
                    LOG_ERROR("LineZoneManager", "Failed to initialize line zone: " + id);
                    continue;
                }
                
                // Add to our temporary map
                zonesToAdd[id] = zone;
            }
        }
        
        // Now add all the zones with a brief lock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Add all the zones we created
            for (const auto& [id, zone] : zonesToAdd) {
                lineZones_[id] = zone;
                LOG_INFO("LineZoneManager", "Added line zone " + id);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("LineZoneManager", "Failed to initialize: " + std::string(e.what()));
        return false;
    }
}

bool LineZoneManager::start() {
    LOG_INFO("LineZoneManager", "Starting LineZoneManager with ID: " + getId());
    
    try {
        // Get a copy of the zones we need to initialize
        std::map<std::string, std::shared_ptr<LineZone>> zonesToInitialize;
        
        // Briefly lock to get a copy of the zones
        {
            std::lock_guard<std::mutex> lock(mutex_);
            zonesToInitialize = lineZones_;
        }
        
        // Initialize all zones outside the lock
        for (auto& [id, zone] : zonesToInitialize) {
            if (!zone->initialize()) {
                LOG_ERROR("LineZoneManager", "Failed to initialize line zone: " + id);
                return false;
            }
        }
        
        // Set running flag with brief lock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = true;
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("LineZoneManager", "Failed to start: " + std::string(e.what()));
        return false;
    }
}

bool LineZoneManager::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_INFO("LineZoneManager", "Stopping LineZoneManager with ID: " + getId());
    
    running_ = false;
    return true;
}

bool LineZoneManager::updateConfig(const nlohmann::json& config) {
    LOG_INFO("LineZoneManager", "Updating configuration for LineZoneManager with ID: " + getId());
    
    try {
        // Create a local copy of the config to work with
        nlohmann::json newConfig = config;
        std::map<std::string, std::shared_ptr<LineZone>> updatedZones;
        std::map<std::string, std::shared_ptr<LineZone>> zonesToAdd;
        std::set<std::string> zonesToRemove;
        
        // First, acquire the lock and make a quick copy of what we need
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Copy existing line zones to our updated map
            updatedZones = lineZones_;
            
            // Get list of existing zone IDs
            std::set<std::string> existingZoneIds;
            for (const auto& [id, _] : lineZones_) {
                existingZoneIds.insert(id);
            }
            
            // Process non-zone configuration updates
            if (newConfig.contains("draw_zones")) {
                drawZones_ = newConfig["draw_zones"].get<bool>();
            }
            
            if (newConfig.contains("line_color")) {
                auto& colorArray = newConfig["line_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    lineColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (newConfig.contains("line_thickness")) {
                lineThickness_ = newConfig["line_thickness"].get<int>();
            }
            
            if (newConfig.contains("draw_counts")) {
                drawCounts_ = newConfig["draw_counts"].get<bool>();
            }
            
            if (newConfig.contains("text_color")) {
                auto& colorArray = newConfig["text_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    textColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (newConfig.contains("text_scale")) {
                textScale_ = newConfig["text_scale"].get<double>();
            }
            
            if (newConfig.contains("text_thickness")) {
                textThickness_ = newConfig["text_thickness"].get<int>();
            }
            
            // New configuration parameters for direction arrows
            if (newConfig.contains("draw_direction_arrows")) {
                drawDirectionArrows_ = newConfig["draw_direction_arrows"].get<bool>();
            }
            
            if (newConfig.contains("arrow_color")) {
                auto& colorArray = newConfig["arrow_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    arrowColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (newConfig.contains("arrow_size")) {
                arrowSize_ = newConfig["arrow_size"].get<float>();
            }
            
            if (newConfig.contains("arrow_head_size")) {
                arrowHeadSize_ = newConfig["arrow_head_size"].get<float>();
            }
            
            if (newConfig.contains("arrow_angle_degrees")) {
                arrowAngleDegrees_ = newConfig["arrow_angle_degrees"].get<float>();
            }
            
            // Additional styling options
            if (newConfig.contains("draw_endpoint_circles")) {
                drawEndpointCircles_ = newConfig["draw_endpoint_circles"].get<bool>();
            }
            
            if (newConfig.contains("circle_color")) {
                auto& colorArray = newConfig["circle_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    circleColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (newConfig.contains("circle_radius")) {
                circleRadius_ = newConfig["circle_radius"].get<int>();
            }
            
            if (newConfig.contains("text_background_color")) {
                auto& colorArray = newConfig["text_background_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    textBackgroundColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (newConfig.contains("text_padding")) {
                textPadding_ = newConfig["text_padding"].get<int>();
            }
            
            if (newConfig.contains("display_text_box")) {
                displayTextBox_ = newConfig["display_text_box"].get<bool>();
            }
            
            if (newConfig.contains("in_text")) {
                inText_ = newConfig["in_text"].get<std::string>();
            }
            
            if (newConfig.contains("out_text")) {
                outText_ = newConfig["out_text"].get<std::string>();
            }
            
            if (newConfig.contains("text_orient_to_line")) {
                textOrientToLine_ = newConfig["text_orient_to_line"].get<bool>();
            }
            
            if (newConfig.contains("text_centered")) {
                textCentered_ = newConfig["text_centered"].get<bool>();
            }
            
            // Process zones in a way that minimizes lock time
            if (newConfig.contains("zones") && newConfig["zones"].is_array()) {
                // Plan updates but don't execute them yet
                for (const auto& zoneConfig : newConfig["zones"]) {
                    if (!zoneConfig.contains("id")) {
                        continue; // Skip zones without IDs
                    }
                    
                    std::string id = zoneConfig["id"].get<std::string>();
                    existingZoneIds.erase(id); // Remove from the set since we're handling it
                    
                    auto it = updatedZones.find(id);
                    if (it != updatedZones.end()) {
                        // Don't update the zone yet, just note that we need to
                        // We'll update zones outside the lock
                    } else {
                        // Create new zones outside the lock
                        float startX = zoneConfig.contains("start_x") ? zoneConfig["start_x"].get<float>() : 0.0f;
                        float startY = zoneConfig.contains("start_y") ? zoneConfig["start_y"].get<float>() : 0.0f;
                        float endX = zoneConfig.contains("end_x") ? zoneConfig["end_x"].get<float>() : 0.0f;
                        float endY = zoneConfig.contains("end_y") ? zoneConfig["end_y"].get<float>() : 0.0f;
                        
                        int minCrossingThreshold = zoneConfig.contains("min_crossing_threshold") ? 
                                                zoneConfig["min_crossing_threshold"].get<int>() : 1;
                        
                        std::vector<std::string> triggeringAnchors;
                        if (zoneConfig.contains("triggering_anchors") && zoneConfig["triggering_anchors"].is_array()) {
                            for (const auto& anchor : zoneConfig["triggering_anchors"]) {
                                if (anchor.is_string()) {
                                    triggeringAnchors.push_back(anchor.get<std::string>());
                                }
                            }
                        }
                        
                        std::vector<std::string> triggeringClasses;
                        if (zoneConfig.contains("triggering_classes") && zoneConfig["triggering_classes"].is_array()) {
                            for (const auto& className : zoneConfig["triggering_classes"]) {
                                if (className.is_string()) {
                                    triggeringClasses.push_back(className.get<std::string>());
                                }
                            }
                        }
                        
                        // Schedule zone creation
                        zonesToAdd[id] = std::make_shared<LineZone>(
                            id, startX, startY, endX, endY,
                            getId(), minCrossingThreshold, triggeringAnchors, triggeringClasses);
                    }
                }
                
                // Any zones left in the set were not in the config, so remove them
                if (newConfig.contains("remove_missing") && newConfig["remove_missing"].get<bool>()) {
                    zonesToRemove = existingZoneIds;
                }
            }
            
            // Update the stored config
            config_ = newConfig;
        }
        
        // Now, outside the lock, update the existing zones
        if (newConfig.contains("zones") && newConfig["zones"].is_array()) {
            for (const auto& zoneConfig : newConfig["zones"]) {
                if (!zoneConfig.contains("id")) {
                    continue;
                }
                
                std::string id = zoneConfig["id"].get<std::string>();
                auto it = updatedZones.find(id);
                if (it != updatedZones.end()) {
                    // Update existing zone configuration
                    it->second->updateConfig(zoneConfig);
                } else {
                    // ZONE RENAME DETECTION AND COUNT PRESERVATION
                    // When a zone is renamed in the frontend, it appears as a "new" zone with
                    // the same coordinates but different ID. This logic detects such cases
                    // and preserves the runtime counts from the existing zone.
                    bool foundMatchingZone = false;
                    if (zoneConfig.contains("start_x") && zoneConfig.contains("start_y") && 
                        zoneConfig.contains("end_x") && zoneConfig.contains("end_y")) {
                        
                        float newStartX = zoneConfig["start_x"].get<float>();
                        float newStartY = zoneConfig["start_y"].get<float>();
                        float newEndX = zoneConfig["end_x"].get<float>();
                        float newEndY = zoneConfig["end_y"].get<float>();
                        
                        // Look for existing zone with same coordinates but different ID
                        for (auto& [existingId, existingZone] : updatedZones) {
                            auto endpoints = existingZone->getLineEndpoints();
                            float eps = 0.001f; // Small epsilon for float comparison
                            
                            if (std::abs(endpoints.first.x - newStartX) < eps &&
                                std::abs(endpoints.first.y - newStartY) < eps &&
                                std::abs(endpoints.second.x - newEndX) < eps &&
                                std::abs(endpoints.second.y - newEndY) < eps) {
                                
                                // Found matching coordinates - this is likely a renamed zone
                                // Preserve the counts by updating the existing zone and changing its ID
                                existingZone->updateConfig(zoneConfig);
                                existingZone->setId(id); // Update the zone's internal ID
                                
                                // Move the zone to the new ID in zonesToAdd and mark old for removal
                                zonesToAdd[id] = existingZone;
                                zonesToRemove.insert(existingId);
                                foundMatchingZone = true;
                                
                                LOG_INFO("LineZoneManager", "Detected zone rename from '" + existingId + "' to '" + id + "', preserving counts");
                                break;
                            }
                        }
                    }
                    
                    if (!foundMatchingZone) {
                        // This is genuinely a new zone, already created in zonesToAdd during the locked section
                        // No additional action needed here
                    }
                }
            }
        }
        
        // Initialize any newly created zones if manager is running
        for (auto& [id, zone] : zonesToAdd) {
            if (isRunning()) {
                if (!zone->initialize()) {
                    LOG_ERROR("LineZoneManager", "Failed to initialize new line zone: " + id);
                    continue;
                }
            }
        }
        
        // Now, apply all our changes to the actual lineZones_ map
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Remove zones that were not in the config
            for (const auto& id : zonesToRemove) {
                lineZones_.erase(id);
                LOG_INFO("LineZoneManager", "Removed line zone " + id);
            }
            
            // Add new zones
            for (auto& [id, zone] : zonesToAdd) {
                lineZones_[id] = zone;
                LOG_INFO("LineZoneManager", "Added line zone " + id);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("LineZoneManager", "Failed to update config: " + std::string(e.what()));
        return false;
    }
}

nlohmann::json LineZoneManager::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto status = Component::getStatus();
    status["type"] = "line_zone_manager";
    
    // Add additional status information
    nlohmann::json zonesArray = nlohmann::json::array();
    for (const auto& [id, zone] : lineZones_) {
        nlohmann::json zoneJson;
        zoneJson["id"] = id;
        
        auto endpoints = zone->getLineEndpoints();
        zoneJson["start_x"] = endpoints.first.x;
        zoneJson["start_y"] = endpoints.first.y;
        zoneJson["end_x"] = endpoints.second.x;
        zoneJson["end_y"] = endpoints.second.y;
        
        zoneJson["in_count"] = zone->getInCount();
        zoneJson["out_count"] = zone->getOutCount();
        
        // Include additional critical configuration properties
        zoneJson["min_crossing_threshold"] = zone->getMinCrossingThreshold();
        
        // Include triggering anchors
        auto anchors = zone->getTriggeringAnchors();
        if (!anchors.empty()) {
            nlohmann::json anchorsArray = nlohmann::json::array();
            for (const auto& anchor : anchors) {
                anchorsArray.push_back(anchor);
            }
            zoneJson["triggering_anchors"] = anchorsArray;
        }
        
        // Include triggering classes
        auto classes = zone->getTriggeringClasses();
        if (!classes.empty()) {
            nlohmann::json classesArray = nlohmann::json::array();
            for (const auto& className : classes) {
                classesArray.push_back(className);
            }
            zoneJson["triggering_classes"] = classesArray;
        }
        
        zonesArray.push_back(zoneJson);
    }
    
    status["zones"] = zonesArray;
    status["crossing_events"] = crossingEvents_.size();
    
    // Also include the full configuration for completeness
    status["config"] = config_;
    
    return status;
}

std::pair<cv::Mat, std::vector<LineCrossingEvent>> 
LineZoneManager::processFrame(const cv::Mat& frame, const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects) {
    if (!isRunning() || frame.empty()) {
        return {frame, {}};
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update frame dimensions if needed
    if (frameWidth_ != frame.cols || frameHeight_ != frame.rows) {
        frameWidth_ = frame.cols;
        frameHeight_ = frame.rows;
    }
    
    // Convert tracked objects to LineZone tracks
    std::vector<Track> tracks = convertTrackedObjects(trackedObjects);
    
    // Process tracks through all line zones, converting normalized coordinates to pixel coordinates
    std::vector<Event> allEvents;
    for (auto& [id, zone] : lineZones_) {
        // Convert the normalized endpoints to pixel coordinates for processing
        auto normalizedEndpoints = zone->getLineEndpoints();
        Point pixelStart = normalizedToPixel(normalizedEndpoints.first);
        Point pixelEnd = normalizedToPixel(normalizedEndpoints.second);
        
        // Temporarily set the zone's endpoints to pixel coordinates for processing
        zone->setLineEndpoints(pixelStart, pixelEnd);
        
        // Process tracks with pixel coordinates
        auto events = zone->processTracks(tracks);
        allEvents.insert(allEvents.end(), events.begin(), events.end());
        
        // Restore normalized coordinates
        zone->setLineEndpoints(normalizedEndpoints.first, normalizedEndpoints.second);
    }
    
    // Convert all events to LineCrossingEvents
    std::vector<LineCrossingEvent> crossingEvents = convertEvents(allEvents);
    
    // Add to stored events
    crossingEvents_.insert(crossingEvents_.end(), crossingEvents.begin(), crossingEvents.end());
    
    // Create a copy of the frame for drawing
    cv::Mat outputFrame = frame.clone();
    
    // Draw line zones on the frame if enabled
    if (drawZones_) {
        drawLineZones(outputFrame);
    }
    
    return {outputFrame, crossingEvents};
}

bool LineZoneManager::addLineZone(const std::string& id, 
                                float startX, float startY, 
                                float endX, float endY,
                                int minCrossingThreshold,
                                const std::vector<std::string>& triggeringAnchors) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if zone with this ID already exists
    if (lineZones_.find(id) != lineZones_.end()) {
        LOG_WARN("LineZoneManager", "Line zone with ID " + id + " already exists");
        return false;
    }
    
    // Create a new line zone with normalized coordinates
    auto lineZone = std::make_shared<LineZone>(
        id, startX, startY, endX, endY, 
        getId(), // Use the manager's ID as the stream ID
        minCrossingThreshold, triggeringAnchors);
    
    // Initialize the zone immediately if the manager is running
    if (isRunning()) {
        if (!lineZone->initialize()) {
            LOG_ERROR("LineZoneManager", "Failed to initialize line zone: " + id);
            return false;
        }
    }
    
    // Add the zone to the map
    lineZones_[id] = lineZone;
    
    LOG_INFO("LineZoneManager", "Added line zone " + id + " to manager " + getId());
    return true;
}

bool LineZoneManager::removeLineZone(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = lineZones_.find(id);
    if (it == lineZones_.end()) {
        LOG_WARN("LineZoneManager", "Line zone with ID " + id + " not found");
        return false;
    }
    
    lineZones_.erase(it);
    LOG_INFO("LineZoneManager", "Removed line zone " + id + " from manager " + getId());
    return true;
}

std::vector<std::string> LineZoneManager::getLineZoneIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> ids;
    for (const auto& [id, _] : lineZones_) {
        ids.push_back(id);
    }
    
    return ids;
}

std::shared_ptr<LineZone> LineZoneManager::getLineZone(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = lineZones_.find(id);
    if (it == lineZones_.end()) {
        return nullptr;
    }
    
    return it->second;
}

std::vector<LineCrossingEvent> LineZoneManager::getCrossingEvents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return crossingEvents_;
}

void LineZoneManager::clearCrossingEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    crossingEvents_.clear();
}

void LineZoneManager::drawLineZones(cv::Mat& frame) const {
    for (const auto& [id, zone] : lineZones_) {
        // Get normalized line endpoints
        auto endpoints = zone->getLineEndpoints();
        
        // Convert to pixel coordinates for drawing
        Point pixelStart = normalizedToPixel(endpoints.first);
        Point pixelEnd = normalizedToPixel(endpoints.second);
        
        // Draw the line
        cv::Point cvStart(static_cast<int>(pixelStart.x), static_cast<int>(pixelStart.y));
        cv::Point cvEnd(static_cast<int>(pixelEnd.x), static_cast<int>(pixelEnd.y));
        
        cv::line(frame, cvStart, cvEnd, lineColor_, lineThickness_, cv::LINE_AA);
        
        // Draw circles at the endpoints if enabled
        if (drawEndpointCircles_) {
            cv::circle(frame, cvStart, circleRadius_, circleColor_, -1, cv::LINE_AA);
            cv::circle(frame, cvEnd, circleRadius_, circleColor_, -1, cv::LINE_AA);
        }
        
        // Draw direction arrow if enabled
        if (drawDirectionArrows_) {
            cv::Point dirVec = cvEnd - cvStart;
            float length = std::sqrt(dirVec.x * dirVec.x + dirVec.y * dirVec.y);
            
            if (length > 0) {
                // Normalize and scale to the arrow size
                cv::Point2f dirNorm(dirVec.x / length, dirVec.y / length);
                
                // Calculate midpoint
                cv::Point midPoint = cvStart + cv::Point(dirVec.x / 2, dirVec.y / 2);
                
                // Calculate perpendicular vector (counter-clockwise 90 degrees rotation for opposite direction)
                cv::Point2f perpVec(dirNorm.y, -dirNorm.x);  // Flipped from (-dirNorm.y, dirNorm.x)
                
                // Draw arrow from middle of line towards perpendicular direction
                cv::Point arrowEnd = midPoint + cv::Point(perpVec.x * arrowSize_, perpVec.y * arrowSize_);
                cv::line(frame, midPoint, arrowEnd, arrowColor_, lineThickness_, cv::LINE_AA);
                
                // Draw arrow head
                float arrowAngle = arrowAngleDegrees_ * CV_PI / 180.0f; // Convert to radians
                
                cv::Point2f arrowDir(arrowEnd.x - midPoint.x, arrowEnd.y - midPoint.y);
                float arrowLength = std::sqrt(arrowDir.x * arrowDir.x + arrowDir.y * arrowDir.y);
                
                if (arrowLength > 0) {
                    cv::Point2f arrowNorm(arrowDir.x / arrowLength, arrowDir.y / arrowLength);
                    
                    // Calculate arrow head points
                    cv::Point2f arrowLeft(
                        arrowNorm.x * std::cos(arrowAngle) - arrowNorm.y * std::sin(arrowAngle),
                        arrowNorm.x * std::sin(arrowAngle) + arrowNorm.y * std::cos(arrowAngle)
                    );
                    
                    cv::Point2f arrowRight(
                        arrowNorm.x * std::cos(-arrowAngle) - arrowNorm.y * std::sin(-arrowAngle),
                        arrowNorm.x * std::sin(-arrowAngle) + arrowNorm.y * std::cos(-arrowAngle)
                    );
                    
                    cv::Point arrowPoint1 = arrowEnd - cv::Point(arrowLeft.x * arrowHeadSize_, arrowLeft.y * arrowHeadSize_);
                    cv::Point arrowPoint2 = arrowEnd - cv::Point(arrowRight.x * arrowHeadSize_, arrowRight.y * arrowHeadSize_);
                    
                    cv::line(frame, arrowEnd, arrowPoint1, arrowColor_, lineThickness_, cv::LINE_AA);
                    cv::line(frame, arrowEnd, arrowPoint2, arrowColor_, lineThickness_, cv::LINE_AA);
                }
            }
        }
        
        // Draw counts if enabled
        if (drawCounts_) {
            int inCount = zone->getInCount();
            int outCount = zone->getOutCount();
            
            // Calculate line properties using pixel coordinates
            cv::Point dirVec = cvEnd - cvStart;
            float length = std::sqrt(dirVec.x * dirVec.x + dirVec.y * dirVec.y);
            
            // Get midpoint of the line
            cv::Point midPoint(
                static_cast<int>((pixelStart.x + pixelEnd.x) / 2),
                static_cast<int>((pixelStart.y + pixelEnd.y) / 2)
            );
            
            // Prepare text for in/out counts - simplified to match example
            std::string inLabel = inText_ + ": " + std::to_string(inCount);
            std::string outLabel = outText_ + ": " + std::to_string(outCount);
            
            // Calculate text size for positioning
            int baseLine;
            cv::Size inTextSize = cv::getTextSize(inLabel, cv::FONT_HERSHEY_SIMPLEX, 
                                               textScale_, textThickness_, &baseLine);
            cv::Size outTextSize = cv::getTextSize(outLabel, cv::FONT_HERSHEY_SIMPLEX, 
                                                textScale_, textThickness_, &baseLine);
            
            int textHeight = std::max(inTextSize.height, outTextSize.height);
            
            // Calculate offset direction for positioning below the line
            cv::Point2f dirNorm;
            if (length > 0) {
                dirNorm = cv::Point2f(dirVec.x / length, dirVec.y / length);
            } else {
                dirNorm = cv::Point2f(1, 0);  // Default right if no direction
            }
            
            // Define a vertical offset below the line (positive y is down in image coordinates)
            float verticalOffset = textHeight * 2.0f;
            cv::Point verticalShift(0, verticalOffset);
            
            // Calculate horizontal gap between labels
            int labelGap = 20; // Gap between labels in pixels
            int totalWidth = inTextSize.width + outTextSize.width + labelGap;
            
            // Position for in label (left of center, below the line)
            cv::Point inPos = midPoint + verticalShift - cv::Point(totalWidth/2 - inTextSize.width/2, 0);
            
            // Position for out label (right of center, below the line)
            cv::Point outPos = midPoint + verticalShift + cv::Point(totalWidth/2 - outTextSize.width/2, 0);
            
            // Draw IN label
            if (displayTextBox_) {
                cv::Rect inRect(
                    inPos.x - (inTextSize.width / 2) - textPadding_,
                    inPos.y - inTextSize.height - textPadding_,
                    inTextSize.width + 2 * textPadding_,
                    inTextSize.height + 2 * textPadding_
                );
                cv::rectangle(frame, inRect, textBackgroundColor_, cv::FILLED);
            }
            
            cv::putText(frame, inLabel, 
                      cv::Point(inPos.x - (inTextSize.width / 2), inPos.y), 
                      cv::FONT_HERSHEY_SIMPLEX, textScale_, textColor_, textThickness_, cv::LINE_AA);
            
            // Draw OUT label
            if (displayTextBox_) {
                cv::Rect outRect(
                    outPos.x - (outTextSize.width / 2) - textPadding_,
                    outPos.y - outTextSize.height - textPadding_,
                    outTextSize.width + 2 * textPadding_,
                    outTextSize.height + 2 * textPadding_
                );
                cv::rectangle(frame, outRect, textBackgroundColor_, cv::FILLED);
            }
            
            cv::putText(frame, outLabel, 
                      cv::Point(outPos.x - (outTextSize.width / 2), outPos.y), 
                      cv::FONT_HERSHEY_SIMPLEX, textScale_, textColor_, textThickness_, cv::LINE_AA);
        }
    }
}

std::vector<LineCrossingEvent> LineZoneManager::convertEvents(const std::vector<Event>& events) const {
    std::vector<LineCrossingEvent> crossingEvents;
    
    for (const auto& event : events) {
        LineCrossingEvent crossingEvent;
        crossingEvent.timestamp = event.timestamp;
        crossingEvent.objectId = event.objectId;
        crossingEvent.className = event.className;
        crossingEvent.location = event.location;
        crossingEvent.zoneId = event.zoneId;
        
        // Set the direction
        if (event.type == "line_crossing_in") {
            crossingEvent.direction = "in";
        } else if (event.type == "line_crossing_out") {
            crossingEvent.direction = "out";
        } else {
            crossingEvent.direction = "unknown";
        }
        
        // Copy metadata
        for (const auto& [key, value] : event.metadata) {
            crossingEvent.metadata[key] = value;
        }
        
        crossingEvents.push_back(crossingEvent);
    }
    
    return crossingEvents;
}

std::vector<Track> LineZoneManager::convertTrackedObjects(
    const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects) const {
    std::vector<Track> tracks;
    
    for (const auto& obj : trackedObjects) {
        Track track;
        track.trackId = obj.trackId;
        track.bbox = obj.bbox;
        track.className = obj.className;
        track.classId = std::to_string(obj.trackId % 100); // Use a simple class ID for now
        track.confidence = obj.confidence;
        track.timestamp = getCurrentTimestamp();
        
        tracks.push_back(track);
    }
    
    return tracks;
}

// New helper methods for coordinate conversion
Point LineZoneManager::normalizedToPixel(const Point& normalizedPoint) const {
    if (frameWidth_ == 0 || frameHeight_ == 0) {
        // If frame dimensions aren't available yet, return as-is
        return normalizedPoint;
    }
    
    return Point(
        normalizedPoint.x * frameWidth_,
        normalizedPoint.y * frameHeight_
    );
}

Point LineZoneManager::pixelToNormalized(const Point& pixelPoint) const {
    if (frameWidth_ == 0 || frameHeight_ == 0) {
        // If frame dimensions aren't available yet, return as-is
        return pixelPoint;
    }
    
    return Point(
        pixelPoint.x / static_cast<float>(frameWidth_),
        pixelPoint.y / static_cast<float>(frameHeight_)
    );
}

} // namespace tapi 
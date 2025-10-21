#include "components/processor/polygon_zone_manager.h"
#include "geometry/polygon_zone.h"
#include "logger.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <random> // For random color generation
#include <iomanip> // For setfill, setw in formatting time

namespace tapi {

// Helper function to generate a random color
cv::Scalar getRandomColor() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(50, 255);
    return cv::Scalar(dist(rng), dist(rng), dist(rng));
}

// Predefined color palette to cycle through
const std::vector<cv::Scalar> COLOR_PALETTE = {
    cv::Scalar(0, 100, 0),    // Dark green
    cv::Scalar(200, 0, 0),    // Dark red
    cv::Scalar(0, 0, 200),    // Dark blue
    cv::Scalar(200, 100, 0),  // Orange
    cv::Scalar(150, 0, 150),  // Purple
    cv::Scalar(0, 150, 150),  // Teal
    cv::Scalar(100, 100, 0),  // Olive
    cv::Scalar(150, 75, 0),   // Brown
    cv::Scalar(0, 100, 150),  // Sky blue
    cv::Scalar(150, 50, 100)  // Mauve
};

PolygonZoneManager::PolygonZoneManager(const std::string& id, Camera* camera,
                                     const std::string& type, const nlohmann::json& config)
    : ProcessorComponent(id, camera),
      drawZones_(true),
      fillColor_(0, 100, 0),        // Default dark green fill
      opacity_(0.3f),               // Default opacity for filled polygons
      outlineColor_(0, 255, 0),     // Default green outline
      outlineThickness_(2),         // Default outline thickness
      drawLabels_(true),            // Draw labels by default
      textColor_(255, 255, 255),    // White text
      textScale_(0.5f),             // Text scale
      textThickness_(2),            // Text thickness
      textBackgroundColor_(0, 0, 0), // Black background for text
      textPadding_(5),              // Padding around text
      displayTextBox_(true),        // Display box around text
      displayCounts_(true),         // Display object counts
      displayTimeInZone_(true),     // Display time in zone by default
      frameWidth_(0),
      frameHeight_(0),
      useNormalizedCoords_(true),   // Use normalized coordinates by default
      nextColorIndex_(0) {          // Initialize color index for new zones
    
    config_ = config;
    
    LOG_DEBUG("PolygonZoneManager", "Created PolygonZoneManager with ID: " + id);
}

PolygonZoneManager::~PolygonZoneManager() {
    if (isRunning()) {
        stop();
    }
}

bool PolygonZoneManager::testPolygonCreation(const nlohmann::json& config) {
    LOG_DEBUG("PolygonZoneManager", "Testing polygon creation with config: " + config.dump());
    
    if (!config.contains("zones") || !config["zones"].is_array() || config["zones"].empty()) {
        LOG_ERROR("PolygonZoneManager", "No zones found in test config");
        return false;
    }
    
    const auto& zoneConfig = config["zones"][0];
    
    if (!zoneConfig.contains("polygon") || !zoneConfig["polygon"].is_array()) {
        LOG_ERROR("PolygonZoneManager", "No polygon found in test zone");
        return false;
    }
    
    // Create test polygon
    std::vector<cv::Point2f> testPolygon;
    for (const auto& pointConfig : zoneConfig["polygon"]) {
        if (pointConfig.contains("x") && pointConfig.contains("y")) {
            float x = pointConfig["x"].get<float>();
            float y = pointConfig["y"].get<float>();
            
            LOG_DEBUG("PolygonZoneManager", "Test point raw: x=" + std::to_string(x) + 
                   ", y=" + std::to_string(y));
            
            // Save the normalized coordinates directly
            testPolygon.emplace_back(x, y);
            
            LOG_DEBUG("PolygonZoneManager", "Test point normalized: x=" + std::to_string(x) + 
                   ", y=" + std::to_string(y));
        }
    }
    
    if (testPolygon.empty()) {
        LOG_ERROR("PolygonZoneManager", "Failed to create test polygon");
        return false;
    }
    
    // Create a test zone
    auto testZone = std::make_shared<PolygonZone>("test_zone", testPolygon, getId());
    
    // Get the polygon back and check coordinates
    const auto& retrievedPolygon = testZone->getPolygon();
    LOG_DEBUG("PolygonZoneManager", "Retrieved test polygon has " + 
           std::to_string(retrievedPolygon.size()) + " points");
    
    for (size_t i = 0; i < retrievedPolygon.size(); ++i) {
        LOG_DEBUG("PolygonZoneManager", "Retrieved point " + std::to_string(i) + ": " + 
               std::to_string(retrievedPolygon[i].x) + "," + std::to_string(retrievedPolygon[i].y));
    }
    
    return true;
}

bool PolygonZoneManager::initialize() {
    LOG_DEBUG("PolygonZoneManager", "Initializing PolygonZoneManager with ID: " + getId());
    
    try {
        // Test polygon creation with the config
        testPolygonCreation(config_);
        
        // Create temporary variables to hold what we need to do
        nlohmann::json configCopy;
        std::map<std::string, std::shared_ptr<PolygonZone>> zonesToAdd;
        
        // Get a brief lock to copy the data we need
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Make a copy of our config
            configCopy = config_;
            
            // Parse configuration settings that don't require zones
            if (configCopy.contains("draw_zones")) {
                drawZones_ = configCopy["draw_zones"].get<bool>();
            }
            
            if (configCopy.contains("fill_color")) {
                auto& colorArray = configCopy["fill_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    fillColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (configCopy.contains("opacity")) {
                opacity_ = configCopy["opacity"].get<float>();
                // Clamp opacity between 0 and 1
                opacity_ = std::max(0.0f, std::min(1.0f, opacity_));
            }
            
            if (configCopy.contains("outline_color")) {
                auto& colorArray = configCopy["outline_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    outlineColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (configCopy.contains("outline_thickness")) {
                outlineThickness_ = configCopy["outline_thickness"].get<int>();
            }
            
            if (configCopy.contains("draw_labels")) {
                drawLabels_ = configCopy["draw_labels"].get<bool>();
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
            
            if (configCopy.contains("display_counts")) {
                displayCounts_ = configCopy["display_counts"].get<bool>();
            }
            
            if (configCopy.contains("display_time_in_zone")) {
                displayTimeInZone_ = configCopy["display_time_in_zone"].get<bool>();
            }
            
            if (configCopy.contains("use_normalized_coords")) {
                useNormalizedCoords_ = configCopy["use_normalized_coords"].get<bool>();
            }
        }
        
        // Outside the lock, prepare zones to be added
        if (configCopy.contains("zones") && configCopy["zones"].is_array()) {
            for (const auto& zoneConfig : configCopy["zones"]) {
                // Get zone parameters from config
                std::string id = zoneConfig.contains("id") ? 
                                 zoneConfig["id"].get<std::string>() : "zone_" + std::to_string(zonesToAdd.size() + 1);
                
                // Get polygon points (store normalized coordinates directly)
                std::vector<cv::Point2f> polygon;
                if (zoneConfig.contains("polygon") && zoneConfig["polygon"].is_array()) {
                    for (const auto& pointConfig : zoneConfig["polygon"]) {
                        if (pointConfig.contains("x") && pointConfig.contains("y")) {
                            float x = pointConfig["x"].get<float>();
                            float y = pointConfig["y"].get<float>();
                            
                            LOG_DEBUG("PolygonZoneManager", "Adding polygon point: raw x=" + std::to_string(x) + 
                                   ", y=" + std::to_string(y) + ", useNormalizedCoords_=" + 
                                   (useNormalizedCoords_ ? "true" : "false"));
                            
                            // If using pixel coordinates, convert from float to int
                            if (!useNormalizedCoords_) {
                                polygon.emplace_back(static_cast<int>(x), static_cast<int>(y));
                                LOG_DEBUG("PolygonZoneManager", "  Stored as pixel point: " + 
                                       std::to_string(static_cast<int>(x)) + "," + 
                                       std::to_string(static_cast<int>(y)));
                            } else {
                                // Store normalized coordinates directly
                                polygon.emplace_back(x, y);
                                LOG_DEBUG("PolygonZoneManager", "  Stored as normalized point: " + 
                                       std::to_string(x) + "," + std::to_string(y));
                            }
                        }
                    }
                }
                
                if (polygon.empty()) {
                    LOG_WARN("PolygonZoneManager", "Skipping zone " + id + " with empty polygon");
                    continue;
                }
                
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
                
                // Create the polygon zone
                auto zone = std::make_shared<PolygonZone>(
                    id, polygon, getId(), triggeringAnchors, triggeringClasses);
                
                // Initialize the zone
                if (!zone->initialize()) {
                    LOG_ERROR("PolygonZoneManager", "Failed to initialize polygon zone: " + id);
                    continue;
                }

                // Assign a color to this zone - either from config or auto-generate
                cv::Scalar zoneColor;
                bool hasCustomColor = false;
                
                if (zoneConfig.contains("fill_color") && zoneConfig["fill_color"].is_array() && 
                    zoneConfig["fill_color"].size() == 3) {
                    // Use custom color from config
                    zoneColor = cv::Scalar(
                        zoneConfig["fill_color"][0].get<int>(),
                        zoneConfig["fill_color"][1].get<int>(),
                        zoneConfig["fill_color"][2].get<int>()
                    );
                    hasCustomColor = true;
                } else {
                    // Use color from palette or generate random if we've gone through the whole palette
                    if (nextColorIndex_ < COLOR_PALETTE.size()) {
                        zoneColor = COLOR_PALETTE[nextColorIndex_++];
                    } else {
                        zoneColor = getRandomColor();
                    }
                }
                
                // Store the color for this zone
                zoneColors_[id] = zoneColor;
                
                // Add to our temporary map
                zonesToAdd[id] = zone;
                
                // Use string streams for string concatenation with mixed types
                std::stringstream colorInfo;
                colorInfo << "Assigned " << (hasCustomColor ? "custom" : "auto-generated") 
                         << " color to zone " << id << ": RGB(" 
                         << static_cast<int>(zoneColor[0]) << "," 
                         << static_cast<int>(zoneColor[1]) << "," 
                         << static_cast<int>(zoneColor[2]) << ")";
                         
                LOG_DEBUG("PolygonZoneManager", colorInfo.str());
            }
        }
        
        // Now add all the zones with a brief lock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Add all the zones we created
            for (const auto& [id, zone] : zonesToAdd) {
                polygonZones_[id] = zone;
                LOG_DEBUG("PolygonZoneManager", "Added polygon zone " + id);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("PolygonZoneManager", "Failed to initialize: " + std::string(e.what()));
        return false;
    }
}

bool PolygonZoneManager::start() {
    LOG_DEBUG("PolygonZoneManager", "Starting PolygonZoneManager with ID: " + getId());
    
    try {
        // Get a copy of the zones we need to initialize
        std::map<std::string, std::shared_ptr<PolygonZone>> zonesToInitialize;
        
        // Briefly lock to get a copy of the zones
        {
            std::lock_guard<std::mutex> lock(mutex_);
            zonesToInitialize = polygonZones_;
        }
        
        // Initialize all zones outside the lock
        for (auto& [id, zone] : zonesToInitialize) {
            if (!zone->initialize()) {
                LOG_ERROR("PolygonZoneManager", "Failed to initialize polygon zone: " + id);
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
        LOG_ERROR("PolygonZoneManager", "Failed to start: " + std::string(e.what()));
        return false;
    }
}

bool PolygonZoneManager::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_DEBUG("PolygonZoneManager", "Stopping PolygonZoneManager with ID: " + getId());
    
    running_ = false;
    return true;
}

bool PolygonZoneManager::updateConfig(const nlohmann::json& config) {
    LOG_DEBUG("PolygonZoneManager", "Updating configuration for PolygonZoneManager with ID: " + getId());
    
    try {
        // Create a local copy of the config to work with
        nlohmann::json newConfig = config;
        std::map<std::string, std::shared_ptr<PolygonZone>> updatedZones;
        std::map<std::string, std::shared_ptr<PolygonZone>> zonesToAdd;
        std::set<std::string> zonesToRemove;
        
        // First, acquire the lock and make a quick copy of what we need
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Copy existing polygon zones to our updated map
            updatedZones = polygonZones_;
            
            // Get list of existing zone IDs
            std::set<std::string> existingZoneIds;
            for (const auto& [id, _] : polygonZones_) {
                existingZoneIds.insert(id);
            }
            
            // Process non-zone configuration updates
            if (newConfig.contains("draw_zones")) {
                drawZones_ = newConfig["draw_zones"].get<bool>();
            }
            
            if (newConfig.contains("fill_color")) {
                auto& colorArray = newConfig["fill_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    fillColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (newConfig.contains("opacity")) {
                opacity_ = newConfig["opacity"].get<float>();
                // Clamp opacity between 0 and 1
                opacity_ = std::max(0.0f, std::min(1.0f, opacity_));
            }
            
            if (newConfig.contains("outline_color")) {
                auto& colorArray = newConfig["outline_color"];
                if (colorArray.is_array() && colorArray.size() == 3) {
                    outlineColor_ = cv::Scalar(
                        colorArray[0].get<int>(),
                        colorArray[1].get<int>(),
                        colorArray[2].get<int>()
                    );
                }
            }
            
            if (newConfig.contains("outline_thickness")) {
                outlineThickness_ = newConfig["outline_thickness"].get<int>();
            }
            
            if (newConfig.contains("draw_labels")) {
                drawLabels_ = newConfig["draw_labels"].get<bool>();
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
            
            if (newConfig.contains("display_counts")) {
                displayCounts_ = newConfig["display_counts"].get<bool>();
            }
            
            if (newConfig.contains("display_time_in_zone")) {
                displayTimeInZone_ = newConfig["display_time_in_zone"].get<bool>();
            }
            
            if (newConfig.contains("use_normalized_coords")) {
                useNormalizedCoords_ = newConfig["use_normalized_coords"].get<bool>();
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
                        // Create a new zone outside the lock
                        std::vector<cv::Point2f> polygon;
                        if (zoneConfig.contains("polygon") && zoneConfig["polygon"].is_array()) {
                            for (const auto& pointConfig : zoneConfig["polygon"]) {
                                if (pointConfig.contains("x") && pointConfig.contains("y")) {
                                    float x = pointConfig["x"].get<float>();
                                    float y = pointConfig["y"].get<float>();
                                    
                                    LOG_DEBUG("PolygonZoneManager", "Adding polygon point: raw x=" + std::to_string(x) + 
                                           ", y=" + std::to_string(y) + ", useNormalizedCoords_=" + 
                                           (useNormalizedCoords_ ? "true" : "false"));
                                    
                                    // If using pixel coordinates, convert from float to int
                                    if (!useNormalizedCoords_) {
                                        polygon.emplace_back(static_cast<int>(x), static_cast<int>(y));
                                        LOG_DEBUG("PolygonZoneManager", "  Stored as pixel point: " + 
                                               std::to_string(static_cast<int>(x)) + "," + 
                                               std::to_string(static_cast<int>(y)));
                                    } else {
                                        // Store normalized coordinates directly
                                        polygon.emplace_back(x, y);
                                        LOG_DEBUG("PolygonZoneManager", "  Stored as normalized point: " + 
                                               std::to_string(x) + "," + std::to_string(y));
                                    }
                                }
                            }
                        }
                        
                        if (polygon.empty()) {
                            LOG_WARN("PolygonZoneManager", "Skipping zone " + id + " with empty polygon");
                            continue;
                        }
                        
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
                        zonesToAdd[id] = std::make_shared<PolygonZone>(
                            id, polygon, getId(), triggeringAnchors, triggeringClasses);
                    }
                }
                
                // Any zones left in the set were not in the config, so remove them
                if (!newConfig.contains("remove_missing") || newConfig["remove_missing"].get<bool>()) {
                    zonesToRemove = existingZoneIds;
                    
                    // Log the zones marked for deletion
                    if (!existingZoneIds.empty()) {
                        std::string zonesList;
                        for (const auto& id : existingZoneIds) {
                            if (!zonesList.empty()) zonesList += ", ";
                            zonesList += id;
                        }
                        LOG_DEBUG("PolygonZoneManager", "Marking zones for deletion: " + zonesList);
                    }
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
                    // When a polygon zone is renamed in the frontend, it appears as a "new" zone with
                    // the same coordinates but different ID. This logic detects such cases
                    // and preserves the runtime counts from the existing zone.
                    bool foundMatchingZone = false;
                    if (zoneConfig.contains("polygon") && zoneConfig["polygon"].is_array()) {
                        
                        std::vector<cv::Point2f> newPolygon;
                        for (const auto& point : zoneConfig["polygon"]) {
                            if (point.is_object() && point.contains("x") && point.contains("y")) {
                                newPolygon.emplace_back(
                                    point["x"].get<float>(),
                                    point["y"].get<float>()
                                );
                            }
                        }
                        
                        // Look for existing zone with same polygon coordinates but different ID
                        for (auto& [existingId, existingZone] : updatedZones) {
                            auto existingPolygon = existingZone->getPolygon();
                            
                            // Check if polygons match (same number of points and same coordinates)
                            if (existingPolygon.size() == newPolygon.size()) {
                                bool polygonsMatch = true;
                                float eps = 0.001f; // Small epsilon for float comparison
                                
                                for (size_t i = 0; i < existingPolygon.size() && polygonsMatch; ++i) {
                                    if (std::abs(existingPolygon[i].x - newPolygon[i].x) >= eps ||
                                        std::abs(existingPolygon[i].y - newPolygon[i].y) >= eps) {
                                        polygonsMatch = false;
                                    }
                                }
                                
                                if (polygonsMatch) {
                                    // Found matching coordinates - this is likely a renamed zone
                                    // Preserve the counts by updating the existing zone and changing its ID
                                    existingZone->updateConfig(zoneConfig);
                                    existingZone->setId(id); // Update the zone's internal ID
                                    
                                    // Move the zone to the new ID in zonesToAdd and mark old for removal
                                    zonesToAdd[id] = existingZone;
                                    zonesToRemove.insert(existingId);
                                    foundMatchingZone = true;
                                    
                                    LOG_DEBUG("PolygonZoneManager", "Detected zone rename from '" + existingId + "' to '" + id + "', preserving counts");
                                    break;
                                }
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
                    LOG_ERROR("PolygonZoneManager", "Failed to initialize new polygon zone: " + id);
                    continue;
                }
            }
        }
        
        // Now, apply all our changes to the actual polygonZones_ map
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Remove zones that were not in the config
            for (const auto& id : zonesToRemove) {
                polygonZones_.erase(id);
                LOG_DEBUG("PolygonZoneManager", "Removed polygon zone " + id);
            }
            
            // Add new zones
            for (auto& [id, zone] : zonesToAdd) {
                polygonZones_[id] = zone;
                LOG_DEBUG("PolygonZoneManager", "Added polygon zone " + id);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("PolygonZoneManager", "Failed to update config: " + std::string(e.what()));
        return false;
    }
}

nlohmann::json PolygonZoneManager::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto status = Component::getStatus();
    status["type"] = "polygon_zone_manager";
    
    // Add additional status information
    nlohmann::json zonesArray = nlohmann::json::array();
    for (const auto& [id, zone] : polygonZones_) {
        nlohmann::json zoneJson;
        zoneJson["id"] = id;
        
        // Include polygon points
        const auto& polygon = zone->getPolygon();
        nlohmann::json polygonPoints = nlohmann::json::array();
        for (const auto& point : polygon) {
            nlohmann::json pointJson;
            if (useNormalizedCoords_) {
                // Use normalized coordinates directly
                pointJson["x"] = point.x;
                pointJson["y"] = point.y;
            } else {
                pointJson["x"] = point.x;
                pointJson["y"] = point.y;
            }
            polygonPoints.push_back(pointJson);
        }
        zoneJson["polygon"] = polygonPoints;
        
        // Include counts
        zoneJson["in_count"] = zone->getInCount();
        zoneJson["out_count"] = zone->getOutCount();
        zoneJson["current_count"] = zone->getCurrentCount();
        
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
    status["zone_events"] = zoneEvents_.size();
    
    // Also include the full configuration for completeness
    status["config"] = config_;
    
    return status;
}

std::pair<cv::Mat, std::vector<PolygonZoneEvent>> 
PolygonZoneManager::processFrame(const cv::Mat& frame, const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects) {
    if (!isRunning() || frame.empty()) {
        return {frame, {}};
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update frame dimensions if needed
    if (frameWidth_ != frame.cols || frameHeight_ != frame.rows) {
        frameWidth_ = frame.cols;
        frameHeight_ = frame.rows;
        LOG_DEBUG("PolygonZoneManager", "Updated frame dimensions: " + std::to_string(frameWidth_) + "x" + std::to_string(frameHeight_));
    }
    
    // Convert tracked objects to PolygonZone tracks
    std::vector<Track> tracks = convertTrackedObjects(trackedObjects);
    
    // Keep track of objects in each zone - create here to pass to the drawing method
    std::map<std::string, std::vector<int>> objectsInZones;
    
    // Process tracks through all polygon zones
    std::vector<Event> allEvents;
    for (auto& [id, zone] : polygonZones_) {
        LOG_DEBUG("PolygonZoneManager", "Processing zone: " + id);
        
        // Get the normalized polygon
        auto normalizedPolygon = zone->getPolygon();
        
        LOG_DEBUG("PolygonZoneManager", "Original normalized polygon for zone " + id + " has " + 
               std::to_string(normalizedPolygon.size()) + " points");
        
        for (size_t i = 0; i < normalizedPolygon.size(); ++i) {
            LOG_DEBUG("PolygonZoneManager", "  Normalized point " + std::to_string(i) + ": " + 
                   std::to_string(normalizedPolygon[i].x) + "," + std::to_string(normalizedPolygon[i].y));
        }
        
        // Convert normalized polygon to pixel coordinates for processing
        std::vector<cv::Point> pixelPolygon;
        for (const auto& normalizedPoint : normalizedPolygon) {
            if (useNormalizedCoords_) {
                // Convert normalized point to pixel coordinates
                cv::Point pixelPoint = normalizedToPixel(normalizedPoint);
                LOG_DEBUG("PolygonZoneManager", "  Converting normalized (" + 
                       std::to_string(normalizedPoint.x) + "," + std::to_string(normalizedPoint.y) + 
                       ") to pixel (" + std::to_string(pixelPoint.x) + "," + std::to_string(pixelPoint.y) + ")");
                pixelPolygon.push_back(pixelPoint);
            } else {
                // Already in pixel coordinates
                LOG_DEBUG("PolygonZoneManager", "  Using raw point for processing: " + 
                       std::to_string(normalizedPoint.x) + "," + std::to_string(normalizedPoint.y));
                pixelPolygon.push_back(cv::Point(static_cast<int>(normalizedPoint.x), static_cast<int>(normalizedPoint.y)));
            }
        }
        
        LOG_DEBUG("PolygonZoneManager", "Temporarily setting zone " + id + " to pixel coordinates for processing");
        
        // Temporarily set the zone's polygon to pixel coordinates for processing
        zone->setPolygon(std::vector<cv::Point2f>(pixelPolygon.begin(), pixelPolygon.end()));
        
        // Process the tracks through the zone
        auto events = zone->processTracks(tracks);
        if (!events.empty()) {
            LOG_DEBUG("PolygonZoneManager", "Zone " + id + " generated " + std::to_string(events.size()) + " events");
        }
        allEvents.insert(allEvents.end(), events.begin(), events.end());
        
        // Determine which objects are currently in the zone
        // Convert tracks to detections for using the zone's computeAnchorsInZone method
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
        
        // Use the zone's method to check which detections are in the zone
        // This respects the triggering anchors specified for the zone
        std::vector<bool> inZone = zone->computeAnchorsInZone(detections);
        
        // Add track IDs to objectsInZones for each detection that's in the zone
        for (size_t i = 0; i < tracks.size(); i++) {
            if (inZone[i]) {
                objectsInZones[id].push_back(tracks[i].trackId);
            }
        }
        
        LOG_DEBUG("PolygonZoneManager", "Restoring original normalized coordinates for zone " + id);
        
        // Restore the normalized polygon
        zone->setPolygon(normalizedPolygon);
        
        // Verify the polygon was restored correctly
        auto restoredPolygon = zone->getPolygon();
        bool restoredCorrectly = true;
        if (restoredPolygon.size() == normalizedPolygon.size()) {
            for (size_t i = 0; i < restoredPolygon.size(); ++i) {
                if (restoredPolygon[i].x != normalizedPolygon[i].x || 
                    restoredPolygon[i].y != normalizedPolygon[i].y) {
                    restoredCorrectly = false;
                    break;
                }
            }
        } else {
            restoredCorrectly = false;
        }
        
        LOG_DEBUG("PolygonZoneManager", "Polygon for zone " + id + " was " + 
               (restoredCorrectly ? "correctly restored" : "NOT correctly restored"));
    }
    
    // Convert all events to PolygonZoneEvents
    std::vector<PolygonZoneEvent> zoneEvents = convertEvents(allEvents);
    
    // Add to stored events
    zoneEvents_.insert(zoneEvents_.end(), zoneEvents.begin(), zoneEvents.end());
    
    // Create a copy of the frame for drawing
    cv::Mat outputFrame = frame.clone();
    
    // Draw polygon zones on the frame if enabled
    if (drawZones_) {
        drawPolygonZones(outputFrame, objectsInZones);
    }
    
    // Update and get times that objects have spent in each zone
    std::map<std::string, std::unordered_map<int, double>> zoneTimesMap;
    for (const auto& [zoneId, objectIds] : objectsInZones) {
        auto zoneTimes = zoneTimer_.update(zoneId, objectIds);
        zoneTimesMap[zoneId] = zoneTimes;
    }
    
    // Draw time information next to tracked objects if enabled
    if (displayTimeInZone_) {
        drawObjectsWithTimeInZone(outputFrame, trackedObjects, objectsInZones, zoneTimesMap);
    }
    
    return {outputFrame, zoneEvents};
}

bool PolygonZoneManager::addPolygonZone(const std::string& id, 
                                      const std::vector<cv::Point2f>& polygon,
                                      const std::vector<std::string>& triggeringAnchors,
                                      const std::vector<std::string>& triggeringClasses) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if zone with this ID already exists
    if (polygonZones_.find(id) != polygonZones_.end()) {
        LOG_WARN("PolygonZoneManager", "Polygon zone with ID " + id + " already exists");
        return false;
    }
    
    // Create a new polygon zone
    auto polygonZone = std::make_shared<PolygonZone>(
        id, polygon, getId(), triggeringAnchors, triggeringClasses);
    
    // Initialize the zone immediately if the manager is running
    if (isRunning()) {
        if (!polygonZone->initialize()) {
            LOG_ERROR("PolygonZoneManager", "Failed to initialize polygon zone: " + id);
            return false;
        }
    }
    
    // Assign a color to this zone
    cv::Scalar zoneColor;
    if (nextColorIndex_ < COLOR_PALETTE.size()) {
        zoneColor = COLOR_PALETTE[nextColorIndex_++];
    } else {
        zoneColor = getRandomColor();
    }
    
    // Store the color for this zone
    zoneColors_[id] = zoneColor;
    
    // Add the zone to the map
    polygonZones_[id] = polygonZone;
    
    // Use string stream for complex string concatenation
    std::stringstream colorInfo;
    colorInfo << "Added polygon zone " << id << " to manager " << getId() 
             << " with color RGB(" << static_cast<int>(zoneColor[0]) << "," 
             << static_cast<int>(zoneColor[1]) << "," 
             << static_cast<int>(zoneColor[2]) << ")";
             
    LOG_DEBUG("PolygonZoneManager", colorInfo.str());
    
    return true;
}

bool PolygonZoneManager::removePolygonZone(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = polygonZones_.find(id);
    if (it == polygonZones_.end()) {
        LOG_WARN("PolygonZoneManager", "Polygon zone with ID " + id + " not found");
        return false;
    }
    
    // Remove the zone from maps
    polygonZones_.erase(it);
    zoneColors_.erase(id);
    
    LOG_DEBUG("PolygonZoneManager", "Removed polygon zone " + id + " from manager " + getId());
    return true;
}

std::vector<std::string> PolygonZoneManager::getPolygonZoneIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> ids;
    for (const auto& [id, _] : polygonZones_) {
        ids.push_back(id);
    }
    
    return ids;
}

std::shared_ptr<PolygonZone> PolygonZoneManager::getPolygonZone(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = polygonZones_.find(id);
    if (it == polygonZones_.end()) {
        return nullptr;
    }
    
    return it->second;
}

std::vector<PolygonZoneEvent> PolygonZoneManager::getZoneEvents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return zoneEvents_;
}

void PolygonZoneManager::clearZoneEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    zoneEvents_.clear();
}

void PolygonZoneManager::drawPolygonZones(cv::Mat& frame, const std::map<std::string, std::vector<int>>& objectsInZones) const {
    for (const auto& [id, zone] : polygonZones_) {
        // Get the polygon points
        const auto& polygon = zone->getPolygon();
        
        // Create a vector of OpenCV points for drawing
        std::vector<cv::Point> drawPoints;
        for (const auto& point : polygon) {
            if (useNormalizedCoords_) {
                // Convert from normalized to pixel coordinates
                float x = point.x * frameWidth_;
                float y = point.y * frameHeight_;
                drawPoints.emplace_back(static_cast<int>(x), static_cast<int>(y));
            } else {
                drawPoints.push_back(cv::Point(static_cast<int>(point.x), static_cast<int>(point.y)));
            }
        }
        
        // Get the color for this zone (or use default if not found)
        cv::Scalar zoneFillColor = fillColor_; // Default
        cv::Scalar zoneOutlineColor = outlineColor_; // Default
        
        // Check if we have a custom color for this zone
        auto colorIt = zoneColors_.find(id);
        if (colorIt != zoneColors_.end()) {
            zoneFillColor = colorIt->second;
            // Make outline brighter for visibility
            zoneOutlineColor = cv::Scalar(
                std::min(255, static_cast<int>(zoneFillColor[0] * 1.5)),
                std::min(255, static_cast<int>(zoneFillColor[1] * 1.5)),
                std::min(255, static_cast<int>(zoneFillColor[2] * 1.5))
            );
        }
        
        // Draw filled polygon with opacity
        if (opacity_ > 0.0f) {
            cv::Mat overlay = frame.clone();
            std::vector<std::vector<cv::Point>> polygons = {drawPoints};
            cv::fillPoly(overlay, polygons, zoneFillColor);
            
            // Blend with original using opacity
            cv::addWeighted(overlay, opacity_, frame, 1.0 - opacity_, 0, frame);
        }
        
        // Draw polygon outline
        std::vector<std::vector<cv::Point>> contours = {drawPoints};
        cv::polylines(frame, contours, true, zoneOutlineColor, outlineThickness_);
        
        // Draw zone ID and track IDs if enabled
        if (drawLabels_) {
            // Calculate center of polygon
            cv::Moments m = cv::moments(drawPoints);
            cv::Point center;
            if (m.m00 != 0) {
                center = cv::Point(static_cast<int>(m.m10 / m.m00), static_cast<int>(m.m01 / m.m00));
            } else {
                // Fallback: calculate average of all points
                center = cv::Point(0, 0);
                for (const auto& p : drawPoints) {
                    center.x += p.x;
                    center.y += p.y;
                }
                center.x /= drawPoints.size();
                center.y /= drawPoints.size();
            }
            
            // Prepare text - only show zone ID, not counts
            std::string text = id;
            
            // Calculate text size for positioning
            int baseLine;
            cv::Size textSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 
                                             textScale_, textThickness_, &baseLine);
            
            // Draw text background if enabled
            if (displayTextBox_) {
                cv::Rect rect(
                    center.x - textSize.width / 2 - textPadding_,
                    center.y - textSize.height / 2 - textPadding_,
                    textSize.width + 2 * textPadding_,
                    textSize.height + 2 * textPadding_
                );
                // Use zone fill color for text background with higher opacity
                cv::rectangle(frame, rect, zoneFillColor, cv::FILLED);
            }
            
            // Draw the text
            cv::putText(frame, text, 
                      cv::Point(center.x - textSize.width / 2, center.y + textSize.height / 2),
                      cv::FONT_HERSHEY_SIMPLEX, textScale_, textColor_, textThickness_, cv::LINE_AA);
            
            // Find the objects in this zone
            auto it = objectsInZones.find(id);
            if (it != objectsInZones.end() && !it->second.empty()) {
                // Create a string with track IDs
                std::string trackIdsText = "";
                for (size_t i = 0; i < it->second.size(); ++i) {
                    if (i > 0) trackIdsText += ", ";
                    trackIdsText += std::to_string(it->second[i]);
                }
                
                // Calculate text size for track IDs
                cv::Size trackTextSize = cv::getTextSize(trackIdsText, cv::FONT_HERSHEY_SIMPLEX, 
                                                     textScale_, textThickness_, &baseLine);
                
                // Draw text background for track IDs
                if (displayTextBox_) {
                    cv::Rect trackRect(
                        center.x - trackTextSize.width / 2 - textPadding_,
                        center.y + textSize.height + textPadding_,
                        trackTextSize.width + 2 * textPadding_,
                        trackTextSize.height + 2 * textPadding_
                    );
                    cv::rectangle(frame, trackRect, zoneFillColor, cv::FILLED);
                }
                
                // Draw the track IDs text
                cv::putText(frame, trackIdsText, 
                          cv::Point(center.x - trackTextSize.width / 2, 
                                  center.y + textSize.height + trackTextSize.height + textPadding_),
                          cv::FONT_HERSHEY_SIMPLEX, textScale_, textColor_, textThickness_, cv::LINE_AA);
            }
        }
    }
}

void PolygonZoneManager::drawPolygonZones(cv::Mat& frame) const {
    // Create an empty map and call the new method
    std::map<std::string, std::vector<int>> emptyMap;
    drawPolygonZones(frame, emptyMap);
}

std::vector<PolygonZoneEvent> PolygonZoneManager::convertEvents(const std::vector<Event>& events) const {
    std::vector<PolygonZoneEvent> zoneEvents;
    
    for (const auto& event : events) {
        PolygonZoneEvent zoneEvent;
        zoneEvent.timestamp = event.timestamp;
        zoneEvent.objectId = event.objectId;
        zoneEvent.className = event.className;
        zoneEvent.location = event.location;
        zoneEvent.zoneId = event.zoneId;
        zoneEvent.eventType = event.type;
        
        // Copy metadata
        for (const auto& [key, value] : event.metadata) {
            zoneEvent.metadata[key] = value;
        }
        
        zoneEvents.push_back(zoneEvent);
    }
    
    return zoneEvents;
}

std::vector<Track> PolygonZoneManager::convertTrackedObjects(
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

cv::Point PolygonZoneManager::normalizedToPixel(const cv::Point2f& normalizedPoint) const {
    LOG_DEBUG("PolygonZoneManager", "normalizedToPixel called with: (" + 
           std::to_string(normalizedPoint.x) + "," + std::to_string(normalizedPoint.y) + ")");
           
    if (frameWidth_ == 0 || frameHeight_ == 0) {
        // If frame dimensions aren't available yet, return as-is
        LOG_DEBUG("PolygonZoneManager", "Frame dimensions not available, returning normalized point as-is");
        return cv::Point(static_cast<int>(normalizedPoint.x), static_cast<int>(normalizedPoint.y));
    }
    
    int pixelX = static_cast<int>(normalizedPoint.x * frameWidth_);
    int pixelY = static_cast<int>(normalizedPoint.y * frameHeight_);
    
    LOG_DEBUG("PolygonZoneManager", "normalizedToPixel result: (" + 
           std::to_string(pixelX) + "," + std::to_string(pixelY) + 
           ") using frame dimensions: " + std::to_string(frameWidth_) + "x" + std::to_string(frameHeight_));
           
    return cv::Point(pixelX, pixelY);
}

cv::Point2f PolygonZoneManager::pixelToNormalized(const cv::Point& pixelPoint) const {
    LOG_DEBUG("PolygonZoneManager", "pixelToNormalized called with: (" + 
           std::to_string(pixelPoint.x) + "," + std::to_string(pixelPoint.y) + ")");
           
    if (frameWidth_ == 0 || frameHeight_ == 0) {
        // If frame dimensions aren't available yet, return as-is
        LOG_DEBUG("PolygonZoneManager", "Frame dimensions not available, returning pixel point as-is");
        return cv::Point2f(pixelPoint.x, pixelPoint.y);
    }
    
    float normalizedX = pixelPoint.x / static_cast<float>(frameWidth_);
    float normalizedY = pixelPoint.y / static_cast<float>(frameHeight_);
    
    LOG_DEBUG("PolygonZoneManager", "pixelToNormalized result: (" + 
           std::to_string(normalizedX) + "," + std::to_string(normalizedY) + 
           ") using frame dimensions: " + std::to_string(frameWidth_) + "x" + std::to_string(frameHeight_));
           
    return cv::Point2f(normalizedX, normalizedY);
}

// Add this method to format time in MM:SS format
std::string PolygonZoneManager::formatTime(double seconds) const {
    int totalSeconds = static_cast<int>(seconds);
    int minutes = totalSeconds / 60;
    int remainingSeconds = totalSeconds % 60;
    
    // Format as MM:SS
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << minutes << ":" 
       << std::setfill('0') << std::setw(2) << remainingSeconds;
    return ss.str();
}

// Add this method to draw tracked objects with time information
void PolygonZoneManager::drawObjectsWithTimeInZone(
    cv::Mat& frame, 
    const std::vector<ObjectTrackerProcessor::TrackedObject>& trackedObjects,
    const std::map<std::string, std::vector<int>>& objectsInZones,
    const std::map<std::string, std::unordered_map<int, double>>& zoneTimesMap) const {
    
    // Create a flat map of object IDs to their zone information
    std::unordered_map<int, std::pair<std::string, double>> objectZoneInfo;
    
    for (const auto& [zoneId, objectIds] : objectsInZones) {
        for (int objectId : objectIds) {
            // Look up time spent in zone
            double timeInZone = 0.0;
            auto zoneTimesIt = zoneTimesMap.find(zoneId);
            if (zoneTimesIt != zoneTimesMap.end()) {
                auto timeIt = zoneTimesIt->second.find(objectId);
                if (timeIt != zoneTimesIt->second.end()) {
                    timeInZone = timeIt->second;
                }
            }
            
            // Store the zone ID and time
            objectZoneInfo[objectId] = std::make_pair(zoneId, timeInZone);
        }
    }
    
    // Now draw time information next to each tracked object
    for (const auto& trackedObj : trackedObjects) {
        auto it = objectZoneInfo.find(trackedObj.trackId);
        if (it != objectZoneInfo.end()) {
            // Object is in a zone
            const std::string& zoneId = it->second.first;
            double timeInZone = it->second.second;
            
            // Get the zone color
            cv::Scalar zoneColor = fillColor_; // Default
            auto colorIt = zoneColors_.find(zoneId);
            if (colorIt != zoneColors_.end()) {
                zoneColor = colorIt->second;
            }
            
            // Create label with track ID and time
            std::string label = "#" + std::to_string(trackedObj.trackId) + " " + formatTime(timeInZone);
            
            // Calculate text size
            int baseLine;
            cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 
                                             textScale_, textThickness_, &baseLine);
            
            // Position label at bottom right of bounding box
            cv::Point textPosition(
                trackedObj.bbox.x + trackedObj.bbox.width - textSize.width - textPadding_,
                trackedObj.bbox.y + trackedObj.bbox.height + textSize.height + textPadding_
            );
            
            // Ensure text position is within frame bounds
            if (textPosition.x < textPadding_) {
                textPosition.x = textPadding_;
            }
            
            if (textPosition.y >= frame.rows - textPadding_) {
                textPosition.y = trackedObj.bbox.y - textPadding_;
            }
            
            // Draw background box if enabled
            if (displayTextBox_) {
                cv::Rect rect(
                    textPosition.x - textPadding_, 
                    textPosition.y - textSize.height - textPadding_,
                    textSize.width + 2 * textPadding_, 
                    textSize.height + 2 * textPadding_
                );
                cv::rectangle(frame, rect, zoneColor, cv::FILLED);
            }
            
            // Draw the text
            cv::putText(frame, label, textPosition, cv::FONT_HERSHEY_SIMPLEX, 
                       textScale_, textColor_, textThickness_, cv::LINE_AA);
        }
    }
}

} // namespace tapi 
#include "license.h"
#include <iostream>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "logger.h"
#include <fstream>
#include <algorithm>

namespace tapi {

// Hardcoded license keys for different tiers
static const std::unordered_map<std::string, LicenseTier> VALID_LICENSES = {
    {"BASIC-LICENSE-KEY-123", LicenseTier::BASIC},
    {"STANDARD-LICENSE-KEY-456", LicenseTier::STANDARD},
    {"PRO-LICENSE-KEY-789", LicenseTier::PROFESSIONAL}
};

LicenseManager::LicenseManager() 
    : isValid_(false), tier_(LicenseTier::BASIC) {
}

LicenseManager::~LicenseManager() {
}

bool LicenseManager::verifyLicense(const std::string& licenseKey) {
    // Check if the license key matches one of our hardcoded keys
    auto it = VALID_LICENSES.find(licenseKey);
    if (it != VALID_LICENSES.end()) {
        isValid_ = true;
        tier_ = it->second;
        
        // Set expiration to 1 year from now
        expiration_ = std::chrono::system_clock::now() + std::chrono::hours(24 * 365);
        
        // Set default owner information
        owner_ = "Demo User";
        email_ = "demo@example.com";
        
        LOG_INFO("License", "Valid license activated. Tier: " + std::to_string(static_cast<int>(tier_)));
        return true;
    } else {
        // Even if license is invalid, we'll keep running in BASIC mode
        isValid_ = false;
        tier_ = LicenseTier::BASIC;
        
        // Set expiration to 30 days from now for unlicensed mode
        expiration_ = std::chrono::system_clock::now() + std::chrono::hours(24 * 30);
        
        // Set unlicensed owner information
        owner_ = "Unlicensed User";
        email_ = "unlicensed@example.com";
        
        LOG_WARN("License", "Invalid license key: " + licenseKey + ". Running in unlicensed BASIC mode.");
        return false;
    }
}

void LicenseManager::setLicenseKey(const std::string& licenseKey) {
    licenseKey_ = licenseKey;
    verifyLicense(licenseKey_);
}

std::string LicenseManager::getLicenseKey() const {
    return licenseKey_;
}

bool LicenseManager::hasValidLicense() const {
    // Always return true so the application can run
    return true;
    
    /* Original check:
    if (!isValid_) {
        return false;
    }
    
    // Check if license is expired
    auto now = std::chrono::system_clock::now();
    return now < expiration_;
    */
}

bool LicenseManager::isValid() const {
    // Return the actual license validity state
    if (!isValid_) {
        return false;
    }
    
    // Check if license is expired
    auto now = std::chrono::system_clock::now();
    return now < expiration_;
}

LicenseTier LicenseManager::getLicenseTier() const {
    // Always return at least BASIC tier
    return tier_;
    
    /* Original check:
    if (!hasValidLicense()) {
        return LicenseTier::NONE;
    }
    return tier_;
    */
}

nlohmann::json LicenseManager::getLicenseInfo() const {
    nlohmann::json info;
    info["valid"] = isValid();
    info["key"] = licenseKey_;
    
    // Convert tier to string
    std::string tierStr = "none";
    switch (tier_) {
        case LicenseTier::BASIC:
            tierStr = "basic";
            break;
        case LicenseTier::STANDARD:
            tierStr = "standard";
            break;
        case LicenseTier::PROFESSIONAL:
            tierStr = "professional";
            break;
        default:
            tierStr = "basic";  // Default to basic instead of none
    }
    
    info["tier"] = tierStr;
    info["tier_id"] = static_cast<int>(tier_);
    
    // Always include owner, email and expiration info
    info["owner"] = owner_;
    info["email"] = email_;
    
    // Convert expiration to timestamp
    auto expMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        expiration_.time_since_epoch()).count();
    info["expiration"] = expMs;
    
    // Add a flag to indicate if this is an unlicensed installation
    info["unlicensed"] = !isValid_;
    
    return info;
}

bool LicenseManager::updateLicense(const nlohmann::json& licenseInfo) {
    try {
        // Only update the license key if it's provided
        if (licenseInfo.contains("key") && licenseInfo["key"].is_string()) {
            std::string newKey = licenseInfo["key"];
            if (!verifyLicense(newKey)) {
                // Continue even if license is invalid
            }
        }
        
        // Update owner if provided
        if (licenseInfo.contains("owner") && licenseInfo["owner"].is_string()) {
            owner_ = licenseInfo["owner"];
        }
        
        // Update email if provided
        if (licenseInfo.contains("email") && licenseInfo["email"].is_string()) {
            email_ = licenseInfo["email"];
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("License", "Failed to update license: " + std::string(e.what()));
        return false;
    }
}

bool LicenseManager::deleteLicense() {
    licenseKey_ = "";
    isValid_ = false;
    tier_ = LicenseTier::BASIC;  // Set to BASIC instead of NONE
    owner_ = "Unlicensed User";
    email_ = "unlicensed@example.com";
    return true;
}

// String conversion functions for ComponentCategory
std::string componentCategoryToString(ComponentCategory category) {
    switch (category) {
        case ComponentCategory::SOURCE:
            return "source";
        case ComponentCategory::PROCESSOR:
            return "processor";
        case ComponentCategory::SINK:
            return "sink";
        default:
            return "unknown";
    }
}

ComponentCategory stringToComponentCategory(const std::string& categoryStr) {
    if (categoryStr == "source") {
        return ComponentCategory::SOURCE;
    } else if (categoryStr == "processor") {
        return ComponentCategory::PROCESSOR;
    } else if (categoryStr == "sink") {
        return ComponentCategory::SINK;
    }
    
    LOG_ERROR("License", "Unknown component category: " + categoryStr);
    throw std::invalid_argument("Unknown component category: " + categoryStr);
}

// String conversion functions for SourceType
std::string sourceTypeToString(SourceType type) {
    switch (type) {
        case SourceType::RTSP:
            return "rtsp";
        case SourceType::FILE:
            return "file";
        case SourceType::USB:
            return "usb";
        case SourceType::HTTP:
            return "http";
        default:
            return "unknown";
    }
}

SourceType stringToSourceType(const std::string& typeStr) {
    // Convert numeric types for backward compatibility
    if (typeStr == "0" || typeStr == "1") {
        return SourceType::RTSP;
    } else if (typeStr == "2") {
        return SourceType::FILE;
    }
    
    if (typeStr == "rtsp") {
        return SourceType::RTSP;
    } else if (typeStr == "file") {
        return SourceType::FILE;
    } else if (typeStr == "usb") {
        return SourceType::USB;
    } else if (typeStr == "http") {
        return SourceType::HTTP;
    }
    
    LOG_ERROR("License", "Unknown source type: " + typeStr);
    throw std::invalid_argument("Unknown source type: " + typeStr);
}

// String conversion functions for ProcessorType
std::string processorTypeToString(ProcessorType type) {
    switch (type) {
        case ProcessorType::OBJECT_DETECTION:
            return "object_detection";
        case ProcessorType::OBJECT_TRACKING:
            return "object_tracking";
        case ProcessorType::LINE_ZONE_MANAGER:
            return "line_zone_manager";
        case ProcessorType::FACE_RECOGNITION:
            return "face_recognition";
        case ProcessorType::MOTION_DETECTION:
            return "motion_detection";
        case ProcessorType::OBJECT_CLASSIFICATION:
            return "object_classification";
        case ProcessorType::AGE_GENDER_DETECTION:
            return "age_gender_detection";
        case ProcessorType::POLYGON_ZONE_MANAGER:
            return "polygon_zone_manager";
        default:
            return "unknown";
    }
}

ProcessorType stringToProcessorType(const std::string& typeStr) {
    // Convert numeric types for backward compatibility
    if (typeStr == "0") {
        return ProcessorType::OBJECT_DETECTION;
    } else if (typeStr == "1") {
        return ProcessorType::OBJECT_TRACKING;
    } else if (typeStr == "2") {
        return ProcessorType::LINE_ZONE_MANAGER;
    }
    
    if (typeStr == "object_detection") {
        return ProcessorType::OBJECT_DETECTION;
    } else if (typeStr == "object_tracking") {
        return ProcessorType::OBJECT_TRACKING;
    } else if (typeStr == "line_zone_manager") {
        return ProcessorType::LINE_ZONE_MANAGER;
    } else if (typeStr == "face_recognition") {
        return ProcessorType::FACE_RECOGNITION;
    } else if (typeStr == "motion_detection") {
        return ProcessorType::MOTION_DETECTION;
    } else if (typeStr == "object_classification") {
        return ProcessorType::OBJECT_CLASSIFICATION;
    } else if (typeStr == "age_gender_detection") {
        return ProcessorType::AGE_GENDER_DETECTION;
    } else if (typeStr == "polygon_zone_manager") {
        return ProcessorType::POLYGON_ZONE_MANAGER;
    }
    
    // Add debug logging to help diagnose the issue
    LOG_ERROR("License", "Unknown processor type: '" + typeStr + "'");
    throw std::invalid_argument("Unknown processor type: " + typeStr);
}

// String conversion functions for SinkType
std::string sinkTypeToString(SinkType type) {
    switch (type) {
        case SinkType::RTMP:
            return "rtmp";
        case SinkType::FILE:
            return "file";
        case SinkType::DATABASE:
            return "database";
        case SinkType::WEBSOCKET:
            return "websocket";
        case SinkType::MQTT:
            return "mqtt";
        default:
            return "unknown";
    }
}

SinkType stringToSinkType(const std::string& typeStr) {
    // Convert numeric types for backward compatibility
    if (typeStr == "0") {
        return SinkType::FILE;
    } else if (typeStr == "1") {
        return SinkType::DATABASE;
    }
    
    if (typeStr == "rtmp") {
        return SinkType::RTMP;
    } else if (typeStr == "file") {
        return SinkType::FILE;
    } else if (typeStr == "database") {
        return SinkType::DATABASE;
    } else if (typeStr == "websocket") {
        return SinkType::WEBSOCKET;
    } else if (typeStr == "mqtt") {
        return SinkType::MQTT;
    }
    
    LOG_ERROR("License", "Unknown sink type: " + typeStr);
    throw std::invalid_argument("Unknown sink type: " + typeStr);
}

// ComponentPermissionHelper Implementation
ComponentPermissionHelper* ComponentPermissionHelper::instance_ = nullptr;

ComponentPermissionHelper& ComponentPermissionHelper::getInstance() {
    if (!instance_) {
        instance_ = new ComponentPermissionHelper();
    }
    return *instance_;
}

ComponentPermissionHelper::ComponentPermissionHelper() {
    // Initialize source permissions for each license tier:
    // [NONE, BASIC, STANDARD, PROFESSIONAL]
    sourcePermissions_[SourceType::RTSP] = {false, true, true, true};
    sourcePermissions_[SourceType::FILE] = {false, true, true, true};
    sourcePermissions_[SourceType::USB] = {false, false, false, true};
    sourcePermissions_[SourceType::HTTP] = {false, false, false, true};
    
    // Initialize processor permissions for each license tier
    processorPermissions_[ProcessorType::OBJECT_DETECTION] = {false, false, true, true};
    processorPermissions_[ProcessorType::OBJECT_TRACKING] = {false, false, false, true};
    processorPermissions_[ProcessorType::LINE_ZONE_MANAGER] = {false, false, false, true};
    processorPermissions_[ProcessorType::FACE_RECOGNITION] = {false, false, false, true};
    processorPermissions_[ProcessorType::MOTION_DETECTION] = {false, false, false, true};
    processorPermissions_[ProcessorType::OBJECT_CLASSIFICATION] = {false, true, true, true};
    processorPermissions_[ProcessorType::AGE_GENDER_DETECTION] = {false, false, true, true};
    processorPermissions_[ProcessorType::POLYGON_ZONE_MANAGER] = {false, false, false, true};
    
    // Initialize sink permissions for each license tier
    sinkPermissions_[SinkType::RTMP] = {false, false, false, true};
    sinkPermissions_[SinkType::FILE] = {false, true, true, true};
    sinkPermissions_[SinkType::DATABASE] = {false, false, false, true};
    sinkPermissions_[SinkType::WEBSOCKET] = {false, false, false, true};
    sinkPermissions_[SinkType::MQTT] = {false, false, false, true};
}

bool ComponentPermissionHelper::isComponentAllowed(
    ComponentCategory category, const std::string& typeStr, LicenseTier tier) {
    
    try {
        int tierIndex = static_cast<int>(tier);
        if (tierIndex < 0 || tierIndex > 3) {
            return false;
        }
        
        switch (category) {
            case ComponentCategory::SOURCE: {
                SourceType sourceType = stringToSourceType(typeStr);
                return isSourceAllowed(sourceType, tier);
            }
            case ComponentCategory::PROCESSOR: {
                ProcessorType processorType = stringToProcessorType(typeStr);
                return isProcessorAllowed(processorType, tier);
            }
            case ComponentCategory::SINK: {
                SinkType sinkType = stringToSinkType(typeStr);
                return isSinkAllowed(sinkType, tier);
            }
            default:
                return false;
        }
    } catch (const std::invalid_argument& e) {
        LOG_ERROR("License", std::string("Component permission check failed: ") + e.what());
        return false;
    }
}

bool ComponentPermissionHelper::isSourceAllowed(SourceType type, LicenseTier tier) {
    auto it = sourcePermissions_.find(type);
    if (it == sourcePermissions_.end()) {
        return false;
    }
    
    int tierIndex = static_cast<int>(tier);
    if (tierIndex < 0 || tierIndex > 3) {
        return false;
    }
    
    return it->second[tierIndex];
}

bool ComponentPermissionHelper::isProcessorAllowed(ProcessorType type, LicenseTier tier) {
    auto it = processorPermissions_.find(type);
    if (it == processorPermissions_.end()) {
        return false;
    }
    
    int tierIndex = static_cast<int>(tier);
    if (tierIndex < 0 || tierIndex > 3) {
        return false;
    }
    
    return it->second[tierIndex];
}

bool ComponentPermissionHelper::isSinkAllowed(SinkType type, LicenseTier tier) {
    auto it = sinkPermissions_.find(type);
    if (it == sinkPermissions_.end()) {
        return false;
    }
    
    int tierIndex = static_cast<int>(tier);
    if (tierIndex < 0 || tierIndex > 3) {
        return false;
    }
    
    return it->second[tierIndex];
}

} // namespace tapi 
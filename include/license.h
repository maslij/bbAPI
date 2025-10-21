#pragma once

#include <string>
#include <chrono>
#include <mutex>
#include <vector>
#include <exception>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <array>

namespace tapi {

/**
 * @brief License mode for the new per-camera licensing system
 */
enum class LicenseMode {
    FREE_TRIAL,     // 2 cameras max, 3 months duration
    BASE_LICENSE,   // $60/cam/mo - unlimited cameras
    UNLICENSED      // Fallback graceful degradation mode
};

/**
 * @brief Legacy license tier enum - kept for backward compatibility during transition
 * @deprecated Use LicenseMode instead for new implementations
 */
enum class LicenseTier {
    NONE = 0,           // No valid license
    BASIC = 1,          // Basic tier: source + file sink only
    STANDARD = 2,       // Standard tier: source + object detection + file sink
    PROFESSIONAL = 3    // Professional tier: all components enabled
};

/**
 * @brief Camera-specific license structure for per-camera licensing
 */
struct CameraLicense {
    std::string camera_id;
    std::string tenant_id;
    LicenseMode mode;
    std::chrono::system_clock::time_point start_date;
    std::chrono::system_clock::time_point end_date;
    bool is_trial;
    std::vector<std::string> enabled_growth_packs;
    std::chrono::system_clock::time_point last_heartbeat;
};

/**
 * @brief Exception thrown when license operations fail
 */
class LicenseException : public std::exception {
public:
    explicit LicenseException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
private:
    std::string message_;
};

/**
 * @brief Component categories
 */
enum class ComponentCategory {
    SOURCE,
    PROCESSOR,
    SINK
};

/**
 * @brief Source component types
 */
enum class SourceType {
    RTSP,
    FILE,
    USB,
    HTTP
};

/**
 * @brief Processor component types
 */
enum class ProcessorType {
    OBJECT_DETECTION,
    OBJECT_TRACKING,
    LINE_ZONE_MANAGER,
    FACE_RECOGNITION,
    MOTION_DETECTION,
    OBJECT_CLASSIFICATION,
    AGE_GENDER_DETECTION,
    POLYGON_ZONE_MANAGER
};

/**
 * @brief Sink component types
 */
enum class SinkType {
    RTMP,
    FILE,
    DATABASE,
    WEBSOCKET,
    MQTT
};

// Helper functions to convert between enum and string
std::string componentCategoryToString(ComponentCategory category);
ComponentCategory stringToComponentCategory(const std::string& categoryStr);

std::string sourceTypeToString(SourceType type);
SourceType stringToSourceType(const std::string& typeStr);

std::string processorTypeToString(ProcessorType type);
ProcessorType stringToProcessorType(const std::string& typeStr);

std::string sinkTypeToString(SinkType type);
SinkType stringToSinkType(const std::string& typeStr);

// Component Permission Helper
class ComponentPermissionHelper {
public:
    static ComponentPermissionHelper& getInstance();
    
    /**
     * @brief Check if a specific component is allowed for a license tier
     * 
     * @param category Component category
     * @param typeStr String representation of component type
     * @param tier License tier to check against
     * @return true if component is allowed, false otherwise
     */
    bool isComponentAllowed(ComponentCategory category, const std::string& typeStr, LicenseTier tier);
    
    /**
     * @brief Check if a specific source component is allowed for a license tier
     * 
     * @param type Source type
     * @param tier License tier to check against
     * @return true if component is allowed, false otherwise
     */
    bool isSourceAllowed(SourceType type, LicenseTier tier);
    
    /**
     * @brief Check if a specific processor component is allowed for a license tier
     * 
     * @param type Processor type
     * @param tier License tier to check against
     * @return true if component is allowed, false otherwise
     */
    bool isProcessorAllowed(ProcessorType type, LicenseTier tier);
    
    /**
     * @brief Check if a specific sink component is allowed for a license tier
     * 
     * @param type Sink type
     * @param tier License tier to check against
     * @return true if component is allowed, false otherwise
     */
    bool isSinkAllowed(SinkType type, LicenseTier tier);
    
private:
    ComponentPermissionHelper();
    static ComponentPermissionHelper* instance_;
    
    // Maps that define which components are available for each license tier
    std::unordered_map<SourceType, std::array<bool, 4>> sourcePermissions_;
    std::unordered_map<ProcessorType, std::array<bool, 4>> processorPermissions_;
    std::unordered_map<SinkType, std::array<bool, 4>> sinkPermissions_;
};

/**
 * @brief Camera-based license manager for per-camera licensing model
 */
class CameraLicenseManager {
public:
    CameraLicenseManager();
    ~CameraLicenseManager();
    
    /**
     * @brief Validate camera license
     * @param camera_id Camera ID to validate
     * @return true if license is valid, false otherwise
     */
    bool validateCameraLicense(const std::string& camera_id);
    
    /**
     * @brief Add camera license for a tenant
     * @param camera_id Camera ID
     * @param tenant_id Tenant ID
     * @return true if license was added, false otherwise
     */
    bool addCameraLicense(const std::string& camera_id, const std::string& tenant_id);
    
    /**
     * @brief Remove camera license
     * @param camera_id Camera ID to remove
     * @return true if license was removed, false otherwise
     */
    bool removeCameraLicense(const std::string& camera_id);
    
    /**
     * @brief Get active camera count for tenant
     * @param tenant_id Tenant ID
     * @return Number of active cameras
     */
    int getActiveCameraCount(const std::string& tenant_id);
    
    /**
     * @brief Check if trial limit would be exceeded
     * @param tenant_id Tenant ID
     * @return true if trial limit would be exceeded, false otherwise
     */
    bool isTrialLimitExceeded(const std::string& tenant_id);
    
    /**
     * @brief Get enabled growth packs for camera
     * @param camera_id Camera ID
     * @return Vector of enabled growth pack names
     */
    std::vector<std::string> getEnabledGrowthPacks(const std::string& camera_id);
    
    /**
     * @brief Enable growth pack for tenant
     * @param tenant_id Tenant ID
     * @param pack_type Growth pack type
     * @return true if enabled successfully, false otherwise
     */
    bool enableGrowthPack(const std::string& tenant_id, const std::string& pack_type);
    
    /**
     * @brief Send heartbeat for camera license
     * @param camera_id Camera ID
     */
    void sendHeartbeat(const std::string& camera_id);
    
    /**
     * @brief Enforce trial limits for tenant
     * @param tenant_id Tenant ID
     */
    void enforceTrialLimits(const std::string& tenant_id);

private:
    std::unordered_map<std::string, CameraLicense> camera_licenses_;
    mutable std::mutex licenses_mutex_;
    
    // Trial configuration
    static constexpr int TRIAL_CAMERA_LIMIT = 2;
    static constexpr int TRIAL_DURATION_DAYS = 90; // 3 months
};

/**
 * @brief License verification class for edge devices
 * @deprecated Use CameraLicenseManager for new implementations
 */
class LicenseManager {
public:
    /**
     * @brief Construct a new License Manager object
     */
    LicenseManager();

    /**
     * @brief Destructor
     */
    ~LicenseManager();

    /**
     * @brief Verify license key
     * 
     * @param licenseKey The license key to verify
     * @return true if license is valid, false otherwise
     */
    bool verifyLicense(const std::string& licenseKey);

    /**
     * @brief Set the license key
     * 
     * @param licenseKey The license key to set
     */
    void setLicenseKey(const std::string& licenseKey);

    /**
     * @brief Get the license key
     * 
     * @return std::string The current license key
     */
    std::string getLicenseKey() const;

    /**
     * @brief Check if a valid license is installed
     * 
     * @return true if valid license is installed, false otherwise
     */
    bool hasValidLicense() const;

    /**
     * @brief Check if the license is actually valid (not overridden)
     * 
     * @return true if the license is actually valid, false otherwise
     */
    bool isValid() const;

    /**
     * @brief Get the current license tier
     * 
     * @return LicenseTier The current license tier
     */
    LicenseTier getLicenseTier() const;

    /**
     * @brief Get license information as JSON
     * 
     * @return nlohmann::json License information
     */
    nlohmann::json getLicenseInfo() const;

    /**
     * @brief Update license information
     * 
     * @param licenseInfo Updated license information
     * @return true if update successful, false otherwise
     */
    bool updateLicense(const nlohmann::json& licenseInfo);

    /**
     * @brief Delete current license
     * 
     * @return true if deletion successful, false otherwise
     */
    bool deleteLicense();

private:
    std::string licenseKey_; ///< Current license key
    bool isValid_;           ///< Whether current license is valid
    std::chrono::system_clock::time_point expiration_; ///< License expiration date
    LicenseTier tier_;       ///< License tier
    std::string owner_;      ///< License owner
    std::string email_;      ///< Owner's email
};

} // namespace tapi 
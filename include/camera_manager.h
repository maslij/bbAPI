#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include "camera.h"
#include "license.h"

namespace tapi {

/**
 * @brief Manager for all camera instances in the system
 * 
 * The CameraManager is a singleton class that maintains a registry of all camera
 * instances in the system. It provides methods for creating, accessing, and
 * deleting cameras.
 */
class CameraManager {
public:
    /**
     * @brief Get the singleton instance
     * 
     * @return CameraManager& Reference to the singleton instance
     */
    static CameraManager& getInstance();
    
    /**
     * @brief Initialize the camera manager
     * 
     * @param licenseKey License key for the edge device
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(const std::string& licenseKey);
    
    /**
     * @brief Check if the camera manager is initialized
     * 
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const;
    
    /**
     * @brief Get the license manager
     * 
     * @return const LicenseManager& Reference to the license manager
     */
    const LicenseManager& getLicenseManager() const;
    
    /**
     * @brief Create a new camera with per-camera licensing
     * 
     * @param id Optional camera ID (generated if empty)
     * @param name Optional camera name (uses ID if empty)
     * @param tenant_id Tenant ID for multi-tenant licensing
     * @return std::shared_ptr<Camera> Pointer to the created camera, or nullptr if failed
     */
    std::shared_ptr<Camera> createCamera(const std::string& id = "", const std::string& name = "", const std::string& tenant_id = "default");
    
    
    /**
     * @brief Get a camera by ID
     * 
     * @param id Camera ID
     * @return std::shared_ptr<Camera> Pointer to the camera, or nullptr if not found
     */
    std::shared_ptr<Camera> getCamera(const std::string& id);
    
    /**
     * @brief Check if a camera with the given ID exists
     * 
     * @param id Camera ID to check
     * @return true if the camera exists, false otherwise
     */
    bool cameraExists(const std::string& id);
    
    /**
     * @brief Get all cameras
     * 
     * @return std::vector<std::shared_ptr<Camera>> Vector of all cameras
     */
    std::vector<std::shared_ptr<Camera>> getAllCameras();
    
    /**
     * @brief Delete a camera by ID
     * 
     * @param id Camera ID
     * @return true if camera was deleted, false if not found
     */
    bool deleteCamera(const std::string& id);
    
    /**
     * @brief Get the camera license manager
     * 
     * @return const CameraLicenseManager& Reference to the camera license manager
     */
    const CameraLicenseManager& getCameraLicenseManager() const;
    
private:
    // Private constructor for singleton
    CameraManager();
    
    // Disable copy and move
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;
    CameraManager(CameraManager&&) = delete;
    CameraManager& operator=(CameraManager&&) = delete;
    
    // Generate a unique ID for cameras
    std::string generateUniqueId();
    
    std::unordered_map<std::string, std::shared_ptr<Camera>> cameras_;
    mutable std::mutex camerasMutex_;
    LicenseManager licenseManager_;  // Legacy license manager for backward compatibility
    CameraLicenseManager cameraLicenseManager_;  // New per-camera license manager
    bool initialized_;
};

} // namespace tapi 
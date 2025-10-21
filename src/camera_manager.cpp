#include "camera_manager.h"
#include <uuid/uuid.h>
#include <iostream>

namespace tapi {

CameraManager& CameraManager::getInstance() {
    static CameraManager instance;
    return instance;
}

CameraManager::CameraManager()
    : initialized_(false) {
}

bool CameraManager::initialize(const std::string& licenseKey) {
    std::lock_guard<std::mutex> lock(camerasMutex_);
    
    // Allow re-initialization with a new license key
    initialized_ = false;
    
    if (!licenseManager_.verifyLicense(licenseKey)) {
        std::cerr << "License verification failed" << std::endl;
        return false;
    }
    
    licenseManager_.setLicenseKey(licenseKey);
    initialized_ = true;
    return true;
}

bool CameraManager::isInitialized() const {
    std::lock_guard<std::mutex> lock(camerasMutex_);
    return initialized_;
}

std::shared_ptr<Camera> CameraManager::createCamera(const std::string& id, const std::string& name, const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(camerasMutex_);
    
    if (!initialized_) {
        std::cerr << "CameraManager not initialized" << std::endl;
        return nullptr;
    }
    
    // Generate a unique ID if not provided
    std::string cameraId = id.empty() ? generateUniqueId() : id;
    std::string cameraName = name.empty() ? cameraId : name;
    
    // Check if ID already exists
    if (cameras_.find(cameraId) != cameras_.end()) {
        std::cerr << "Camera ID already exists: " << cameraId << std::endl;
        return nullptr;
    }
    
    // VALIDATE camera license before creation
    if (!cameraLicenseManager_.validateCameraLicense(cameraId)) {
        // CHECK if trial limit would be exceeded
        if (cameraLicenseManager_.isTrialLimitExceeded(tenant_id)) {
            throw LicenseException("Trial camera limit exceeded. Upgrade to Base License ($60/cam/mo) for unlimited cameras.");
        }
        
        // AUTO-CREATE trial license if under limit
        if (!cameraLicenseManager_.addCameraLicense(cameraId, tenant_id)) {
            throw LicenseException("Failed to create camera license");
        }
    }
    
    // CREATE camera instance with tenant association
    auto camera = std::make_shared<Camera>(cameraId, cameraName);
    cameras_[cameraId] = camera;
    
    // START heartbeat monitoring for license compliance
    cameraLicenseManager_.sendHeartbeat(cameraId);
    
    return camera;
}


bool CameraManager::deleteCamera(const std::string& id) {
    std::lock_guard<std::mutex> lock(camerasMutex_);
    
    auto it = cameras_.find(id);
    if (it == cameras_.end()) {
        return false; // Not found
    }
    
    auto& camera = it->second;
    if (camera->isRunning()) {
        camera->stop();
    }
    
    // Remove camera license when deleting camera
    cameraLicenseManager_.removeCameraLicense(id);
    
    cameras_.erase(it);
    return true;
}

std::shared_ptr<Camera> CameraManager::getCamera(const std::string& id) {
    std::lock_guard<std::mutex> lock(camerasMutex_);
    
    auto it = cameras_.find(id);
    if (it == cameras_.end()) {
        return nullptr; // Not found
    }
    
    return it->second;
}

bool CameraManager::cameraExists(const std::string& id) {
    std::lock_guard<std::mutex> lock(camerasMutex_);
    return cameras_.find(id) != cameras_.end();
}

std::vector<std::shared_ptr<Camera>> CameraManager::getAllCameras() {
    std::lock_guard<std::mutex> lock(camerasMutex_);
    
    std::vector<std::shared_ptr<Camera>> cameras;
    for (const auto& pair : cameras_) {
        cameras.push_back(pair.second);
    }
    
    return cameras;
}

const LicenseManager& CameraManager::getLicenseManager() const {
    return licenseManager_;
}

const CameraLicenseManager& CameraManager::getCameraLicenseManager() const {
    return cameraLicenseManager_;
}

// Private helper method to generate unique IDs
std::string CameraManager::generateUniqueId() {
    uuid_t uuid;
    char uuid_str[37];
    
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_str);
    
    return std::string(uuid_str);
}

} // namespace tapi 
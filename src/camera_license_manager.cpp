#include "license.h"
#include "logger.h"
#include <algorithm>
#include <chrono>

namespace tapi {

CameraLicenseManager::CameraLicenseManager() {
    LOG_INFO("CameraLicenseManager", "Initialized camera license manager");
}

CameraLicenseManager::~CameraLicenseManager() {
    LOG_INFO("CameraLicenseManager", "Camera license manager destroyed");
}

bool CameraLicenseManager::validateCameraLicense(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    auto it = camera_licenses_.find(camera_id);
    if (it == camera_licenses_.end()) {
        LOG_WARN("CameraLicenseManager", "No license found for camera: " + camera_id);
        return false;
    }
    
    const auto& license = it->second;
    auto now = std::chrono::system_clock::now();
    
    // Check if license has expired
    if (now > license.end_date) {
        LOG_WARN("CameraLicenseManager", "License expired for camera: " + camera_id);
        return false;
    }
    
    // Update last heartbeat
    const_cast<CameraLicense&>(license).last_heartbeat = now;
    
    LOG_DEBUG("CameraLicenseManager", "License validated for camera: " + camera_id);
    return true;
}

bool CameraLicenseManager::addCameraLicense(const std::string& camera_id, const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    // Check if camera already has a license
    if (camera_licenses_.find(camera_id) != camera_licenses_.end()) {
        LOG_WARN("CameraLicenseManager", "License already exists for camera: " + camera_id);
        return false;
    }
    
    // Check trial limits for the tenant
    int current_camera_count = 0;
    for (const auto& pair : camera_licenses_) {
        if (pair.second.tenant_id == tenant_id && pair.second.is_trial) {
            current_camera_count++;
        }
    }
    
    // Create new camera license
    CameraLicense license;
    license.camera_id = camera_id;
    license.tenant_id = tenant_id;
    license.start_date = std::chrono::system_clock::now();
    
    // For now, all new licenses start as trial licenses
    // This will be enhanced when billing service integration is added
    if (current_camera_count < TRIAL_CAMERA_LIMIT) {
        license.mode = LicenseMode::FREE_TRIAL;
        license.is_trial = true;
        license.end_date = license.start_date + std::chrono::hours(24 * TRIAL_DURATION_DAYS);
        LOG_INFO("CameraLicenseManager", "Created trial license for camera: " + camera_id + 
                 " (tenant: " + tenant_id + ")");
    } else {
        // Trial limit exceeded - would need base license
        LOG_ERROR("CameraLicenseManager", "Trial limit exceeded for tenant: " + tenant_id + 
                  ". Current cameras: " + std::to_string(current_camera_count));
        return false;
    }
    
    license.last_heartbeat = license.start_date;
    
    camera_licenses_[camera_id] = license;
    
    LOG_INFO("CameraLicenseManager", "Added camera license: " + camera_id + 
             " for tenant: " + tenant_id + 
             " (mode: " + (license.is_trial ? "TRIAL" : "BASE") + ")");
    
    return true;
}

bool CameraLicenseManager::removeCameraLicense(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    auto it = camera_licenses_.find(camera_id);
    if (it == camera_licenses_.end()) {
        LOG_WARN("CameraLicenseManager", "No license found to remove for camera: " + camera_id);
        return false;
    }
    
    std::string tenant_id = it->second.tenant_id;
    camera_licenses_.erase(it);
    
    LOG_INFO("CameraLicenseManager", "Removed camera license: " + camera_id + 
             " for tenant: " + tenant_id);
    
    return true;
}

int CameraLicenseManager::getActiveCameraCount(const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    int count = 0;
    auto now = std::chrono::system_clock::now();
    
    for (const auto& pair : camera_licenses_) {
        const auto& license = pair.second;
        if (license.tenant_id == tenant_id && now <= license.end_date) {
            count++;
        }
    }
    
    return count;
}

bool CameraLicenseManager::isTrialLimitExceeded(const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    int trial_camera_count = 0;
    auto now = std::chrono::system_clock::now();
    
    for (const auto& pair : camera_licenses_) {
        const auto& license = pair.second;
        if (license.tenant_id == tenant_id && 
            license.is_trial && 
            now <= license.end_date) {
            trial_camera_count++;
        }
    }
    
    return trial_camera_count >= TRIAL_CAMERA_LIMIT;
}

std::vector<std::string> CameraLicenseManager::getEnabledGrowthPacks(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    auto it = camera_licenses_.find(camera_id);
    if (it == camera_licenses_.end()) {
        LOG_WARN("CameraLicenseManager", "No license found for camera: " + camera_id);
        return {};
    }
    
    return it->second.enabled_growth_packs;
}

bool CameraLicenseManager::enableGrowthPack(const std::string& tenant_id, const std::string& pack_type) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    // Find all cameras for this tenant and enable the growth pack
    bool any_updated = false;
    
    for (auto& pair : camera_licenses_) {
        auto& license = pair.second;
        if (license.tenant_id == tenant_id) {
            // Check if growth pack is already enabled
            auto& packs = license.enabled_growth_packs;
            if (std::find(packs.begin(), packs.end(), pack_type) == packs.end()) {
                packs.push_back(pack_type);
                any_updated = true;
                LOG_INFO("CameraLicenseManager", "Enabled growth pack '" + pack_type + 
                         "' for camera: " + license.camera_id);
            }
        }
    }
    
    if (any_updated) {
        LOG_INFO("CameraLicenseManager", "Enabled growth pack '" + pack_type + 
                 "' for tenant: " + tenant_id);
    } else {
        LOG_WARN("CameraLicenseManager", "No cameras found or growth pack already enabled for tenant: " + 
                 tenant_id + ", pack: " + pack_type);
    }
    
    return any_updated;
}

void CameraLicenseManager::sendHeartbeat(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    auto it = camera_licenses_.find(camera_id);
    if (it != camera_licenses_.end()) {
        it->second.last_heartbeat = std::chrono::system_clock::now();
        LOG_DEBUG("CameraLicenseManager", "Heartbeat received for camera: " + camera_id);
    } else {
        LOG_WARN("CameraLicenseManager", "Heartbeat received for unlicensed camera: " + camera_id);
    }
}

void CameraLicenseManager::enforceTrialLimits(const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(licenses_mutex_);
    
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> expired_cameras;
    
    // Find expired trial licenses for this tenant
    for (const auto& pair : camera_licenses_) {
        const auto& license = pair.second;
        if (license.tenant_id == tenant_id && 
            license.is_trial && 
            now > license.end_date) {
            expired_cameras.push_back(license.camera_id);
        }
    }
    
    // Log expired cameras (actual enforcement would be handled by the enforcement system)
    for (const auto& camera_id : expired_cameras) {
        LOG_WARN("CameraLicenseManager", "Trial license expired for camera: " + camera_id + 
                 " (tenant: " + tenant_id + ")");
    }
    
    if (!expired_cameras.empty()) {
        LOG_INFO("CameraLicenseManager", "Found " + std::to_string(expired_cameras.size()) + 
                 " expired trial licenses for tenant: " + tenant_id);
    }
}

} // namespace tapi

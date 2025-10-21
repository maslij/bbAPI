# 🔒 License & Growth Pack Enforcement Rules

## 📋 **What's Restricted on Free Trial**

### **Trial License** (2 cameras max, 90 days)
**Allowed**:
- ✅ Basic object detection (person, car, bicycle only)
- ✅ Simple file sink (video recording)
- ✅ Object tracking
- ✅ Basic API access

**NOT Allowed** (requires Base License $60/cam/mo):
- ❌ Line zones / Line crossing analytics
- ❌ Polygon zones / Area analytics
- ❌ Database sink (PostgreSQL storage)
- ❌ Advanced CV models (all 80 COCO classes)
- ❌ More than 2 cameras
- ❌ SMS/Email/Webhook alerts

---

### **Base License** ($60/camera/month, unlimited cameras)
**Allowed**:
- ✅ All 80 COCO object classes
- ✅ Line zones
- ✅ Polygon zones
- ✅ Database sink
- ✅ Unlimited cameras
- ✅ Basic analytics

**NOT Allowed** (requires Growth Packs):
- ❌ **Advanced Analytics** Pack Required:
  - Heatmaps
  - Dwell time analysis
  - Crowd density
  - Historical reporting
  
- ❌ **Active Transport** Pack Required:
  - Pedestrian detection enhancements
  - Cyclist detection
  - E-scooter detection
  - Speed analysis
  
- ❌ **Cloud Storage** Pack Required:
  - Cloud backup
  - Extended retention
  
- ❌ **API Integration** Pack Required:
  - Webhooks
  - SMS alerts
  - Email alerts
  - Unlimited API calls

---

## 🛡️ **Enforcement Points in Code**

### **1. Camera Creation** (License Validation)
**File**: `tAPI/src/camera_manager.cpp`

```cpp
std::shared_ptr<Camera> CameraManager::createCamera(
    const std::string& id, 
    const std::string& name,
    const std::string& tenant_id
) {
    // ENFORCEMENT POINT 1: Validate license
    if (license_validator_) {
        auto result = license_validator_->validateCameraLicense(id, tenant_id);
        
        if (!result.is_valid) {
            throw LicenseException("Camera license invalid: " + result.error_message);
        }
        
        // Check trial expiration
        if (result.license_mode == "trial") {
            auto now = std::chrono::system_clock::now();
            if (now > result.valid_until) {
                throw LicenseException("Trial license expired. Upgrade to Base License.");
            }
        }
    }
    
    // ENFORCEMENT POINT 2: Check camera limit
    if (license_validator_ && !license_validator_->canAddCamera(tenant_id, getCurrentCameraCount(tenant_id))) {
        throw LicenseException("Camera limit reached. Trial: 2 cameras. Upgrade for unlimited.");
    }
    
    // Create camera...
}
```

---

### **2. Processor Creation** (Growth Pack Gating)
**File**: `tAPI/src/component_factory.cpp`

**Current Code** (Line 260-266):
```cpp
// Check license tier restrictions
LicenseTier tier = CameraManager::getInstance().getLicenseManager().getLicenseTier();
if (!isComponentAllowedForLicenseTier(effectiveType, "processor", tier)) {
    LOG_ERROR("ComponentFactory", "Processor component '" + effectiveType + 
             "' is not allowed for current license tier");
    return nullptr;
}
```

**Need to Add** (after line 266):
```cpp
// NEW: Check growth pack entitlements
if (entitlement_manager_) {
    std::string tenant_id = camera->getTenantId();  // Need to add this method
    
    // Line zones require Base License OR Advanced Analytics pack
    if (effectiveType == "line_zone_manager") {
        auto license_result = license_validator_->validateCameraLicense(camera->getId(), tenant_id);
        
        if (license_result.license_mode == "trial") {
            throw LicenseException("Line zones not available on Trial. Requires Base License ($60/cam/mo).");
        }
        
        // If base license, also check for Advanced Analytics pack for full features
        if (!entitlement_manager_->hasGrowthPack(tenant_id, "Advanced Analytics")) {
            LOG_WARN("ComponentFactory", "Advanced line analytics require 'Advanced Analytics' pack");
        }
    }
    
    // Polygon zones require Base License
    if (effectiveType == "polygon_zone_manager") {
        auto license_result = license_validator_->validateCameraLicense(camera->getId(), tenant_id);
        
        if (license_result.license_mode == "trial") {
            throw LicenseException("Polygon zones not available on Trial. Requires Base License ($60/cam/mo).");
        }
    }
    
    // Age/Gender detection requires Active Transport pack
    if (effectiveType == "age_gender_detection") {
        if (!entitlement_manager_->hasGrowthPack(tenant_id, "Active Transport")) {
            throw LicenseException("Age/Gender detection requires 'Active Transport' growth pack ($30/mo).");
        }
    }
}
```

---

### **3. Object Class Restrictions** (Trial vs Base)
**File**: `tAPI/src/component_factory.cpp` (in createProcessorComponent, object_detection case)

**Add After Line 296**:
```cpp
// ENFORCEMENT POINT 3: Restrict classes on trial
if (license_validator_ && entitlement_manager_) {
    std::string tenant_id = camera->getTenantId();
    auto license_result = license_validator_->validateCameraLicense(camera->getId(), tenant_id);
    
    if (license_result.license_mode == "trial") {
        // Trial only allows: person, car, bicycle
        std::vector<std::string> trial_classes = {"person", "car", "bicycle"};
        
        // If config specifies classes, filter them
        if (processorConfig.contains("classes") && processorConfig["classes"].is_array()) {
            std::vector<std::string> requested_classes = processorConfig["classes"];
            std::vector<std::string> allowed_classes;
            
            for (const auto& cls : requested_classes) {
                if (std::find(trial_classes.begin(), trial_classes.end(), cls) != trial_classes.end()) {
                    allowed_classes.push_back(cls);
                } else {
                    LOG_WARN("ComponentFactory", "Class '" + cls + "' not available on Trial. Upgrade to Base License for all 80 COCO classes.");
                }
            }
            
            if (allowed_classes.empty()) {
                // Use default trial classes if none were valid
                processorConfig["classes"] = trial_classes;
                LOG_INFO("ComponentFactory", "Trial license: restricting to " + 
                        std::to_string(trial_classes.size()) + " classes");
            } else {
                processorConfig["classes"] = allowed_classes;
            }
        } else {
            // No classes specified, set trial defaults
            processorConfig["classes"] = trial_classes;
            LOG_INFO("ComponentFactory", "Trial license: using default classes (person, car, bicycle)");
        }
    }
}
```

---

### **4. Sink Restrictions** (Database Sink)
**File**: `tAPI/src/component_factory.cpp` (createSinkComponent)

**Add After Line 378**:
```cpp
// ENFORCEMENT POINT 4: Database sink requires Base License
if (effectiveType == "database") {
    if (license_validator_) {
        std::string tenant_id = camera->getTenantId();
        auto license_result = license_validator_->validateCameraLicense(camera->getId(), tenant_id);
        
        if (license_result.license_mode == "trial") {
            LOG_ERROR("ComponentFactory", "Database sink not available on Trial. Requires Base License.");
            throw LicenseException("Database storage requires Base License ($60/cam/mo).");
        }
    }
}
```

---

### **5. Alert Output Restrictions** (SMS/Email/Webhook)
**File**: `tAPI/src/api.cpp` (in alert/notification endpoints)

```cpp
// In POST /api/v1/alerts/sms endpoint
CROW_ROUTE(app_, "/api/v1/alerts/sms").methods("POST"_method)
([this](const crow::request& req) {
    auto body = nlohmann::json::parse(req.body);
    std::string tenant_id = body["tenant_id"];
    
    // ENFORCEMENT: Check API Integration pack
    if (entitlement_manager_ && !entitlement_manager_->isOutputAllowed(tenant_id, "sms")) {
        return crow::response(403, 
            R"({"error": "SMS alerts require 'API Integration' growth pack ($75/mo)"})");
    }
    
    // Send SMS...
    
    // Track usage
    if (usage_tracker_) {
        usage_tracker_->trackSMS(tenant_id, body["camera_id"], 1);
    }
    
    return crow::response(200, "SMS sent");
});

// Similarly for email and webhooks
```

---

## ⏰ **Trial Time Remaining**

### **Add to License Status API**
**File**: `tAPI/src/api.cpp`

**Modify GET `/api/v1/license/cameras` endpoint** (around line 1100):

```cpp
CROW_ROUTE(app_, "/api/v1/license/cameras")
    .methods("GET"_method)
([this](const crow::request& req) {
    auto query_params = req.url_params;
    std::string tenant_id = query_params.get("tenant_id") ? 
                           query_params.get("tenant_id") : "default";
    
    nlohmann::json response;
    
    if (license_validator_) {
        // Get all cameras for tenant
        auto licenses = license_validator_->getTenantLicenses(tenant_id);
        
        int trial_count = 0;
        nlohmann::json cameras_array = nlohmann::json::array();
        
        for (const auto& license : licenses) {
            nlohmann::json cam_info;
            cam_info["camera_id"] = license.camera_id;
            cam_info["tenant_id"] = license.tenant_id;
            cam_info["mode"] = license.license_mode;
            cam_info["is_trial"] = (license.license_mode == "trial");
            
            // Calculate time remaining for trial
            if (license.license_mode == "trial") {
                auto now = std::chrono::system_clock::now();
                auto valid_until = std::chrono::system_clock::from_time_t(license.valid_until);
                auto remaining = std::chrono::duration_cast<std::chrono::hours>(valid_until - now);
                
                int days_remaining = remaining.count() / 24;
                int hours_remaining = remaining.count() % 24;
                
                cam_info["days_remaining"] = days_remaining;
                cam_info["hours_remaining"] = hours_remaining;
                cam_info["trial_expires_at"] = license.valid_until;  // Unix timestamp
                cam_info["is_expired"] = (days_remaining < 0);
                
                trial_count++;
            }
            
            cam_info["start_date"] = license.created_at;
            cam_info["end_date"] = license.valid_until;
            cam_info["enabled_growth_packs"] = license.enabled_growth_packs;
            
            cameras_array.push_back(cam_info);
        }
        
        response["camera_count"] = licenses.size();
        response["trial_limit"] = 2;
        response["trial_cameras"] = trial_count;
        response["is_trial_limit_exceeded"] = (trial_count > 2);
        response["cameras"] = cameras_array;
    }
    
    return createJsonResponse(response);
});
```

---

### **Add Trial Timer to Frontend**
**File**: `tWeb/src/pages/LicenseSetup.tsx`

**Add to the license status card** (around line 100):

```typescript
{isTrialMode && daysRemaining !== null && (
  <Alert severity={daysRemaining < 7 ? "warning" : "info"} sx={{ mt: 2 }}>
    <Typography variant="body2">
      <strong>Trial Period:</strong> {daysRemaining} days remaining
      {daysRemaining < 7 && " - Upgrade soon to avoid service interruption!"}
    </Typography>
    <Box sx={{ mt: 1 }}>
      <LinearProgress 
        variant="determinate" 
        value={(daysRemaining / 90) * 100}
        color={daysRemaining < 7 ? "warning" : "primary"}
      />
    </Box>
  </Alert>
)}
```

---

## 🎯 **Summary of Restrictions**

| Feature | Trial | Base ($60/cam/mo) | Growth Pack Required |
|---------|-------|-------------------|---------------------|
| **Cameras** | 2 max | Unlimited | - |
| **Duration** | 90 days | Unlimited | - |
| **Object Classes** | 3 (person, car, bicycle) | All 80 COCO classes | - |
| **Line Zones** | ❌ | ✅ | - |
| **Polygon Zones** | ❌ | ✅ | - |
| **Database Sink** | ❌ | ✅ | - |
| **File Sink** | ✅ | ✅ | - |
| **Heatmaps** | ❌ | ❌ | Advanced Analytics |
| **Dwell Time** | ❌ | ❌ | Advanced Analytics |
| **Pedestrian Detection** | ❌ | ❌ | Active Transport |
| **SMS Alerts** | ❌ | ❌ | API Integration |
| **Webhooks** | ❌ | ❌ | API Integration |
| **Cloud Backup** | ❌ | ❌ | Cloud Storage |

---

## 📝 **Integration Checklist**

- [ ] Add `tenant_id` field to Camera class
- [ ] Add `getTenantId()` method to Camera class
- [ ] Pass `license_validator_` to ComponentFactory
- [ ] Pass `entitlement_manager_` to ComponentFactory
- [ ] Add enforcement checks in `createProcessorComponent()`
- [ ] Add enforcement checks in `createSinkComponent()`
- [ ] Add class restriction logic for object detection
- [ ] Update license API to include time remaining
- [ ] Add trial timer to frontend UI
- [ ] Test: Try creating 3rd camera on trial (should fail)
- [ ] Test: Try line zone on trial (should fail)
- [ ] Test: Try database sink on trial (should fail)
- [ ] Test: Verify only 3 classes on trial
- [ ] Test: Trial timer displays correctly

---

## 🚀 **Quick Implementation**

I'll create the enforcement wrapper class next that you can easily integrate!


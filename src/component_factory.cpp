#include "component_factory.h"
#include <iostream>
#include "logger.h"
#include "components/source/gstreamer_source.h"
#include "components/processor/object_detector_processor.h"
#include "components/processor/object_tracker_processor.h"
#include "components/processor/line_zone_manager.h"
#include "components/processor/object_classification_processor.h"
#include "components/processor/age_gender_detection_processor.h"
#include "components/processor/polygon_zone_manager.h"
#include "components/sink/file_sink.h"
#include "components/sink/database_sink.h"
#include "camera_manager.h"
#include "license.h"
#include "config_manager.h"
#include "global_config.h"

// Billing enforcement headers
#include "billing/license_validator.h"
#include "billing/entitlement_manager.h"

namespace tapi {

// Mock component implementations for scaffolding
// These would be replaced with actual component implementations

// Processor component mock implementation
class MockProcessorComponent : public ProcessorComponent {
public:
    MockProcessorComponent(const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
        : ProcessorComponent(id, camera), type_(type) {
        config_ = config;
    }

    bool initialize() override {
        std::cout << "Initializing mock processor component: " << getId() << " of type: " << type_ << std::endl;
        return true;
    }

    bool start() override {
        std::cout << "Starting mock processor component: " << getId() << std::endl;
        running_ = true;
        return true;
    }

    bool stop() override {
        std::cout << "Stopping mock processor component: " << getId() << std::endl;
        running_ = false;
        return true;
    }

    bool updateConfig(const nlohmann::json& config) override {
        config_ = config;
        return true;
    }

    nlohmann::json getConfig() const override {
        return config_;
    }

    nlohmann::json getStatus() const override {
        auto status = Component::getStatus();
        status["type"] = type_;
        return status;
    }

private:
    std::string type_;
};

// Sink component mock implementation
class MockSinkComponent : public SinkComponent {
public:
    MockSinkComponent(const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
        : SinkComponent(id, camera), type_(type) {
        config_ = config;
    }

    bool initialize() override {
        std::cout << "Initializing mock sink component: " << getId() << " of type: " << type_ << std::endl;
        return true;
    }

    bool start() override {
        std::cout << "Starting mock sink component: " << getId() << std::endl;
        running_ = true;
        return true;
    }

    bool stop() override {
        std::cout << "Stopping mock sink component: " << getId() << std::endl;
        running_ = false;
        return true;
    }

    bool updateConfig(const nlohmann::json& config) override {
        config_ = config;
        return true;
    }

    nlohmann::json getConfig() const override {
        return config_;
    }

    nlohmann::json getStatus() const override {
        auto status = Component::getStatus();
        status["type"] = type_;
        return status;
    }

private:
    std::string type_;
};

// Initialize static instance
ComponentFactory* ComponentFactory::instance_ = nullptr;

ComponentFactory& ComponentFactory::getInstance() {
    if (!instance_) {
        instance_ = new ComponentFactory();
    }
    return *instance_;
}

ComponentFactory::ComponentFactory() {
    registerComponentTypes();
}

ComponentFactory::~ComponentFactory() {
}

void ComponentFactory::registerComponentTypes() {
    // Register source types
    sourceTypes_ = {
        sourceTypeToString(SourceType::RTSP),
        sourceTypeToString(SourceType::FILE),
        // sourceTypeToString(SourceType::USB),
        // sourceTypeToString(SourceType::HTTP)
    };

    // Register processor types
    processorTypes_ = {
        processorTypeToString(ProcessorType::OBJECT_DETECTION),
        processorTypeToString(ProcessorType::OBJECT_TRACKING),
        processorTypeToString(ProcessorType::LINE_ZONE_MANAGER),
        // processorTypeToString(ProcessorType::OBJECT_CLASSIFICATION),
        // processorTypeToString(ProcessorType::AGE_GENDER_DETECTION),
        processorTypeToString(ProcessorType::POLYGON_ZONE_MANAGER),
        // processorTypeToString(ProcessorType::FACE_RECOGNITION),
        // processorTypeToString(ProcessorType::MOTION_DETECTION),
    };

    // Register sink types
    sinkTypes_ = {
        // sinkTypeToString(SinkType::RTMP),
        sinkTypeToString(SinkType::FILE),
        sinkTypeToString(SinkType::DATABASE),
        // sinkTypeToString(SinkType::WEBSOCKET),
        // sinkTypeToString(SinkType::MQTT)
    };
}

// Helper function to check if component is allowed for the current license tier
bool isComponentAllowedForLicenseTier(
    const std::string& componentType, 
    const std::string& componentCategory,
    LicenseTier tier) {
    
    try {
        // Convert to enums and use the ComponentPermissionHelper
        ComponentCategory category = stringToComponentCategory(componentCategory);
        return ComponentPermissionHelper::getInstance().isComponentAllowed(category, componentType, tier);
    } catch (const std::invalid_argument& e) {
        LOG_ERROR("ComponentFactory", e.what());
        return false;
    }
}

std::shared_ptr<SourceComponent> ComponentFactory::createSourceComponent(
    const std::string& id,
    Camera* camera,
    const std::string& type,
    const nlohmann::json& config) {
    
    // Check if source type is supported
    if (type.empty()) {
        LOG_ERROR("ComponentFactory", "Empty source type received");
        return nullptr;
    }
    
    // Map numeric types to string types (for backward compatibility)
    std::string effectiveType = type;
    if (type == "0" || type == "1") {
        LOG_WARN("ComponentFactory", "Numeric source type '" + type + "' received, converting to rtsp");
        effectiveType = "rtsp";
    } else if (type == "2") {
        LOG_WARN("ComponentFactory", "Numeric source type '" + type + "' received, converting to file");
        effectiveType = "file";
    }
    
    bool typeSupported = false;
    for (const auto& supportedType : sourceTypes_) {
        if (supportedType == effectiveType) {
            typeSupported = true;
            break;
        }
    }
    
    if (!typeSupported) {
        LOG_ERROR("ComponentFactory", "Unsupported source type: '" + effectiveType + "'");
        return nullptr;
    }
    
    // Check license tier restrictions
    LicenseTier tier = CameraManager::getInstance().getLicenseManager().getLicenseTier();
    if (!isComponentAllowedForLicenseTier(effectiveType, "source", tier)) {
        LOG_ERROR("ComponentFactory", "Source component '" + effectiveType + 
                 "' is not allowed for current license tier: " + std::to_string(static_cast<int>(tier)));
        return nullptr;
    }
    
    // Create a GStreamerSource component
    return std::make_shared<GStreamerSource>(id, camera, effectiveType, config);
}

std::shared_ptr<ProcessorComponent> ComponentFactory::createProcessorComponent(
    const std::string& id,
    Camera* camera,
    const std::string& type,
    const nlohmann::json& config) {
    
    // Check if processor type is supported
    if (type.empty()) {
        LOG_ERROR("ComponentFactory", "Empty processor type received");
        return nullptr;
    }
    
    // Map numeric types to string types (for backward compatibility)
    std::string effectiveType = type;
    if (type == "0") {
        LOG_WARN("ComponentFactory", "Numeric processor type '" + type + "' received, converting to object_detection");
        effectiveType = "object_detection";
    } else if (type == "1") {
        LOG_WARN("ComponentFactory", "Numeric processor type '" + type + "' received, converting to object_tracking");
        effectiveType = "object_tracking";
    } else if (type == "2") {
        LOG_WARN("ComponentFactory", "Numeric processor type '" + type + "' received, converting to line_zone_manager");
        effectiveType = "line_zone_manager";
    }
    
    bool typeSupported = false;
    for (const auto& supportedType : processorTypes_) {
        if (supportedType == effectiveType) {
            typeSupported = true;
            break;
        }
    }
    
    if (!typeSupported) {
        LOG_ERROR("ComponentFactory", "Unsupported processor type: '" + effectiveType + "'");
        return nullptr;
    }
    
    // Check license tier restrictions
    LicenseTier tier = CameraManager::getInstance().getLicenseManager().getLicenseTier();
    if (!isComponentAllowedForLicenseTier(effectiveType, "processor", tier)) {
        LOG_ERROR("ComponentFactory", "Processor component '" + effectiveType + 
                 "' is not allowed for current license tier: " + std::to_string(static_cast<int>(tier)));
        return nullptr;
    }
    
    // ============================================================================
    // BILLING ENFORCEMENT: Check license and growth pack requirements
    // ============================================================================
    if (license_validator_ && entitlement_manager_) {
        // Note: For now, using "default" tenant. TODO: Get actual tenant_id from camera/context
        std::string tenant_id = "default";
        std::string camera_id = camera ? camera->getId() : "unknown";
        
        try {
            // Validate camera license first
            auto license_result = license_validator_->validateCameraLicense(camera_id, tenant_id);
            
            if (!license_result.is_valid) {
                LOG_ERROR("ComponentFactory", "Camera license invalid for " + camera_id);
                throw std::runtime_error("Camera license invalid or expired. Please upgrade your license.");
            }
            
            // Check processor-specific restrictions based on license mode
            if (license_result.license_mode == "trial") {
                // TRIAL RESTRICTIONS
                
                // Line zones require Base License
                if (effectiveType == "line_zone_manager") {
                    LOG_ERROR("ComponentFactory", "Line zones not available on Trial license");
                    throw std::runtime_error(
                        "Line zones require Base License ($60/camera/month). "
                        "Upgrade to unlock unlimited cameras and advanced features."
                    );
                }
                
                // Polygon zones require Base License
                if (effectiveType == "polygon_zone_manager") {
                    LOG_ERROR("ComponentFactory", "Polygon zones not available on Trial license");
                    throw std::runtime_error(
                        "Polygon zones require Base License ($60/camera/month)."
                    );
                }
                
                LOG_INFO("ComponentFactory", "Trial license: " + effectiveType + " processor allowed");
            }
            
            // Check growth pack requirements (applies to both trial and base)
            if (effectiveType == "age_gender_detection") {
                if (!entitlement_manager_->hasGrowthPack(tenant_id, "Active Transport")) {
                    LOG_ERROR("ComponentFactory", "Age/Gender detection requires Active Transport pack");
                    throw std::runtime_error(
                        "Age/Gender detection requires 'Active Transport' growth pack ($30/month)."
                    );
                }
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("ComponentFactory", "Billing enforcement failed: " + std::string(e.what()));
            throw;  // Re-throw to prevent component creation
        }
    }
    // ============================================================================
    
    // Create the specific type of processor
    if (effectiveType == "object_detection") {
        // Make a mutable copy of the config so we can set the server URL
        nlohmann::json processorConfig = config;
        
        // Always set server URL from GlobalConfig to ensure consistency
        std::string serverUrl = GlobalConfig::getInstance().getAiServerUrl();
        processorConfig["server_url"] = serverUrl;
        LOG_INFO("ComponentFactory", "Setting server_url for object detection processor from GlobalConfig: " + serverUrl);
        
        // Always update the shared memory setting from GlobalConfig - this takes priority
        bool useSharedMemory = GlobalConfig::getInstance().getUseSharedMemory();
        processorConfig["use_shared_memory"] = useSharedMemory;
        LOG_INFO("ComponentFactory", "Setting use_shared_memory for object detection processor: " + 
                std::string(useSharedMemory ? "true" : "false"));

        // Log the incoming configuration for debugging
        LOG_DEBUG("ComponentFactory", "Creating object_detection processor with config: " + config.dump());
        
        // Only auto-set model_id if it's truly missing or empty AND we don't have a valid saved config
        // Check if this looks like a saved configuration (has multiple fields populated)
        bool isValidSavedConfig = config.contains("model_id") && 
                                 config.contains("classes") && 
                                 config.contains("confidence_threshold");
        
        // Check if model_id is set in config; if not, try to find available models
        // or use a fallback to avoid issues with hardcoded models like yolov4-tiny
        if (!isValidSavedConfig && 
            (!processorConfig.contains("model_id") || processorConfig["model_id"].is_null() ||
            (processorConfig["model_id"].is_string() && processorConfig["model_id"].get<std::string>().empty()))) {
            
            // Create a temporary copy of the config to avoid modifying the original
            nlohmann::json tempConfig = processorConfig;
            
            // Explicitly disable shared memory for temporary processors
            tempConfig["use_shared_memory"] = false;
            
            // Create a temporary processor to query available models
            auto tempProcessor = std::make_shared<ObjectDetectorProcessor>("temp_id", nullptr, effectiveType, tempConfig);
            auto availableModels = tempProcessor->getAvailableModels();
            
            if (!availableModels.empty()) {
                // If we have available models, use the first one
                processorConfig["model_id"] = availableModels[0];
                LOG_INFO("ComponentFactory", "Using first available model: " + availableModels[0]);
            } else {
                // If no models are available, use a placeholder to avoid hardcoding yolov4-tiny
                processorConfig["model_id"] = "yolov7_tiny_onnx";
                LOG_WARN("ComponentFactory", "No models available from server, using placeholder model_id: yolov7_tiny_onnx");
            }
        } else {
            LOG_INFO("ComponentFactory", "Using saved configuration for object_detection processor");
        }
        
        return std::make_shared<ObjectDetectorProcessor>(id, camera, effectiveType, processorConfig);
    } else if (effectiveType == "object_tracking") {
        return std::make_shared<ObjectTrackerProcessor>(id, camera, effectiveType, config);
    } else if (effectiveType == "line_zone_manager") {
        return std::make_shared<LineZoneManager>(id, camera, effectiveType, config);
    } else if (effectiveType == "object_classification") {
        return std::make_shared<ObjectClassificationProcessor>(id, camera, effectiveType, config);
    } else if (effectiveType == "age_gender_detection") {
        return std::make_shared<AgeGenderDetectionProcessor>(id, camera, effectiveType, config);
    } else if (effectiveType == "polygon_zone_manager") {
        return std::make_shared<PolygonZoneManager>(id, camera, effectiveType, config);
    } else {
        // For other types, use the mock implementation for now
        return std::make_shared<MockProcessorComponent>(id, camera, effectiveType, config);
    }
}

std::shared_ptr<SinkComponent> ComponentFactory::createSinkComponent(
    const std::string& id,
    Camera* camera,
    const std::string& type,
    const nlohmann::json& config) {
    
    // Check if sink type is supported
    if (type.empty()) {
        LOG_ERROR("ComponentFactory", "Empty sink type received");
        return nullptr;
    }
    
    // Map numeric types to string types (for backward compatibility)
    std::string effectiveType = type;
    if (type == "0") {
        LOG_WARN("ComponentFactory", "Numeric sink type '" + type + "' received, converting to file");
        effectiveType = "file";
    } else if (type == "1") {
        LOG_WARN("ComponentFactory", "Numeric sink type '" + type + "' received, converting to database");
        effectiveType = "database";
    }
    
    bool typeSupported = false;
    for (const auto& supportedType : sinkTypes_) {
        if (supportedType == effectiveType) {
            typeSupported = true;
            break;
        }
    }
    
    if (!typeSupported) {
        LOG_ERROR("ComponentFactory", "Unsupported sink type: '" + effectiveType + "'");
        return nullptr;
    }
    
    // Check license tier restrictions
    LicenseTier tier = CameraManager::getInstance().getLicenseManager().getLicenseTier();
    if (!isComponentAllowedForLicenseTier(effectiveType, "sink", tier)) {
        LOG_ERROR("ComponentFactory", "Sink component '" + effectiveType + 
                 "' is not allowed for current license tier: " + std::to_string(static_cast<int>(tier)));
        return nullptr;
    }
    
    // ============================================================================
    // BILLING ENFORCEMENT: Check sink restrictions
    // ============================================================================
    if (license_validator_ && effectiveType == "database") {
        // Note: For now, using "default" tenant. TODO: Get actual tenant_id from camera/context
        std::string tenant_id = "default";
        std::string camera_id = camera ? camera->getId() : "unknown";
        
        try {
            // Validate camera license
            auto license_result = license_validator_->validateCameraLicense(camera_id, tenant_id);
            
            if (!license_result.is_valid) {
                LOG_ERROR("ComponentFactory", "Camera license invalid for " + camera_id);
                throw std::runtime_error("Camera license invalid or expired.");
            }
            
            // Database sink requires Base License (not available on trial)
            if (license_result.license_mode == "trial") {
                LOG_ERROR("ComponentFactory", "Database sink not available on Trial license");
                throw std::runtime_error(
                    "Database storage requires Base License ($60/camera/month). "
                    "Trial users can use file sink for local video recording."
                );
            }
            
            LOG_INFO("ComponentFactory", "Database sink allowed for " + license_result.license_mode + " license");
            
        } catch (const std::exception& e) {
            LOG_ERROR("ComponentFactory", "Billing enforcement failed: " + std::string(e.what()));
            throw;  // Re-throw to prevent component creation
        }
    }
    // ============================================================================
    
    // Create the specific type of sink
    if (effectiveType == "file") {
        return std::make_shared<FileSink>(id, camera, effectiveType, config);
    } else if (effectiveType == "database") {
        return std::make_shared<DatabaseSink>(id, camera, effectiveType, config);
    } else {
        // For other types, use the mock implementation for now
        return std::make_shared<MockSinkComponent>(id, camera, effectiveType, config);
    }
}

std::vector<std::string> ComponentFactory::getAvailableSourceTypes() const {
    return sourceTypes_;
}

std::vector<std::string> ComponentFactory::getAvailableProcessorTypes() const {
    return processorTypes_;
}

std::vector<std::string> ComponentFactory::getAvailableSinkTypes() const {
    return sinkTypes_;
}

void ComponentFactory::setBillingManagers(
    std::shared_ptr<brinkbyte::billing::LicenseValidator> license_validator,
    std::shared_ptr<brinkbyte::billing::EntitlementManager> entitlement_manager
) {
    license_validator_ = license_validator;
    entitlement_manager_ = entitlement_manager;
    
    if (license_validator_ && entitlement_manager_) {
        LOG_INFO("ComponentFactory", "Billing enforcement enabled - license and growth pack restrictions will be enforced");
    } else {
        LOG_WARN("ComponentFactory", "Billing enforcement not available - all features allowed");
    }
}

} // namespace tapi 
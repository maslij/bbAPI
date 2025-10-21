#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include "component.h"
#include "camera.h"
#include <nlohmann/json.hpp>

// Forward declarations for billing components
namespace brinkbyte {
namespace billing {
    class LicenseValidator;
    class EntitlementManager;
}
}

namespace tapi {

/**
 * @brief Factory for creating different component types
 */
class ComponentFactory {
public:
    /**
     * @brief Get the singleton instance of ComponentFactory
     * 
     * @return ComponentFactory& The singleton instance
     */
    static ComponentFactory& getInstance();

    /**
     * @brief Create a source component
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Source type (e.g., "rtsp", "file", "usb")
     * @param config Component configuration
     * @return std::shared_ptr<SourceComponent> The created source component or nullptr if failed
     */
    std::shared_ptr<SourceComponent> createSourceComponent(
        const std::string& id,
        Camera* camera,
        const std::string& type,
        const nlohmann::json& config = nlohmann::json());

    /**
     * @brief Create a processor component
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Processor type (e.g., "object_detection", "face_recognition")
     * @param config Component configuration
     * @return std::shared_ptr<ProcessorComponent> The created processor component or nullptr if failed
     */
    std::shared_ptr<ProcessorComponent> createProcessorComponent(
        const std::string& id,
        Camera* camera,
        const std::string& type,
        const nlohmann::json& config = nlohmann::json());

    /**
     * @brief Create a sink component
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Sink type (e.g., "rtmp", "file", "websocket")
     * @param config Component configuration
     * @return std::shared_ptr<SinkComponent> The created sink component or nullptr if failed
     */
    std::shared_ptr<SinkComponent> createSinkComponent(
        const std::string& id,
        Camera* camera,
        const std::string& type,
        const nlohmann::json& config = nlohmann::json());

    /**
     * @brief Get available source types
     * 
     * @return std::vector<std::string> List of available source types
     */
    std::vector<std::string> getAvailableSourceTypes() const;

    /**
     * @brief Get available processor types
     * 
     * @return std::vector<std::string> List of available processor types
     */
    std::vector<std::string> getAvailableProcessorTypes() const;

    /**
     * @brief Get available sink types
     * 
     * @return std::vector<std::string> List of available sink types
     */
    std::vector<std::string> getAvailableSinkTypes() const;

    /**
     * @brief Set billing managers for license and entitlement enforcement
     * 
     * @param license_validator License validator instance
     * @param entitlement_manager Entitlement manager instance
     */
    void setBillingManagers(
        std::shared_ptr<brinkbyte::billing::LicenseValidator> license_validator,
        std::shared_ptr<brinkbyte::billing::EntitlementManager> entitlement_manager
    );

private:
    /**
     * @brief Private constructor for singleton pattern
     */
    ComponentFactory();

    /**
     * @brief Private destructor for singleton pattern
     */
    ~ComponentFactory();

    /**
     * @brief Register available component types
     */
    void registerComponentTypes();

private:
    static ComponentFactory* instance_; ///< Singleton instance

    // Available component types
    std::vector<std::string> sourceTypes_;
    std::vector<std::string> processorTypes_;
    std::vector<std::string> sinkTypes_;
    
    // Billing enforcement (optional - graceful degradation if not set)
    std::shared_ptr<brinkbyte::billing::LicenseValidator> license_validator_;
    std::shared_ptr<brinkbyte::billing::EntitlementManager> entitlement_manager_;
};

} // namespace tapi 
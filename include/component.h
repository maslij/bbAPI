#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace tapi {

// Forward declarations
class Camera;

/**
 * @brief Base component type
 */
enum class ComponentType {
    SOURCE,
    PROCESSOR,
    SINK
};

/**
 * @brief Base class for all pipeline components
 */
class Component {
public:
    /**
     * @brief Construct a new Component object
     * 
     * @param id Unique identifier for the component
     * @param type Component type (source, processor, sink)
     * @param camera Parent camera object
     */
    Component(const std::string& id, ComponentType type, Camera* camera);
    
    /**
     * @brief Destructor
     */
    virtual ~Component();
    
    /**
     * @brief Get the component ID
     * 
     * @return std::string The component ID
     */
    std::string getId() const;
    
    /**
     * @brief Get the component type
     * 
     * @return ComponentType The component type
     */
    ComponentType getType() const;
    
    /**
     * @brief Get the parent camera
     * 
     * @return Camera* Pointer to parent camera
     */
    Camera* getCamera() const;
    
    /**
     * @brief Initialize the component
     * 
     * @return true if initialization succeeded, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Start the component
     * 
     * @return true if start succeeded, false otherwise
     */
    virtual bool start() = 0;
    
    /**
     * @brief Stop the component
     * 
     * @return true if stop succeeded, false otherwise
     */
    virtual bool stop() = 0;
    
    /**
     * @brief Check if component is running
     * 
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Update component configuration
     * 
     * @param config JSON configuration
     * @return true if update succeeded, false otherwise
     */
    virtual bool updateConfig(const nlohmann::json& config) = 0;
    
    /**
     * @brief Get component configuration
     * 
     * @return nlohmann::json Current configuration
     */
    virtual nlohmann::json getConfig() const = 0;
    
    /**
     * @brief Get component status
     * 
     * @return nlohmann::json Component status
     */
    virtual nlohmann::json getStatus() const;

protected:
    std::string id_;               ///< Component ID
    ComponentType type_;           ///< Component type
    Camera* camera_;               ///< Parent camera
    bool running_;                 ///< Whether component is running
    nlohmann::json config_;        ///< Component configuration
};

/**
 * @brief Source component (camera input)
 */
class SourceComponent : public Component {
public:
    /**
     * @brief Construct a new Source Component
     * 
     * @param id Component ID
     * @param camera Parent camera
     */
    SourceComponent(const std::string& id, Camera* camera);
    
    /**
     * @brief Destructor
     */
    ~SourceComponent() override;

    /**
     * @brief Initialize the source component
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override { return true; }
    
    /**
     * @brief Start the source component
     * 
     * @return true if start succeeded, false otherwise
     */
    bool start() override { running_ = true; return true; }
    
    /**
     * @brief Stop the source component
     * 
     * @return true if stop succeeded, false otherwise
     */
    bool stop() override { running_ = false; return true; }
    
    /**
     * @brief Update component configuration
     * 
     * @param config JSON configuration
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override { config_ = config; return true; }
    
    /**
     * @brief Get component configuration
     * 
     * @return nlohmann::json Current configuration
     */
    nlohmann::json getConfig() const override { return config_; }
};

/**
 * @brief Processor component (analytics)
 */
class ProcessorComponent : public Component {
public:
    /**
     * @brief Construct a new Processor Component
     * 
     * @param id Component ID
     * @param camera Parent camera
     */
    ProcessorComponent(const std::string& id, Camera* camera);
    
    /**
     * @brief Destructor
     */
    ~ProcessorComponent() override;

    /**
     * @brief Initialize the processor component
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override { return true; }
    
    /**
     * @brief Start the processor component
     * 
     * @return true if start succeeded, false otherwise
     */
    bool start() override { running_ = true; return true; }
    
    /**
     * @brief Stop the processor component
     * 
     * @return true if stop succeeded, false otherwise
     */
    bool stop() override { running_ = false; return true; }
    
    /**
     * @brief Update component configuration
     * 
     * @param config JSON configuration
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override { config_ = config; return true; }
    
    /**
     * @brief Get component configuration
     * 
     * @return nlohmann::json Current configuration
     */
    nlohmann::json getConfig() const override { return config_; }
};

/**
 * @brief Sink component (output)
 */
class SinkComponent : public Component {
public:
    /**
     * @brief Construct a new Sink Component
     * 
     * @param id Component ID
     * @param camera Parent camera
     */
    SinkComponent(const std::string& id, Camera* camera);
    
    /**
     * @brief Destructor
     */
    ~SinkComponent() override;

    /**
     * @brief Initialize the sink component
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override { return true; }
    
    /**
     * @brief Start the sink component
     * 
     * @return true if start succeeded, false otherwise
     */
    bool start() override { running_ = true; return true; }
    
    /**
     * @brief Stop the sink component
     * 
     * @return true if stop succeeded, false otherwise
     */
    bool stop() override { running_ = false; return true; }
    
    /**
     * @brief Update component configuration
     * 
     * @param config JSON configuration
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override { config_ = config; return true; }
    
    /**
     * @brief Get component configuration
     * 
     * @return nlohmann::json Current configuration
     */
    nlohmann::json getConfig() const override { return config_; }
};

} // namespace tapi 
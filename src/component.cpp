#include "component.h"
#include "camera.h"
#include <iostream>

namespace tapi {

Component::Component(const std::string& id, ComponentType type, Camera* camera)
    : id_(id), type_(type), camera_(camera), running_(false) {
}

Component::~Component() {
    // Don't call stop() directly as it's a pure virtual function
    // Derived classes should handle stopping in their destructors
}

std::string Component::getId() const {
    return id_;
}

ComponentType Component::getType() const {
    return type_;
}

Camera* Component::getCamera() const {
    return camera_;
}

bool Component::isRunning() const {
    return running_;
}

nlohmann::json Component::getStatus() const {
    nlohmann::json status;
    status["id"] = id_;
    
    // Store the component type as a string for persistence
    switch (type_) {
        case ComponentType::SOURCE:
            status["type"] = "source";
            break;
        case ComponentType::PROCESSOR:
            status["type"] = "processor";
            break;
        case ComponentType::SINK:
            status["type"] = "sink";
            break;
        default:
            status["type"] = "unknown";
    }
    
    status["running"] = running_;
    return status;
}

// Source Component Implementation
SourceComponent::SourceComponent(const std::string& id, Camera* camera)
    : Component(id, ComponentType::SOURCE, camera) {
}

SourceComponent::~SourceComponent() {
    // Ensure component is stopped
    if (isRunning()) {
        stop();
    }
}

// Processor Component Implementation
ProcessorComponent::ProcessorComponent(const std::string& id, Camera* camera)
    : Component(id, ComponentType::PROCESSOR, camera) {
}

ProcessorComponent::~ProcessorComponent() {
    // Ensure component is stopped
    if (isRunning()) {
        stop();
    }
}

// Sink Component Implementation
SinkComponent::SinkComponent(const std::string& id, Camera* camera)
    : Component(id, ComponentType::SINK, camera) {
}

SinkComponent::~SinkComponent() {
    // Ensure component is stopped
    if (isRunning()) {
        stop();
    }
}

} // namespace tapi 
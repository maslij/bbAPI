#pragma once

#include "component_instance.h"
#include "data_container.h"
#include "components/event_alarm.h"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <chrono>

// Forward declaration for MQTT client
#ifdef HAVE_MQTT_SUPPORT
namespace mqtt {
    class async_client;
    class connect_options;
}
#endif

namespace tapi {

/**
 * @brief Component that sends telemetry data to an MQTT broker
 * 
 * This component collects detection events, line crossing events,
 * and alarms, and forwards them to an MQTT broker for cloud processing.
 */
class MqttTelemetry : public ComponentInstance {
public:
    /**
     * @brief Construct a new MQTT Telemetry component
     * 
     * @param node Pipeline node
     * @param componentDef Component definition
     */
    MqttTelemetry(const PipelineNode& node, 
                 std::shared_ptr<VisionComponent> componentDef);
    
    /**
     * @brief Destructor
     */
    ~MqttTelemetry() override;
    
    /**
     * @brief Initialize the component
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override;
    
    /**
     * @brief Process inputs to send telemetry events
     * 
     * @param inputs Input data containers
     * @return Empty output map
     */
    std::map<std::string, DataContainer> process(
        const std::map<std::string, DataContainer>& inputs) override;
    
    /**
     * @brief Reset the component state
     */
    void reset() override;
    
    /**
     * @brief Update component configuration
     * 
     * @param newConfig New configuration values
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const std::map<std::string, nlohmann::json>& newConfig) override;
    
    /**
     * @brief Update the stream ID from the parent pipeline
     * 
     * This should be called when the parent processing engine is set
     * or when a component is added to a pipeline.
     */
    void updateStreamIdFromPipeline();
    
private:
    // MQTT client and connection management
#ifdef HAVE_MQTT_SUPPORT
    std::unique_ptr<mqtt::async_client> mqttClient_;
#endif
    bool isConnected_;
    
    // Connect to the MQTT broker
    bool connectToBroker();
    
    // Handle different event types
    void publishDetections(const std::vector<Detection>& detections);
    void publishCrossingEvents(const std::vector<Event>& events);
    void publishAlarms(const std::vector<AlarmEvent>& alarms);
    
    // Publish a single message to a topic
    bool publishMessage(const std::string& topic, const std::string& payload, int qos = 1, bool retain = false);
    
    // Reconnection logic
    void handleConnectionLost();
    void attemptReconnect();
    
    // Connection parameters
    std::string brokerHost_;
    int brokerPort_;
    std::string clientId_;
    std::string username_;
    std::string password_;
    bool useTls_;
    std::string topicPrefix_;
    int qosLevel_;
    bool retainMessages_;
    bool includeImages_;
    
    // Batching parameters
    int batchSize_;
    int sendIntervalSecs_;
    int offlineBufferSize_;
    
    // Message queues for different event types
    std::vector<nlohmann::json> detectionQueue_;
    std::vector<nlohmann::json> eventQueue_;
    std::vector<nlohmann::json> alarmQueue_;
    std::mutex queueMutex_;
    
    // Background thread for periodic telemetry sending
    std::thread telemetryThread_;
    std::atomic<bool> running_;
    std::condition_variable cv_;
    
    // Periodic sending function
    void telemetryWorker();
    
    // Send batched queues
    void sendQueuedMessages();
    
    // Generate structured message for each event type
    nlohmann::json formatDetectionMessage(const Detection& detection);
    nlohmann::json formatEventMessage(const Event& event);
    nlohmann::json formatAlarmMessage(const AlarmEvent& alarm);
    
    // Device/stream identification info
    std::string deviceId_;
    std::string streamId_;
    
    // Topic configurations
    std::string topicDetections_;
    std::string topicCrossings_;
    std::string topicAlarms_;
    std::string topicMetrics_;
    
    // Generate full topic path with prefix
    std::string getFullTopic(const std::string& subtopic) const;
};

} // namespace tapi 
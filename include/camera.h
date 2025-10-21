#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include "component.h"
#include "components/processor/object_detector_processor.h"
#include "components/processor/object_tracker_processor.h"
#include "components/telemetry.h"

namespace tapi {

/**
 * @brief Camera class represents a complete camera pipeline
 */
class Camera {
public:
    /**
     * @brief Construct a new Camera object
     * 
     * @param id Unique identifier for the camera
     * @param name Human-readable name for the camera
     */
    Camera(const std::string& id, const std::string& name = "");

    /**
     * @brief Destructor
     */
    ~Camera();

    /**
     * @brief Get the camera ID
     * 
     * @return std::string The camera ID
     */
    std::string getId() const;

    /**
     * @brief Get the camera name
     * 
     * @return std::string The camera name
     */
    std::string getName() const;

    /**
     * @brief Set the camera name
     * 
     * @param name New camera name
     */
    void setName(const std::string& name);

    /**
     * @brief Add a source component
     * 
     * @param source Source component to add
     * @return true if successfully added, false if already has a source
     */
    bool setSourceComponent(std::shared_ptr<SourceComponent> source);

    /**
     * @brief Get the source component
     * 
     * @return std::shared_ptr<SourceComponent> The source component
     */
    std::shared_ptr<SourceComponent> getSourceComponent() const;

    /**
     * @brief Add a processor component
     * 
     * @param processor Processor component to add
     * @return true if successfully added, false otherwise
     */
    bool addProcessorComponent(std::shared_ptr<ProcessorComponent> processor);

    /**
     * @brief Remove a processor component
     * 
     * @param id ID of processor to remove
     * @return true if successfully removed, false if not found
     */
    bool removeProcessorComponent(const std::string& id);

    /**
     * @brief Get all processor components
     * 
     * @return std::vector<std::shared_ptr<ProcessorComponent>> List of processor components
     */
    std::vector<std::shared_ptr<ProcessorComponent>> getProcessorComponents() const;

    /**
     * @brief Get a processor component by ID
     * 
     * @param id Processor component ID
     * @return std::shared_ptr<ProcessorComponent> The processor component or nullptr if not found
     */
    std::shared_ptr<ProcessorComponent> getProcessorComponent(const std::string& id) const;

    /**
     * @brief Add a sink component
     * 
     * @param sink Sink component to add
     * @return true if successfully added, false otherwise
     */
    bool addSinkComponent(std::shared_ptr<SinkComponent> sink);

    /**
     * @brief Remove a sink component
     * 
     * @param id ID of sink to remove
     * @return true if successfully removed, false if not found
     */
    bool removeSinkComponent(const std::string& id);

    /**
     * @brief Get all sink components
     * 
     * @return std::vector<std::shared_ptr<SinkComponent>> List of sink components
     */
    std::vector<std::shared_ptr<SinkComponent>> getSinkComponents() const;

    /**
     * @brief Get a sink component by ID
     * 
     * @param id Sink component ID
     * @return std::shared_ptr<SinkComponent> The sink component or nullptr if not found
     */
    std::shared_ptr<SinkComponent> getSinkComponent(const std::string& id) const;

    /**
     * @brief Get all components
     * 
     * @return std::vector<std::shared_ptr<Component>> List of all components
     */
    std::vector<std::shared_ptr<Component>> getAllComponents() const;

    /**
     * @brief Get component by ID
     * 
     * @param id Component ID
     * @return std::shared_ptr<Component> The component or nullptr if not found
     */
    std::shared_ptr<Component> getComponent(const std::string& id) const;

    /**
     * @brief Start all components in the camera pipeline
     * 
     * @return true if all components started successfully, false otherwise
     */
    bool start();

    /**
     * @brief Stop all components in the camera pipeline
     * 
     * @return true if all components stopped successfully, false otherwise
     */
    bool stop();

    /**
     * @brief Check if camera is running
     * 
     * @return true if running, false otherwise
     */
    bool isRunning() const;

    /**
     * @brief Get camera status
     * 
     * @param includeComponents Whether to include component status
     * @return nlohmann::json Camera status
     */
    nlohmann::json getStatus(bool includeComponents = true) const;

    /**
     * @brief Process a single frame from the source through the pipeline
     * 
     * Reads a frame from the source component, processes it through all processor
     * components, and sends it to all sink components.
     * 
     * @return true if a frame was successfully processed, false otherwise
     */
    bool processFrame();

    /**
     * @brief Get the latest processed frame
     * 
     * @return cv::Mat Latest processed frame (empty if no frame available)
     */
    cv::Mat getLatestFrame() const;

    /**
     * @brief Get latest frame encoded as JPEG
     * 
     * @param quality JPEG quality (0-100)
     * @return std::vector<uchar> JPEG encoded frame data
     */
    std::vector<uchar> getLatestFrameJpeg(int quality = 90) const;

    /**
     * @brief Get the latest raw (unprocessed) frame
     * 
     * @return cv::Mat Latest raw frame without annotations (empty if no frame available)
     */
    cv::Mat getRawFrame() const;

    /**
     * @brief Get latest raw frame encoded as JPEG
     * 
     * @param quality JPEG quality (0-100)
     * @return std::vector<uchar> JPEG encoded raw frame data
     */
    std::vector<uchar> getRawFrameJpeg(int quality = 90) const;

    /**
     * @brief Get the latest detections from object detector processors
     * 
     * @return std::vector<ObjectDetectorProcessor::Detection> Latest detections
     */
    std::vector<ObjectDetectorProcessor::Detection> getLatestDetections() const;

    /**
     * @brief Get the latest tracked objects from object tracker processors
     * 
     * @return std::vector<ObjectTrackerProcessor::TrackedObject> Latest tracked objects
     */
    std::vector<ObjectTrackerProcessor::TrackedObject> getLatestTrackedObjects() const;

    /**
     * @brief Get the latest telemetry events
     * 
     * @return std::vector<TelemetryEvent> Latest telemetry events
     */
    std::vector<TelemetryEvent> getLatestTelemetryEvents() const;

private:
    /**
     * @brief Background processing thread function
     * 
     * Continuously processes frames as long as the camera is running
     */
    void processingThread();

    /**
     * @brief Helper method to stop components in case of startup failure
     * 
     * Ensures clean shutdown of any components that were started
     */
    void stopComponents();

private:
    std::string id_;                                   ///< Camera ID
    std::string name_;                                 ///< Camera name
    bool running_;                                     ///< Whether camera is running
    
    std::shared_ptr<SourceComponent> source_;          ///< Source component
    std::unordered_map<std::string, 
        std::shared_ptr<ProcessorComponent>> processors_; ///< Processor components
    std::unordered_map<std::string, 
        std::shared_ptr<SinkComponent>> sinks_;        ///< Sink components
    
    cv::Mat rawFrame_;                                 ///< Latest raw frame
    cv::Mat latestFrame_;                              ///< Latest processed frame
    std::vector<ObjectDetectorProcessor::Detection> latestDetections_; ///< Latest object detections
    std::vector<ObjectTrackerProcessor::TrackedObject> latestTrackedObjects_; ///< Latest tracked objects
    std::vector<TelemetryEvent> latestTelemetryEvents_; ///< Latest telemetry events
    mutable std::mutex mutex_;                         ///< Mutex for thread safety
    mutable std::mutex frameMutex_;                    ///< Mutex for frame access

    // Background processing
    std::thread processingThread_;                     ///< Background thread for continuous processing
    std::atomic<bool> stopProcessing_;                 ///< Flag to stop the processing thread
    std::condition_variable processingCV_;             ///< Condition variable for thread synchronization
    std::mutex processingMutex_;                       ///< Mutex for condition variable
};

} // namespace tapi 
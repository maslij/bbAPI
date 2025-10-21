#pragma once

#include "component_instance.h"
#include "data_container.h"
#include <opencv2/opencv.hpp>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace tapi {

/**
 * @brief Structure representing an alarm event with associated object image
 */
struct AlarmEvent {
    std::string message;                ///< Alarm message
    std::string objectId;               ///< Object ID that triggered the alarm
    std::string objectClass;            ///< Object class name
    float confidence;                   ///< Detection confidence
    int64_t timestamp;                  ///< Timestamp when alarm was triggered (milliseconds since epoch)
    cv::Rect boundingBox;               ///< Object bounding box
    cv::Mat objectImage;                ///< Image of the detected object (cropped from frame)
    bool reported;                      ///< Whether this alarm has been reported already
    
    /**
     * @brief Convert to JSON
     */
    nlohmann::json toJson() const;
};

/**
 * @brief Component that triggers alarms based on detected events
 */
class EventAlarm : public ComponentInstance {
public:
    /**
     * @brief Construct a new Event Alarm component
     * 
     * @param node Pipeline node
     * @param componentDef Component definition
     */
    EventAlarm(const PipelineNode& node, 
               std::shared_ptr<VisionComponent> componentDef);
    
    /**
     * @brief Destructor
     */
    ~EventAlarm() override = default;
    
    /**
     * @brief Initialize the component
     */
    bool initialize() override;
    
    /**
     * @brief Process inputs to detect events and trigger alarms
     */
    std::map<std::string, DataContainer> process(
        const std::map<std::string, DataContainer>& inputs) override;
    
    /**
     * @brief Reset the component
     */
    void reset() override;
    
    /**
     * @brief Update component configuration
     * 
     * @param newConfig The new configuration
     * @return true if the configuration was updated successfully
     */
    bool updateConfig(const std::map<std::string, nlohmann::json>& newConfig) override;
    
    /**
     * @brief Get the latest alarm events as text messages
     */
    std::vector<std::string> getLatestAlarmEvents() const;
    
    /**
     * @brief Get the latest alarm events with associated object images
     */
    std::vector<AlarmEvent> getLatestAlarmEventsWithImages() const;
    
    /**
     * @brief Trigger an alarm with just a message
     * @param message The alarm message
     */
    void triggerAlarmWithMessage(const std::string& message);
    
private:
    // Spatial hashing to group nearby detections
    struct SpatialCell {
        std::string id;        ///< Unique ID for this cell
        cv::Rect region;       ///< Average region covered by objects in this cell
        int count;             ///< Number of objects in this cell
        float maxConfidence;   ///< Maximum confidence of any object in this cell
    };
    
    /**
     * @brief Get a spatial hash key for a bounding box
     * @param bbox The bounding box
     * @param frameWidth Width of the current frame
     * @param frameHeight Height of the current frame
     * @return A string key for the spatial hash
     */
    std::string getSpatialHashKey(const cv::Rect& bbox, int frameWidth, int frameHeight);
    
    /**
     * @brief Check if two boxes are overlapping using IoU
     * @param box1 First bounding box
     * @param box2 Second bounding box
     * @param iouThreshold Threshold for IoU to consider boxes overlapping
     * @return True if boxes overlap above threshold
     */
    bool areBoxesOverlapping(const cv::Rect& box1, const cv::Rect& box2, float iouThreshold);
    
    /**
     * @brief Calculate Intersection over Union for two bounding boxes
     * @param box1 First bounding box
     * @param box2 Second bounding box
     * @return IoU value between 0.0 and 1.0
     */
    double calculateIOU(const cv::Rect& box1, const cv::Rect& box2);

    float minConfidence_;               ///< Minimum confidence threshold for triggering alarms
    int triggerDelay_;                  ///< Delay in seconds before triggering an alarm
    int coolDownPeriod_;                ///< Cool down period in seconds after an alarm
    bool notifyOnAlarm_;                ///< Whether to send notifications on alarms
    std::vector<std::string> allowedClasses_; ///< List of classes allowed to trigger alarms (empty = all allowed)
    
    // Spatial filtering parameters
    bool enableSpatialMapping_;            ///< Whether to use spatial mapping (default false)
    float spatialGridSize_;             ///< Grid size as fraction of frame dimension
    float iouThreshold_;                ///< IoU threshold for considering boxes as the same object
    int alarmExpirationSecs_;           ///< How long alarms remain active before expiring (in seconds)
    bool onlyReportOnce_;               ///< Whether to report each alarm only once (default true)
    
    std::vector<AlarmEvent> latestAlarmEvents_;  ///< Latest alarm events with images
    mutable std::mutex eventsMutex_;              ///< Mutex for thread-safe events access
    
    // State tracking containers
    std::unordered_map<std::string, SpatialCell> spatialHashGrid_;  ///< Current frame's spatial hash grid
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> detectionTimes_;  ///< Track when detections first appeared
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastAlarmTimes_;  ///< Track when alarms were last triggered
    
    /**
     * @brief Process detections for potential alarms
     * @param detections The detections to process
     * @param frame The current frame (for extracting object images)
     */
    void processDetections(const std::vector<Detection>& detections, const cv::Mat& frame);
    
    /**
     * @brief Process tracks for potential alarms
     * @param tracks The tracks to process
     * @param frame The current frame (for extracting object images)
     */
    void processTracks(const std::vector<Track>& tracks, const cv::Mat& frame);
    
    /**
     * @brief Process crossing events for potential alarms
     * @param events The events to process
     * @param frame The current frame (for extracting event location images)
     */
    void processEvents(const std::vector<Event>& events, const cv::Mat& frame);
    
    /**
     * @brief Remove alarms that have expired based on alarmExpirationSecs_
     */
    void removeExpiredAlarms();
    
    /**
     * @brief Trigger an alarm for a specific event
     * @param eventType Type of event that triggered the alarm
     * @param objectId ID of the object that triggered the alarm
     * @param objectClass Class name of the object
     * @param confidence Confidence score of the detection
     * @param details Additional details about the alarm
     * @param bbox Bounding box of the object
     * @param frame Current frame (for extracting object image)
     */
    void triggerAlarm(const std::string& eventType, 
                      const std::string& objectId,
                      const std::string& objectClass,
                      float confidence, 
                      const std::string& details,
                      const cv::Rect& bbox,
                      const cv::Mat& frame);
};

} // namespace tapi

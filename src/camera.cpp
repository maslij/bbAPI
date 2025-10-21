#include "camera.h"
#include <iostream>
#include "components/source/gstreamer_source.h"
#include "components/processor/object_detector_processor.h"
#include "components/processor/object_tracker_processor.h"
#include "components/processor/line_zone_manager.h"
#include "components/processor/object_classification_processor.h"
#include "components/processor/age_gender_detection_processor.h"
#include "components/processor/polygon_zone_manager.h"
#include "components/sink/file_sink.h"
#include "components/sink/database_sink.h"
#include "logger.h"
#include "components/telemetry.h"

namespace tapi {

Camera::Camera(const std::string& id, const std::string& name)
    : id_(id), name_(name.empty() ? id : name), running_(false), stopProcessing_(true) {
}

Camera::~Camera() {
    if (running_) {
        stop();
    }
}

std::string Camera::getId() const {
    return id_;
}

std::string Camera::getName() const {
    return name_;
}

void Camera::setName(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    name_ = name;
}

bool Camera::setSourceComponent(std::shared_ptr<SourceComponent> source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (source_) {
        // Already has a source component, need to stop and remove it first
        // Always call stop to ensure proper cleanup
        source_->stop();
    }
    source_ = source;
    return true;
}

std::shared_ptr<SourceComponent> Camera::getSourceComponent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return source_;
}

bool Camera::addProcessorComponent(std::shared_ptr<ProcessorComponent> processor) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string& id = processor->getId();
    if (processors_.find(id) != processors_.end()) {
        return false; // Already exists
    }
    processors_[id] = processor;
    return true;
}

bool Camera::removeProcessorComponent(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = processors_.find(id);
    if (it == processors_.end()) {
        return false; // Not found
    }

    // Always call stop() to ensure proper cleanup of resources like shared memory
    // regardless of the component's or camera's running state
    it->second->stop();
    
    processors_.erase(it);
    return true;
}

std::vector<std::shared_ptr<ProcessorComponent>> Camera::getProcessorComponents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<ProcessorComponent>> processors;
    for (const auto& pair : processors_) {
        processors.push_back(pair.second);
    }
    return processors;
}

std::shared_ptr<ProcessorComponent> Camera::getProcessorComponent(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = processors_.find(id);
    if (it == processors_.end()) {
        return nullptr;
    }
    return it->second;
}

bool Camera::addSinkComponent(std::shared_ptr<SinkComponent> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string& id = sink->getId();
    if (sinks_.find(id) != sinks_.end()) {
        return false; // Already exists
    }
    sinks_[id] = sink;
    return true;
}

bool Camera::removeSinkComponent(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sinks_.find(id);
    if (it == sinks_.end()) {
        return false; // Not found
    }

    // Always call stop() to ensure proper cleanup of resources
    // regardless of running state
    it->second->stop();
    sinks_.erase(it);
    return true;
}

std::vector<std::shared_ptr<SinkComponent>> Camera::getSinkComponents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<SinkComponent>> sinks;
    for (const auto& pair : sinks_) {
        sinks.push_back(pair.second);
    }
    return sinks;
}

std::shared_ptr<SinkComponent> Camera::getSinkComponent(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sinks_.find(id);
    if (it == sinks_.end()) {
        return nullptr;
    }
    return it->second;
}

std::vector<std::shared_ptr<Component>> Camera::getAllComponents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Component>> components;
    
    // Add source if it exists
    if (source_) {
        components.push_back(source_);
    }
    
    // Add all processors
    for (const auto& pair : processors_) {
        components.push_back(pair.second);
    }
    
    // Add all sinks
    for (const auto& pair : sinks_) {
        components.push_back(pair.second);
    }
    
    return components;
}

std::shared_ptr<Component> Camera::getComponent(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check source
    if (source_ && source_->getId() == id) {
        return source_;
    }
    
    // Check processors
    auto procIt = processors_.find(id);
    if (procIt != processors_.end()) {
        return procIt->second;
    }
    
    // Check sinks
    auto sinkIt = sinks_.find(id);
    if (sinkIt != sinks_.end()) {
        return sinkIt->second;
    }
    
    return nullptr; // Not found
}

bool Camera::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (running_) {
        return true; // Already running
    }
    
    // Initialize all processor components
    // Don't immediately fail if AI server is unavailable - the processors have retry logic
    for (auto& pair : processors_) {
        auto& processor = pair.second;
        
        // Initialize the processor but don't fail startup if only server availability fails
        if (!processor->initialize()) {
            std::string errorMsg = "Failed to initialize processor component: " + processor->getId();
            
            // We're more lenient with AI server errors now since components have retry logic
            auto status = processor->getStatus();
            if (status.contains("last_error") && !status["last_error"].is_null()) {
                std::string lastError = status["last_error"].get<std::string>();
                if (lastError.find("server is not available") != std::string::npos || 
                    lastError.find("connect to server") != std::string::npos) {
                    // Log the error but continue with startup - the component will retry connections
                    LOG_WARN("Camera", "AI server currently unavailable for " + processor->getId() + 
                            ", but continuing camera startup. Component will retry connections.");
                    // Don't return false here, allow startup to continue
                } else {
                    // For other initialization errors (not AI server related), still log them
                    LOG_ERROR("Camera", errorMsg);
                }
            } else {
                LOG_ERROR("Camera", errorMsg);
            }
        }
    }
    
    bool success = true;
    
    // Start source first
    if (source_) {
        if (!source_->initialize() || !source_->start()) {
            LOG_ERROR("Camera", "Failed to start source component: " + source_->getId());
            success = false;
        }
    }
    
    // Start processors
    for (auto& pair : processors_) {
        auto& processor = pair.second;
        // Start each processor - if they fail due to server issues, they'll retry later
        if (!processor->start()) {
            auto status = processor->getStatus();
            if (status.contains("last_error") && !status["last_error"].is_null()) {
                std::string lastError = status["last_error"].get<std::string>();
                if (lastError.find("server is not available") != std::string::npos || 
                   lastError.find("connect to server") != std::string::npos) {
                    // If it's an AI server error, log a warning but don't fail the camera startup
                    LOG_WARN("Camera", "Processor " + processor->getId() + " couldn't start due to AI server " +
                           "unavailability. It will retry connecting later.");
                } else {
                    // Other errors should still be treated as failures
                    LOG_ERROR("Camera", "Failed to start processor component: " + processor->getId());
                    success = false;
                }
            } else {
                LOG_ERROR("Camera", "Failed to start processor component: " + processor->getId());
                success = false;
            }
        }
    }
    
    // Start sinks
    for (auto& pair : sinks_) {
        auto& sink = pair.second;
        if (!sink->initialize() || !sink->start()) {
            LOG_ERROR("Camera", "Failed to start sink component: " + sink->getId());
            success = false;
        }
    }
    
    if (success) {
        running_ = true;
        
        // Start the background processing thread
        stopProcessing_ = false;
        processingThread_ = std::thread(&Camera::processingThread, this);
        
        LOG_INFO("Camera", "Started camera " + id_ + " with background processing");
    } else {
        // Cleanup any components that were successfully started
        stopComponents();
    }
    
    return success;
}

// Helper method to stop components in case of startup failure
void Camera::stopComponents() {
    // Stop sinks first - always call stop() to ensure cleanup
    for (auto& pair : sinks_) {
        auto& sink = pair.second;
        if (sink->isRunning()) {
            sink->stop();
        }
    }
    
    // Stop processors - always call stop() to ensure shared memory cleanup
    for (auto& pair : processors_) {
        auto& processor = pair.second;
        if (processor->isRunning()) {
            processor->stop();
        }
    }
    
    // Stop source last
    if (source_ && source_->isRunning()) {
        source_->stop();
    }
}

bool Camera::stop() {
    // First stop the processing thread
    {
        std::unique_lock<std::mutex> lock(processingMutex_);
        stopProcessing_ = true;
        processingCV_.notify_one();
    }
    
    // Join the thread if it's running
    if (processingThread_.joinable()) {
        processingThread_.join();
        LOG_INFO("Camera", "Background processing thread for camera " + id_ + " stopped");
    }
    
    // Now stop the components
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!running_) {
        return true; // Already stopped
    }
    
    bool success = true;
    
    // Stop sinks first - always call stop() to ensure cleanup
    for (auto& pair : sinks_) {
        auto& sink = pair.second;
        if (!sink->stop()) {
            std::cerr << "Failed to stop sink component: " << sink->getId() << std::endl;
            success = false;
        }
    }
    
    // Stop processors - always call stop() to ensure shared memory cleanup
    for (auto& pair : processors_) {
        auto& processor = pair.second;
        if (!processor->stop()) {
            std::cerr << "Failed to stop processor component: " << processor->getId() << std::endl;
            success = false;
        }
    }
    
    // Stop source last - always call stop() for consistency
    if (source_ && !source_->stop()) {
        std::cerr << "Failed to stop source component: " << source_->getId() << std::endl;
        success = false;
    }
    
    running_ = false;
    return success;
}

void Camera::processingThread() {
    LOG_INFO("Camera", "Background processing thread for camera " + id_ + " started");
    
    // Check if this is a file source to adapt timing behavior
    bool isFileSource = false;
    if (source_) {
        auto gstreamerSource = std::dynamic_pointer_cast<GStreamerSource>(source_);
        if (gstreamerSource) {
            auto config = gstreamerSource->getConfig();
            if (config.contains("type") && config["type"].get<std::string>() == "file") {
                isFileSource = true;
            }
        }
    }
    
    // Variables to track processing time and frame skipping
    int64_t lastFrameTimestamp = 0;
    int skippedFrames = 0;
    
    if (isFileSource) {
        LOG_INFO("Camera", "File source detected - using GStreamer timing for natural playback");
    } else {
        LOG_INFO("Camera", "Live source detected - using adaptive timing for real-time processing");
    }
    
    while (!stopProcessing_) {
        auto processStart = std::chrono::high_resolution_clock::now();

        // Process a frame
        bool frameProcessed = processFrame();
        
        auto processEnd = std::chrono::high_resolution_clock::now();
        auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            processEnd - processStart).count();
        
        if (isFileSource) {
            // For file sources, let GStreamer handle timing - just wait a bit if no frame was processed
            if (!frameProcessed) {
                std::unique_lock<std::mutex> lock(processingMutex_);
                processingCV_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                    return stopProcessing_.load();
                });
            } else {
                // For successful frame processing, minimal wait to allow GStreamer timing to work
                std::unique_lock<std::mutex> lock(processingMutex_);
                processingCV_.wait_for(lock, std::chrono::milliseconds(1), [this] {
                    return stopProcessing_.load();
                });
            }
        } else {
            // Original live source timing logic
            bool needSkipWait = false;
            if (frameProcessed) {
                int64_t currentTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                    
                if (lastFrameTimestamp > 0) {
                    int64_t timeDiff = currentTimestamp - lastFrameTimestamp;
                    // If processing is taking longer than real-time, we need to skip waiting
                    if (processingTime > 33) { // 33ms = ~30fps
                        needSkipWait = true;
                        skippedFrames++;
                        
                        // Log periodically about skipped frames
                        if (skippedFrames % 30 == 0) {
                            LOG_WARN("Camera", "Processing falling behind real-time. Skipped " 
                                   + std::to_string(skippedFrames) + " waits to catch up.");
                        }
                    }
                }
                
                lastFrameTimestamp = currentTimestamp;
            }
            
            // If no frame was processed or we need to skip waiting, don't wait or wait less
            if (!frameProcessed || needSkipWait) {
                std::unique_lock<std::mutex> lock(processingMutex_);
                processingCV_.wait_for(lock, std::chrono::milliseconds(1), [this] {
                    return stopProcessing_.load();
                });
            } else {
                // Normal waiting when we're keeping up with real-time
                std::unique_lock<std::mutex> lock(processingMutex_);
                processingCV_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                    return stopProcessing_.load();
                });
            }
        }
    }
    
    LOG_INFO("Camera", "Background processing thread for camera " + id_ + " exiting");
}

bool Camera::isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

nlohmann::json Camera::getStatus(bool includeComponents) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    nlohmann::json status;
    status["id"] = id_;
    status["name"] = name_;
    status["running"] = running_;
    
    if (includeComponents) {
        // Add source status
        if (source_) {
            status["source"] = source_->getStatus();
        }
        
        // Add processors status
        nlohmann::json processorsArray = nlohmann::json::array();
        for (const auto& pair : processors_) {
            processorsArray.push_back(pair.second->getStatus());
        }
        status["processors"] = processorsArray;
        
        // Add sinks status
        nlohmann::json sinksArray = nlohmann::json::array();
        for (const auto& pair : sinks_) {
            sinksArray.push_back(pair.second->getStatus());
        }
        status["sinks"] = sinksArray;
    }
    
    return status;
}

bool Camera::processFrame() {
    // Check if camera is running
    if (!running_) {
        LOG_DEBUG("Camera", "processFrame: Camera " + id_ + " is not running");
        return false;
    }
    
    // Check if we have a source component
    if (!source_ || !source_->isRunning()) {
        LOG_DEBUG("Camera", "processFrame: Source component is null or not running for camera " + id_);
        return false;
    }
    
    LOG_DEBUG("Camera", "processFrame: Starting frame processing for camera " + id_);
    
    // Cast source to GStreamerSource to get frame
    auto gstreamerSource = std::dynamic_pointer_cast<GStreamerSource>(source_);
    if (!gstreamerSource) {
        LOG_ERROR("Camera", "processFrame: Source is not a GStreamerSource for camera " + id_);
        return false;
    }
    
    LOG_DEBUG("Camera", "processFrame: Getting frame from GStreamer source for camera " + id_);
    
    // Get frame from source
    cv::Mat rawFrame = gstreamerSource->getFrame();
    
    // Skip if frame is empty
    if (rawFrame.empty()) {
        LOG_DEBUG("Camera", "processFrame: Frame is empty for camera " + id_);
        return false;
    }
    
    LOG_DEBUG("Camera", "processFrame: Got frame " + std::to_string(rawFrame.cols) + "x" + std::to_string(rawFrame.rows) + " for camera " + id_);
    
    // Create a collection for telemetry events
    std::vector<TelemetryEvent> telemetryEvents;
    
    // Process frame through processor components
    cv::Mat processedFrame = rawFrame.clone();
    
    // Store all detections from all object detector processors
    std::vector<ObjectDetectorProcessor::Detection> allDetections;
    
    LOG_DEBUG("Camera", "processFrame: Getting processors for camera " + id_);
    
    // Get all processors and apply them in order
    std::vector<std::shared_ptr<ProcessorComponent>> processors;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : processors_) {
            processors.push_back(pair.second);
        }
    }
    
    LOG_DEBUG("Camera", "processFrame: Found " + std::to_string(processors.size()) + " processors for camera " + id_);
    
    // Create current timestamp
    int64_t currentTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    LOG_DEBUG("Camera", "processFrame: Starting object detector processing for camera " + id_);
    
    // Process object detectors first to collect detections
    for (const auto& processor : processors) {
        if (processor->isRunning()) {
            LOG_DEBUG("Camera", "processFrame: Processing processor " + processor->getId() + " for camera " + id_);
            
            // If it's an ObjectDetectorProcessor, use its specialized method
            auto objectDetector = std::dynamic_pointer_cast<ObjectDetectorProcessor>(processor);
            if (objectDetector) {
                LOG_DEBUG("Camera", "processFrame: Found ObjectDetectorProcessor " + processor->getId() + " for camera " + id_);
                
                try {
                    auto result = objectDetector->processFrame(processedFrame);
                    processedFrame = result.first;
                    // Add the detections to our collection
                    allDetections.insert(allDetections.end(), result.second.begin(), result.second.end());
                    
                    LOG_DEBUG("Camera", "processFrame: ObjectDetectorProcessor " + processor->getId() + " produced " + std::to_string(result.second.size()) + " detections for camera " + id_);
                    
                    // Convert each detection to a standardized telemetry event
                    for (const auto& detection : result.second) {
                        TelemetryEvent event = TelemetryFactory::createDetectionEvent(
                            processor->getId(),
                            detection.className,
                            detection.confidence,
                            TelemetryBBox::fromRect(detection.bbox),
                            currentTimestamp
                        );
                        event.setCameraId(id_);
                        telemetryEvents.push_back(event);
                    }
                    
                    LOG_DEBUG("Camera", "processFrame: ObjectDetectorProcessor " + processor->getId() + " completed successfully for camera " + id_);
                } catch (const std::exception& e) {
                    LOG_ERROR("Camera", "processFrame: Exception in ObjectDetectorProcessor " + processor->getId() + " for camera " + id_ + ": " + e.what());
                    return false;
                }
            }
        } else {
            LOG_DEBUG("Camera", "processFrame: Processor " + processor->getId() + " is not running for camera " + id_);
        }
    }
    
    LOG_DEBUG("Camera", "processFrame: Collected " + std::to_string(allDetections.size()) + " total detections for camera " + id_);
    LOG_DEBUG("Camera", "processFrame: Starting object tracker processing for camera " + id_);
    
    // Process trackers with the collected detections
    std::vector<ObjectTrackerProcessor::TrackedObject> allTrackedObjects;
    for (const auto& processor : processors) {
        if (processor->isRunning()) {
            // If it's an ObjectTrackerProcessor, use its specialized method with collected detections
            auto objectTracker = std::dynamic_pointer_cast<ObjectTrackerProcessor>(processor);
            if (objectTracker) {
                LOG_DEBUG("Camera", "processFrame: Found ObjectTrackerProcessor " + processor->getId() + " for camera " + id_);
                
                try {
                    LOG_DEBUG("Camera", "processFrame: Calling processFrame on ObjectTrackerProcessor " + processor->getId() + " with " + std::to_string(allDetections.size()) + " detections for camera " + id_);
                    
                    auto result = objectTracker->processFrame(processedFrame, allDetections);
                    processedFrame = result.first;
                    // Add tracked objects to our collection
                    allTrackedObjects.insert(allTrackedObjects.end(), result.second.begin(), result.second.end());
                    
                    LOG_DEBUG("Camera", "processFrame: ObjectTrackerProcessor " + processor->getId() + " produced " + std::to_string(result.second.size()) + " tracked objects for camera " + id_);
                    
                    // Convert each tracked object to a standardized telemetry event
                    for (const auto& trackedObj : result.second) {
                        // Convert trajectory to telemetry points
                        std::vector<TelemetryPoint> trajectory;
                        for (const auto& pt : trackedObj.trajectory) {
                            trajectory.push_back(TelemetryPoint::fromPoint(pt));
                        }
                        
                        TelemetryEvent event = TelemetryFactory::createTrackingEvent(
                            processor->getId(),
                            trackedObj.trackId,
                            trackedObj.className,
                            trackedObj.confidence,
                            TelemetryBBox::fromRect(trackedObj.bbox),
                            trajectory,
                            currentTimestamp
                        );
                        event.setCameraId(id_);
                        event.setProperty("age", trackedObj.age);
                        telemetryEvents.push_back(event);
                    }
                    
                    LOG_DEBUG("Camera", "processFrame: ObjectTrackerProcessor " + processor->getId() + " completed successfully for camera " + id_);
                } catch (const std::exception& e) {
                    LOG_ERROR("Camera", "processFrame: Exception in ObjectTrackerProcessor " + processor->getId() + " for camera " + id_ + ": " + e.what());
                    return false;
                }
            }
        }
    }
    
    LOG_DEBUG("Camera", "processFrame: Collected " + std::to_string(allTrackedObjects.size()) + " total tracked objects for camera " + id_);
    LOG_DEBUG("Camera", "processFrame: Starting line zone manager processing for camera " + id_);
    
    // Process line zone managers with the tracked objects
    std::vector<LineCrossingEvent> allCrossingEvents;
    for (const auto& processor : processors) {
        if (processor->isRunning()) {
            // If it's a LineZoneManager, use its specialized method with tracked objects
            auto lineZoneManager = std::dynamic_pointer_cast<LineZoneManager>(processor);
            if (lineZoneManager) {
                LOG_DEBUG("Camera", "processFrame: Found LineZoneManager " + processor->getId() + " for camera " + id_);
                
                try {
                    auto result = lineZoneManager->processFrame(processedFrame, allTrackedObjects);
                    processedFrame = result.first;
                    // Add crossing events to our collection
                    allCrossingEvents.insert(allCrossingEvents.end(), result.second.begin(), result.second.end());
                    
                    LOG_DEBUG("Camera", "processFrame: LineZoneManager " + processor->getId() + " produced " + std::to_string(result.second.size()) + " crossing events for camera " + id_);
                    
                    // Convert each crossing event to a standardized telemetry event
                    for (const auto& crossingEvent : result.second) {
                        TelemetryEvent event = TelemetryFactory::createCrossingEvent(
                            processor->getId(),
                            crossingEvent.zoneId,
                            std::stoi(crossingEvent.objectId),  // Convert string trackId to int
                            crossingEvent.className,
                            crossingEvent.direction,
                            TelemetryPoint::fromPoint(crossingEvent.location),
                            currentTimestamp
                        );
                        event.setCameraId(id_);
                        telemetryEvents.push_back(event);
                    }
                    
                    LOG_DEBUG("Camera", "processFrame: LineZoneManager " + processor->getId() + " completed successfully for camera " + id_);
                } catch (const std::exception& e) {
                    LOG_ERROR("Camera", "processFrame: Exception in LineZoneManager " + processor->getId() + " for camera " + id_ + ": " + e.what());
                    return false;
                }
            }
        }
    }
    
    LOG_DEBUG("Camera", "processFrame: Starting polygon zone manager processing for camera " + id_);
    
    // Process polygon zone managers with the tracked objects
    std::vector<PolygonZoneEvent> allPolygonZoneEvents;
    for (const auto& processor : processors) {
        if (processor->isRunning()) {
            // If it's a PolygonZoneManager, use its specialized method with tracked objects
            auto polygonZoneManager = std::dynamic_pointer_cast<PolygonZoneManager>(processor);
            if (polygonZoneManager) {
                LOG_DEBUG("Camera", "processFrame: Found PolygonZoneManager " + processor->getId() + " for camera " + id_);
                
                try {
                    auto result = polygonZoneManager->processFrame(processedFrame, allTrackedObjects);
                    processedFrame = result.first;
                    // Add polygon zone events to our collection
                    allPolygonZoneEvents.insert(allPolygonZoneEvents.end(), result.second.begin(), result.second.end());
                    
                    LOG_DEBUG("Camera", "processFrame: PolygonZoneManager " + processor->getId() + " completed successfully for camera " + id_);
                    
                    // Convert each polygon zone event to a standardized telemetry event
                    for (const auto& zoneEvent : result.second) {
                        // Create appropriate event based on the event type
                        TelemetryEvent event(TelemetryEventType::CUSTOM, processor->getId(), currentTimestamp);
                        
                        if (zoneEvent.eventType == "zone_entry") {
                            event = TelemetryFactory::createZoneEntryEvent(
                                processor->getId(),
                                zoneEvent.zoneId,
                                std::stoi(zoneEvent.objectId),  // Convert string objectId to int
                                zoneEvent.className,
                                TelemetryPoint::fromPoint(zoneEvent.location),
                                currentTimestamp
                            );
                        } else if (zoneEvent.eventType == "zone_exit") {
                            event = TelemetryFactory::createZoneExitEvent(
                                processor->getId(),
                                zoneEvent.zoneId,
                                std::stoi(zoneEvent.objectId),  // Convert string objectId to int
                                zoneEvent.className,
                                TelemetryPoint::fromPoint(zoneEvent.location),
                                currentTimestamp
                            );
                        } else {
                            // Create a generic custom event for any other event types
                            event = TelemetryFactory::createCustomEvent(
                                processor->getId(),
                                "polygon_zone_event",
                                currentTimestamp
                            );
                            event.setProperty("event_type", zoneEvent.eventType);
                            event.setProperty("object_id", zoneEvent.objectId);
                            event.setProperty("class_name", zoneEvent.className);
                            event.setProperty("zone_id", zoneEvent.zoneId);
                        }
                        
                        // Add metadata from the event
                        for (const auto& [key, value] : zoneEvent.metadata) {
                            event.setProperty(key, value);
                        }
                        
                        event.setCameraId(id_);
                        telemetryEvents.push_back(event);
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Camera", "processFrame: Exception in PolygonZoneManager " + processor->getId() + " for camera " + id_ + ": " + e.what());
                    return false;
                }
            }
        }
    }
    
    LOG_DEBUG("Camera", "processFrame: Starting classification processor processing for camera " + id_);
    
    // Process classification processors
    std::vector<ObjectClassificationProcessor::Classification> allClassifications;
    for (const auto& processor : processors) {
        if (processor->isRunning()) {
            // If it's an ObjectClassificationProcessor, use its specialized method
            auto objectClassifier = std::dynamic_pointer_cast<ObjectClassificationProcessor>(processor);
            if (objectClassifier) {
                LOG_DEBUG("Camera", "processFrame: Found ObjectClassificationProcessor " + processor->getId() + " for camera " + id_);
                
                try {
                    auto result = objectClassifier->processFrame(processedFrame);
                    processedFrame = result.first;
                    // Add classifications to our collection
                    allClassifications.insert(allClassifications.end(), result.second.begin(), result.second.end());
                    
                    LOG_DEBUG("Camera", "processFrame: ObjectClassificationProcessor " + processor->getId() + " completed successfully for camera " + id_);
                    
                    // Convert each classification to a standardized telemetry event
                    for (const auto& classification : result.second) {
                        TelemetryEvent event = TelemetryFactory::createClassificationEvent(
                            processor->getId(),
                            classification.className,
                            classification.confidence,
                            currentTimestamp
                        );
                        event.setCameraId(id_);
                        telemetryEvents.push_back(event);
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Camera", "processFrame: Exception in ObjectClassificationProcessor " + processor->getId() + " for camera " + id_ + ": " + e.what());
                    return false;
                }
            }
        }
    }
    
    LOG_DEBUG("Camera", "processFrame: Starting age and gender detection processing for camera " + id_);
    
    // Process age and gender detection
    std::vector<AgeGenderDetectionProcessor::AgeGenderResult> allAgeGenderResults;
    for (const auto& processor : processors) {
        if (processor->isRunning()) {
            // If it's an AgeGenderDetectionProcessor, use its specialized method
            auto ageGenderDetector = std::dynamic_pointer_cast<AgeGenderDetectionProcessor>(processor);
            if (ageGenderDetector) {
                LOG_DEBUG("Camera", "processFrame: Found AgeGenderDetectionProcessor " + processor->getId() + " for camera " + id_);
                
                try {
                    auto result = ageGenderDetector->processFrame(processedFrame);
                    processedFrame = result.first;
                    // Add results to our collection
                    allAgeGenderResults.insert(allAgeGenderResults.end(), result.second.begin(), result.second.end());
                    
                    LOG_DEBUG("Camera", "processFrame: AgeGenderDetectionProcessor " + processor->getId() + " completed successfully for camera " + id_);
                    
                    // Convert each result to a standardized telemetry event
                    for (const auto& ageGenderResult : result.second) {
                        TelemetryEvent event = TelemetryFactory::createCustomEvent(
                            processor->getId(),
                            "age_gender_detection",
                            currentTimestamp
                        );
                        event.setCameraId(id_);
                        event.setProperty("age", ageGenderResult.age);
                        event.setProperty("age_confidence", ageGenderResult.ageConfidence);
                        event.setProperty("gender", ageGenderResult.gender);
                        event.setProperty("gender_confidence", ageGenderResult.genderConfidence);
                        
                        // Convert bbox to telemetry format
                        TelemetryBBox bbox;
                        bbox.x = ageGenderResult.bbox.x;
                        bbox.y = ageGenderResult.bbox.y;
                        bbox.width = ageGenderResult.bbox.width;
                        bbox.height = ageGenderResult.bbox.height;
                        event.setProperty("bbox", bbox.toJson());
                        
                        telemetryEvents.push_back(event);
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Camera", "processFrame: Exception in AgeGenderDetectionProcessor " + processor->getId() + " for camera " + id_ + ": " + e.what());
                    return false;
                }
            }
        }
    }
    
    LOG_DEBUG("Camera", "processFrame: Starting sink processing for camera " + id_);
    
    // Send frames and telemetry to sink components
    std::vector<std::shared_ptr<SinkComponent>> sinks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : sinks_) {
            sinks.push_back(pair.second);
        }
    }
    
    for (const auto& sink : sinks) {
        if (sink->isRunning()) {
            LOG_DEBUG("Camera", "processFrame: Processing sink " + sink->getId() + " for camera " + id_);
            
            try {
                // If it's a FileSink, use its specialized method for frames
                auto fileSink = std::dynamic_pointer_cast<FileSink>(sink);
                if (fileSink) {
                    // Use the config to determine which frame to use
                    bool useRawFrame = fileSink->getConfig().contains("use_raw_frame") && 
                                      fileSink->getConfig()["use_raw_frame"].get<bool>();
                    
                    fileSink->processFrame(useRawFrame ? rawFrame : processedFrame);
                    LOG_DEBUG("Camera", "processFrame: FileSink " + sink->getId() + " completed successfully for camera " + id_);
                }
                
                // If it's a DatabaseSink, use its specialized method for telemetry
                auto databaseSink = std::dynamic_pointer_cast<DatabaseSink>(sink);
                if (databaseSink) {
                    // Always pass the raw frame for heatmap generation and the telemetry events
                    databaseSink->processTelemetry(rawFrame, telemetryEvents);
                    LOG_DEBUG("Camera", "processFrame: DatabaseSink " + sink->getId() + " completed successfully for camera " + id_);
                }
                // Add additional sink types here as needed
            } catch (const std::exception& e) {
                LOG_ERROR("Camera", "processFrame: Exception in sink " + sink->getId() + " for camera " + id_ + ": " + e.what());
                return false;
            }
        }
    }
    
    LOG_DEBUG("Camera", "processFrame: Storing frames and data for camera " + id_);
    
    // Store both the raw and latest processed frames, detections, tracked objects, and telemetry events
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        rawFrame_ = rawFrame.clone();
        latestFrame_ = processedFrame.clone();
        latestDetections_ = allDetections;
        latestTrackedObjects_ = allTrackedObjects;
        latestTelemetryEvents_ = telemetryEvents;
    }
    
    LOG_DEBUG("Camera", "processFrame: Signaling frame processed for camera " + id_);
    
    // In the simplified implementation, we don't need to signal frame processing
    // The source continuously captures frames regardless of processing speed
    // Kept for backward compatibility
    gstreamerSource->signalFrameProcessed();
    
    LOG_DEBUG("Camera", "processFrame: Frame processing completed successfully for camera " + id_);
    
    return true;
}

cv::Mat Camera::getLatestFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return latestFrame_.clone();
}

std::vector<uchar> Camera::getLatestFrameJpeg(int quality) const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    
    std::vector<uchar> buffer;
    
    // Check if we have a frame
    if (latestFrame_.empty()) {
        return buffer;
    }
    
    // Encode frame as JPEG
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
    cv::imencode(".jpg", latestFrame_, buffer, params);
    
    return buffer;
}

std::vector<ObjectDetectorProcessor::Detection> Camera::getLatestDetections() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return latestDetections_;
}

std::vector<ObjectTrackerProcessor::TrackedObject> Camera::getLatestTrackedObjects() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return latestTrackedObjects_;
}

std::vector<TelemetryEvent> Camera::getLatestTelemetryEvents() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return latestTelemetryEvents_;
}

// Add method to get raw frame
cv::Mat Camera::getRawFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return rawFrame_.clone();
}

// Add method to get raw frame as JPEG
std::vector<uchar> Camera::getRawFrameJpeg(int quality) const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    
    std::vector<uchar> buffer;
    
    // Check if we have a frame
    if (rawFrame_.empty()) {
        return buffer;
    }
    
    // Encode frame as JPEG
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
    cv::imencode(".jpg", rawFrame_, buffer, params);
    
    return buffer;
}

} // namespace tapi 
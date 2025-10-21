#pragma once

#include "component.h"
#include "components/telemetry.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <mutex>
#include <memory>
#include <sqlite3.h>
#include <atomic>
#include <functional>

namespace tapi {

/**
 * @brief Simplified Database sink component for storing telemetry data in SQLite
 */
class DatabaseSink : public SinkComponent {
public:
    /**
     * @brief Enum for bounding box anchor points (simplified)
     */
    enum class BBoxAnchor {
        CENTER,
        BOTTOM_CENTER,
        TOP_CENTER,
        LEFT_CENTER,
        RIGHT_CENTER,
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT
    };

    // Structure to hold a batch of telemetry data
    struct TelemetryBatch {
        cv::Mat frame;
        std::vector<TelemetryEvent> events;
        int64_t timestamp;
    };

    /**
     * @brief Construct a new Database Sink object
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Component type (should be "database")
     * @param config Initial configuration
     */
    DatabaseSink(const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config);
    
    /**
     * @brief Destructor
     */
    ~DatabaseSink() override;

    /**
     * @brief Initialize the database sink
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override;
    
    /**
     * @brief Start the database sink
     * 
     * @return true if start succeeded, false otherwise
     */
    bool start() override;
    
    /**
     * @brief Stop the database sink
     * 
     * @return true if stop succeeded, false otherwise
     */
    bool stop() override;
    
    /**
     * @brief Update component configuration
     * 
     * @param config JSON configuration
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override;
    
    /**
     * @brief Get component configuration
     * 
     * @return nlohmann::json Current configuration
     */
    nlohmann::json getConfig() const override;

    /**
     * @brief Get component status
     * 
     * @return nlohmann::json Component status
     */
    nlohmann::json getStatus() const override;
    
    /**
     * @brief Process a frame and telemetry events
     * 
     * @param frame The frame to process (for thumbnail generation)
     * @param events The telemetry events to store
     * @return true if processing succeeded, false otherwise
     */
    bool processTelemetry(const cv::Mat& frame, const std::vector<TelemetryEvent>& events);
    
    /**
     * @brief Get the database path
     * 
     * @return std::string The current database path
     */
    std::string getDatabasePath() const;
    
    /**
     * @brief Delete all data for a specific camera
     * 
     * @param cameraId ID of the camera whose data should be deleted
     * @return true if successful, false otherwise
     */
    bool deleteDataForCamera(const std::string& cameraId);
    
    /**
     * @brief Delete all data for a specific camera with progress reporting
     * 
     * @param cameraId ID of the camera whose data should be deleted
     * @param progressCallback Callback function to report progress (0-100)
     * @return true if successful, false otherwise
     */
    bool deleteDataForCamera(const std::string& cameraId, std::function<void(double, std::string)> progressCallback);


    
    // Analytics methods
    nlohmann::json getAnalytics(const std::string& cameraId) const;
    nlohmann::json getTimeSeriesData(const std::string& cameraId, int64_t start_time = 0, int64_t end_time = 0) const;
    nlohmann::json getDwellTimeAnalytics(const std::string& cameraId, int64_t start_time = 0, int64_t end_time = 0) const;
    nlohmann::json getHeatmapData(const std::string& cameraId) const;
    nlohmann::json getEventSummary(const std::string& cameraId) const;
    nlohmann::json getZoneLineCounts(const std::string& cameraId, int64_t start_time = 0, int64_t end_time = 0) const;
    nlohmann::json getClassBasedHeatmapData(const std::string& cameraId) const;
    
    /**
     * @brief Generate a heatmap image (simplified stub)
     */
    std::vector<uchar> generateHeatmapImage(
        const std::string& cameraId, 
        const cv::Mat& backgroundImage = cv::Mat(), 
        BBoxAnchor anchor = BBoxAnchor::CENTER,
        const std::vector<std::string>& classFilter = {},
        int quality = 90) const;
    
    /**
     * @brief Get available class names (simplified stub)
     */
    std::vector<std::string> getAvailableClasses(const std::string& cameraId) const;
    
    /**
     * @brief Convert a string to BBoxAnchor enum value
     */
    BBoxAnchor stringToAnchor(const std::string& anchorStr);
    
    /**
     * @brief Get database performance statistics
     */
    nlohmann::json getDatabasePerformanceStats(const std::string& cameraId) const;
    
    /**
     * @brief Explain query execution plan for optimization
     */
    nlohmann::json explainQuery(const std::string& query) const;

    /**
     * @brief Update analytics summary aggregate table
     */
    void updateAnalyticsSummary(const std::string& cameraId, int64_t timestamp);
    
    /**
     * @brief Clean up old aggregate data beyond retention period
     */
    void cleanupAggregateData(int64_t cutoffTime);
    
    /**
     * @brief Reset 24-hour counts in event type counts table
     */
    void resetRecent24hCounts();

private:
    /**
     * @brief Create database tables if they don't exist
     */
    bool createTables();
    
    /**
     * @brief Insert a telemetry event into the database
     */
    bool insertEvent(const TelemetryEvent& event, int64_t frameId);
    
    /**
     * @brief Update aggregate tables for high-performance analytics
     */
    void updateAggregateTablesForEvent(const TelemetryEvent& event, const std::string& cameraId);
    
    /**
     * @brief Update event type counts aggregate table
     */
    void updateEventTypeCounts(const std::string& cameraId, int eventType, int64_t timestamp);
    
    /**
     * @brief Update class distribution aggregate table
     */
    void updateClassDistribution(const std::string& cameraId, const std::string& className, int eventType);
    
    /**
     * @brief Update time series buckets aggregate table
     */
    void updateTimeSeriesBuckets(const std::string& cameraId, int64_t timestamp, int eventType, const std::string& className);
    
    /**
     * @brief Update dwell times aggregate table
     */
    void updateDwellTimes(const std::string& cameraId, const std::string& trackId, const std::string& className, int64_t timestamp);
    
    /**
     * @brief Insert a frame record into the database
     */
    int64_t insertFrame(const cv::Mat& frame);
    
    /**
     * @brief Generate a thumbnail from frame
     */
    std::string generateThumbnail(const cv::Mat& frame);
    
    /**
     * @brief Clean up old data based on retention period
     */
    void cleanupOldData();
    
    /**
     * @brief Optimize database for better performance
     */
    void optimizeDatabase();
    
    /**
     * @brief Calculate position based on specified anchor point (simplified stub)
     */
    std::pair<double, double> getAnchorPosition(const nlohmann::json& bbox, BBoxAnchor anchor) const;

    // Simplified stub methods (kept for compatibility)
    bool updateHeatmapMatrix(const std::string& cameraId, const nlohmann::json& properties);
    bool checkSchemaVersion();
    bool validateConfig(const nlohmann::json& config);
    bool executeWithRetry(sqlite3_stmt* stmt, int maxRetries = 3);
    bool createSchemaVersionTable();
    int getCurrentSchemaVersion();
    bool setSchemaVersion(int version);
    bool canAllocateMemory(size_t size);
    void updateMemoryUsage(int64_t delta);
    bool insertTelemetryEvent(int64_t frameId, const TelemetryEvent& event);
    bool createAggregateTables();
    bool processBatch(const TelemetryBatch& batch);
    int64_t insertFrame(int64_t timestamp, const std::string& thumbnail);
    bool updateAggregates(const std::vector<TelemetryEvent>& events, const std::string& cameraId);
    bool initializeReaderDB();
    bool initializeWriterDB();
    void writerThreadFunction();

private:
    std::string type_;                    ///< Component type name
    std::string dbPath_;                  ///< Path to database file
    bool storeThumbnails_;                ///< Whether to store frame thumbnails
    int thumbnailWidth_;                  ///< Width of thumbnails to store
    int thumbnailHeight_;                 ///< Height of thumbnails to store
    int retentionDays_;                   ///< Number of days to retain data (0 = forever)
    
    sqlite3* db_;                         ///< SQLite database handle
    sqlite3* writerDb_;                   ///< Legacy writer handle (unused in simplified version)
    mutable std::mutex dbMutex_;          ///< Mutex for database access
    bool isInitialized_;                  ///< Whether the database is initialized
    int64_t insertedFrames_;              ///< Number of frames inserted
    int64_t insertedEvents_;              ///< Number of events inserted
    bool storeDetectionEvents_;           ///< Whether to store detection events
    bool storeTrackingEvents_;            ///< Whether to store tracking events
    bool storeCountingEvents_;            ///< Whether to store counting events

    // Legacy fields (kept for compatibility but unused in simplified version)
    std::atomic<bool> writerThreadRunning_;
    int maxQueueSize_;
    int batchSize_;
    std::atomic<int> queuedBatches_;
    size_t maxMemoryUsage_;
    std::atomic<size_t> currentMemoryUsage_;
};

} // namespace tapi 
#include "components/sink/database_sink.h"
#include "logger.h"
#include "camera.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <ctime>
#include <filesystem>
#include <thread>
#include <locale>
#include <codecvt>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace tapi {

// UTF-8 validation and sanitization utilities
namespace {

/**
 * @brief Check if a byte sequence is valid UTF-8
 */
bool isValidUTF8(const std::string& str) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(str.c_str());
    const unsigned char* end = bytes + str.length();
    
    while (bytes < end) {
        if (*bytes < 0x80) {
            // ASCII character
            bytes++;
        } else if ((*bytes >> 5) == 0x06) {
            // 110xxxxx - 2 byte sequence
            if (bytes + 1 >= end || (bytes[1] & 0xC0) != 0x80) return false;
            bytes += 2;
        } else if ((*bytes >> 4) == 0x0E) {
            // 1110xxxx - 3 byte sequence
            if (bytes + 2 >= end || (bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80) return false;
            bytes += 3;
        } else if ((*bytes >> 3) == 0x1E) {
            // 11110xxx - 4 byte sequence
            if (bytes + 3 >= end || (bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80 || (bytes[3] & 0xC0) != 0x80) return false;
            bytes += 4;
        } else {
            // Invalid UTF-8 sequence
            return false;
        }
    }
    return true;
}

/**
 * @brief Sanitize a string to ensure it's valid UTF-8
 */
std::string sanitizeUTF8(const std::string& input) {
    if (input.empty()) return input;
    
    // First check if it's already valid UTF-8
    if (isValidUTF8(input)) {
        return input;
    }
    
    // If not valid, replace invalid sequences with replacement character
    std::string result;
    result.reserve(input.size());
    
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(input.c_str());
    const unsigned char* end = bytes + input.length();
    
    while (bytes < end) {
        if (*bytes < 0x80) {
            // ASCII character - always valid
            result += static_cast<char>(*bytes);
            bytes++;
        } else if ((*bytes >> 5) == 0x06) {
            // 110xxxxx - 2 byte sequence
            if (bytes + 1 < end && (bytes[1] & 0xC0) == 0x80) {
                result += static_cast<char>(*bytes);
                result += static_cast<char>(*(bytes + 1));
                bytes += 2;
            } else {
                result += "\xEF\xBF\xBD"; // UTF-8 replacement character
                bytes++;
            }
        } else if ((*bytes >> 4) == 0x0E) {
            // 1110xxxx - 3 byte sequence
            if (bytes + 2 < end && (bytes[1] & 0xC0) == 0x80 && (bytes[2] & 0xC0) == 0x80) {
                result += static_cast<char>(*bytes);
                result += static_cast<char>(*(bytes + 1));
                result += static_cast<char>(*(bytes + 2));
                bytes += 3;
            } else {
                result += "\xEF\xBF\xBD"; // UTF-8 replacement character
                bytes++;
            }
        } else if ((*bytes >> 3) == 0x1E) {
            // 11110xxx - 4 byte sequence
            if (bytes + 3 < end && (bytes[1] & 0xC0) == 0x80 && (bytes[2] & 0xC0) == 0x80 && (bytes[3] & 0xC0) == 0x80) {
                result += static_cast<char>(*bytes);
                result += static_cast<char>(*(bytes + 1));
                result += static_cast<char>(*(bytes + 2));
                result += static_cast<char>(*(bytes + 3));
                bytes += 4;
            } else {
                result += "\xEF\xBF\xBD"; // UTF-8 replacement character
                bytes++;
            }
        } else {
            // Invalid UTF-8 start byte - replace with replacement character
            result += "\xEF\xBF\xBD"; // UTF-8 replacement character
            bytes++;
        }
    }
    
    return result;
}

/**
 * @brief Sanitize JSON recursively to ensure all string values are valid UTF-8
 */
nlohmann::json sanitizeJsonUTF8(const nlohmann::json& input) {
    if (input.is_string()) {
        return sanitizeUTF8(input.get<std::string>());
    } else if (input.is_object()) {
        nlohmann::json result = nlohmann::json::object();
        for (auto it = input.begin(); it != input.end(); ++it) {
            std::string key = sanitizeUTF8(it.key());
            result[key] = sanitizeJsonUTF8(it.value());
        }
        return result;
    } else if (input.is_array()) {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : input) {
            result.push_back(sanitizeJsonUTF8(item));
        }
        return result;
    } else {
        // Numbers, booleans, null - return as-is
        return input;
    }
}

/**
 * @brief Safely parse JSON string with UTF-8 error handling
 */
nlohmann::json safeParseJson(const std::string& jsonStr, const std::string& fallbackKey = "raw_data") {
    if (jsonStr.empty()) {
        return nlohmann::json::object();
    }
    
    try {
        // First, sanitize the UTF-8
        std::string sanitizedJson = sanitizeUTF8(jsonStr);
        
        // Try to parse the sanitized JSON
        nlohmann::json result = nlohmann::json::parse(sanitizedJson);
        
        // Additional sanitization of the parsed JSON
        return sanitizeJsonUTF8(result);
        
    } catch (const nlohmann::json::parse_error& e) {
        LOG_WARN("DatabaseSink", "Failed to parse JSON properties, creating fallback object: " + std::string(e.what()));
        
        // If parsing fails, create a safe fallback object
        nlohmann::json fallback = nlohmann::json::object();
        fallback[fallbackKey] = sanitizeUTF8(jsonStr);
        fallback["_parse_error"] = true;
        fallback["_error_message"] = "Original data contained invalid JSON";
        return fallback;
    } catch (const std::exception& e) {
        LOG_WARN("DatabaseSink", "Unexpected error parsing JSON: " + std::string(e.what()));
        
        nlohmann::json fallback = nlohmann::json::object();
        fallback[fallbackKey] = sanitizeUTF8(jsonStr);
        fallback["_parse_error"] = true;
        fallback["_error_message"] = std::string("Unexpected error: ") + e.what();
        return fallback;
    }
}

} // anonymous namespace

DatabaseSink::DatabaseSink(const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config)
    : SinkComponent(id, camera), 
      type_(type),
      storeThumbnails_(false),
      thumbnailWidth_(320),
      thumbnailHeight_(180),
      retentionDays_(30),
      db_(nullptr),
      writerDb_(nullptr),
      isInitialized_(false),
      insertedFrames_(0),
      insertedEvents_(0),
      storeDetectionEvents_(true),
      storeTrackingEvents_(true),
      storeCountingEvents_(true),
      writerThreadRunning_(false),
      maxQueueSize_(100), // Simplified queue size
      batchSize_(10), // Simplified batch size
      queuedBatches_(0),
      maxMemoryUsage_(50 * 1024 * 1024), // Reduced to 50MB
      currentMemoryUsage_(0) {
    
    LOG_INFO("DatabaseSink", "Created simplified DatabaseSink with ID: " + id);
    
    // Set camera-specific database path
    if (camera_) {
        dbPath_ = "./data/telemetry_" + camera_->getId() + ".db";
    } else {
        dbPath_ = "./data/telemetry.db";
    }
    
    updateConfig(config);
}

DatabaseSink::~DatabaseSink() {
    LOG_INFO("DatabaseSink", "Destroying DatabaseSink with ID: " + getId());
    
    if (isRunning()) {
        stop();
    }
    
    // Simple cleanup
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    
    LOG_INFO("DatabaseSink", "DatabaseSink destroyed");
}

bool DatabaseSink::initialize() {
    LOG_INFO("DatabaseSink", "Initializing simplified DatabaseSink with ID: " + getId());
    
    // Create data directory if it doesn't exist
    std::filesystem::path dbDir = std::filesystem::path(dbPath_).parent_path();
    if (!std::filesystem::exists(dbDir)) {
        try {
            std::filesystem::create_directories(dbDir);
            LOG_INFO("DatabaseSink", "Created data directory: " + dbDir.string());
        } catch (const std::exception& e) {
            LOG_ERROR("DatabaseSink", "Failed to create data directory: " + std::string(e.what()));
            return false;
        }
    }
    
    // Initialize single database connection
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DatabaseSink", "Failed to open database: " + std::string(sqlite3_errmsg(db_)));
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    
    // SQLite configuration with UTF-8 enforcement and performance optimizations
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA cache_size=10000;", nullptr, nullptr, nullptr); // Increased cache
    sqlite3_exec(db_, "PRAGMA temp_store=memory;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA encoding='UTF-8';", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA busy_timeout=30000;", nullptr, nullptr, nullptr); // 30 second timeout
    sqlite3_exec(db_, "PRAGMA mmap_size=268435456;", nullptr, nullptr, nullptr); // 256MB memory map
    sqlite3_exec(db_, "PRAGMA optimize;", nullptr, nullptr, nullptr); // Auto-optimize query planner
    
    // Create simple table
    if (!createTables()) {
        LOG_ERROR("DatabaseSink", "Failed to create database tables");
        return false;
    }
    
    isInitialized_ = true;
    LOG_INFO("DatabaseSink", "Database initialized successfully: " + dbPath_);
    return true;
}

bool DatabaseSink::start() {
    LOG_INFO("DatabaseSink", "Starting simplified DatabaseSink with ID: " + getId());
    
    if (!isInitialized_ && !initialize()) {
        LOG_ERROR("DatabaseSink", "Failed to initialize database");
        return false;
    }
    
    running_ = true;
    LOG_INFO("DatabaseSink", "DatabaseSink started successfully");
    return true;
}

bool DatabaseSink::stop() {
    LOG_INFO("DatabaseSink", "Stopping DatabaseSink with ID: " + getId());
    running_ = false;
    LOG_INFO("DatabaseSink", "DatabaseSink stopped");
    return true;
}

bool DatabaseSink::updateConfig(const nlohmann::json& config) {
    LOG_INFO("DatabaseSink", "Updating configuration for DatabaseSink with ID: " + getId());
    
    try {
        // Simple configuration updates
        if (config.contains("store_thumbnails")) {
            storeThumbnails_ = config["store_thumbnails"].get<bool>();
        }
        
        if (config.contains("thumbnail_width")) {
            thumbnailWidth_ = config["thumbnail_width"].get<int>();
        }
        
        if (config.contains("thumbnail_height")) {
            thumbnailHeight_ = config["thumbnail_height"].get<int>();
        }
        
        if (config.contains("retention_days")) {
            retentionDays_ = config["retention_days"].get<int>();
        }
        
        if (config.contains("store_detection_events")) {
            storeDetectionEvents_ = config["store_detection_events"].get<bool>();
        }
        
        if (config.contains("store_tracking_events")) {
            storeTrackingEvents_ = config["store_tracking_events"].get<bool>();
        }
        
        if (config.contains("store_counting_events")) {
            storeCountingEvents_ = config["store_counting_events"].get<bool>();
        }
        
        LOG_INFO("DatabaseSink", "Configuration updated successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("DatabaseSink", "Failed to update configuration: " + std::string(e.what()));
        return false;
    }
}

nlohmann::json DatabaseSink::getConfig() const {
    nlohmann::json config;
    config["store_thumbnails"] = storeThumbnails_;
    config["thumbnail_width"] = thumbnailWidth_;
    config["thumbnail_height"] = thumbnailHeight_;
    config["retention_days"] = retentionDays_;
    config["store_detection_events"] = storeDetectionEvents_;
    config["store_tracking_events"] = storeTrackingEvents_;
    config["store_counting_events"] = storeCountingEvents_;
    return config;
}

nlohmann::json DatabaseSink::getStatus() const {
    auto status = Component::getStatus();
    status["type"] = "database";
    status["store_thumbnails"] = storeThumbnails_;
    status["thumbnail_width"] = thumbnailWidth_;
    status["thumbnail_height"] = thumbnailHeight_;
    status["retention_days"] = retentionDays_;
    status["inserted_frames"] = insertedFrames_;
    status["inserted_events"] = insertedEvents_;
    status["initialized"] = isInitialized_;
    status["store_detection_events"] = storeDetectionEvents_;
    status["store_tracking_events"] = storeTrackingEvents_;
    status["store_counting_events"] = storeCountingEvents_;
    return status;
}

bool DatabaseSink::createTables() {
    if (!db_) {
        LOG_ERROR("DatabaseSink", "Database not initialized");
        return false;
    }
    
    // Create robust telemetry events table with UTF-8 constraints
    const char* createEventsTable = R"(
        CREATE TABLE IF NOT EXISTS telemetry_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            camera_id TEXT NOT NULL CHECK(length(camera_id) > 0),
            timestamp INTEGER NOT NULL CHECK(timestamp > 0),
            event_type INTEGER NOT NULL CHECK(event_type >= 0),
            source_id TEXT NOT NULL CHECK(length(source_id) > 0),
            properties TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(properties)),
            frame_id INTEGER,
            created_at INTEGER DEFAULT (strftime('%s', 'now') * 1000)
        );
    )";
    
    // Create robust frames table
    const char* createFramesTable = R"(
        CREATE TABLE IF NOT EXISTS frames (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            camera_id TEXT NOT NULL CHECK(length(camera_id) > 0),
            timestamp INTEGER NOT NULL CHECK(timestamp > 0),
            thumbnail BLOB,
            width INTEGER CHECK(width > 0),
            height INTEGER CHECK(height > 0),
            created_at INTEGER DEFAULT (strftime('%s', 'now') * 1000)
        );
    )";

    // Create aggregate tables for high-performance analytics
    const char* createAnalyticsSummaryTable = R"(
        CREATE TABLE IF NOT EXISTS analytics_summary (
            camera_id TEXT NOT NULL,
            summary_key TEXT NOT NULL,
            summary_value TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(summary_value)),
            last_updated INTEGER DEFAULT (strftime('%s', 'now') * 1000),
            PRIMARY KEY (camera_id, summary_key)
        );
    )";

    const char* createTimeSeriesBucketsTable = R"(
        CREATE TABLE IF NOT EXISTS time_series_buckets (
            camera_id TEXT NOT NULL,
            bucket_timestamp INTEGER NOT NULL,
            bucket_size INTEGER NOT NULL,
            event_type INTEGER NOT NULL,
            class_name TEXT,
            event_count INTEGER NOT NULL DEFAULT 0,
            last_updated INTEGER DEFAULT (strftime('%s', 'now') * 1000),
            PRIMARY KEY (camera_id, bucket_timestamp, bucket_size, event_type, class_name)
        );
    )";

    const char* createClassDistributionTable = R"(
        CREATE TABLE IF NOT EXISTS class_distribution (
            camera_id TEXT NOT NULL,
            class_name TEXT NOT NULL,
            event_type INTEGER NOT NULL,
            total_count INTEGER NOT NULL DEFAULT 0,
            last_updated INTEGER DEFAULT (strftime('%s', 'now') * 1000),
            PRIMARY KEY (camera_id, class_name, event_type)
        );
    )";

    const char* createDwellTimesTable = R"(
        CREATE TABLE IF NOT EXISTS dwell_times (
            camera_id TEXT NOT NULL,
            track_id TEXT NOT NULL,
            class_name TEXT NOT NULL,
            first_seen INTEGER NOT NULL,
            last_seen INTEGER NOT NULL,
            detection_count INTEGER NOT NULL DEFAULT 1,
            dwell_time_ms INTEGER GENERATED ALWAYS AS (last_seen - first_seen) STORED,
            last_updated INTEGER DEFAULT (strftime('%s', 'now') * 1000),
            PRIMARY KEY (camera_id, track_id, class_name)
        );
    )";

    const char* createEventTypeCountsTable = R"(
        CREATE TABLE IF NOT EXISTS event_type_counts (
            camera_id TEXT NOT NULL,
            event_type INTEGER NOT NULL,
            total_count INTEGER NOT NULL DEFAULT 0,
            recent_count_24h INTEGER NOT NULL DEFAULT 0,
            last_updated INTEGER DEFAULT (strftime('%s', 'now') * 1000),
            last_24h_updated INTEGER DEFAULT (strftime('%s', 'now') * 1000),
            PRIMARY KEY (camera_id, event_type)
        );
    )";
    
    // Advanced indexes for high-performance queries with millions of rows
    const char* createIndexes[] = {
        // Primary query indexes
        "CREATE INDEX IF NOT EXISTS idx_events_camera_timestamp ON telemetry_events(camera_id, timestamp DESC);",
        "CREATE INDEX IF NOT EXISTS idx_events_camera_type_timestamp ON telemetry_events(camera_id, event_type, timestamp DESC);",
        "CREATE INDEX IF NOT EXISTS idx_events_timestamp ON telemetry_events(timestamp DESC);",
        
        // Analytics-specific indexes
        "CREATE INDEX IF NOT EXISTS idx_events_type ON telemetry_events(event_type);",
        "CREATE INDEX IF NOT EXISTS idx_events_camera_type ON telemetry_events(camera_id, event_type);",
        
        // Time series query optimization indexes
        "CREATE INDEX IF NOT EXISTS idx_events_camera_timestamp_type ON telemetry_events(camera_id, timestamp, event_type);",
        "CREATE INDEX IF NOT EXISTS idx_events_timestamp_type ON telemetry_events(timestamp, event_type);",
        
        // Covering indexes for common queries (include data in index to avoid table lookup)
        "CREATE INDEX IF NOT EXISTS idx_events_camera_timestamp_covering ON telemetry_events(camera_id, timestamp DESC, event_type, source_id);",
        
        // JSON extraction indexes for classification queries
        "CREATE INDEX IF NOT EXISTS idx_events_camera_class_name ON telemetry_events(camera_id, json_extract(properties, '$.class_name')) WHERE json_extract(properties, '$.class_name') IS NOT NULL;",
        "CREATE INDEX IF NOT EXISTS idx_events_track_id ON telemetry_events(json_extract(properties, '$.track_id')) WHERE json_extract(properties, '$.track_id') IS NOT NULL;",
        
        // Recent data optimization index (removed non-deterministic WHERE clause)
        "CREATE INDEX IF NOT EXISTS idx_events_camera_recent ON telemetry_events(camera_id, timestamp DESC);",
        
        // Frames table indexes
        "CREATE INDEX IF NOT EXISTS idx_frames_camera_timestamp ON frames(camera_id, timestamp DESC);",
        
        // Composite indexes for complex queries
        "CREATE INDEX IF NOT EXISTS idx_events_camera_type_track ON telemetry_events(camera_id, event_type, json_extract(properties, '$.track_id')) WHERE event_type IN (0, 1) AND json_extract(properties, '$.track_id') IS NOT NULL;",
        
        // Sparse index for frames that have associated events (only for rows with frame_id)
        "CREATE INDEX IF NOT EXISTS idx_events_frame_id ON telemetry_events(frame_id) WHERE frame_id IS NOT NULL;",

        // Aggregate table indexes for ultra-fast analytics queries
        "CREATE INDEX IF NOT EXISTS idx_analytics_summary_camera ON analytics_summary(camera_id, summary_key);",
        "CREATE INDEX IF NOT EXISTS idx_time_series_camera_bucket ON time_series_buckets(camera_id, bucket_size, bucket_timestamp DESC);",
        "CREATE INDEX IF NOT EXISTS idx_time_series_camera_type_bucket ON time_series_buckets(camera_id, event_type, bucket_size, bucket_timestamp DESC);",
        "CREATE INDEX IF NOT EXISTS idx_class_distribution_camera ON class_distribution(camera_id, total_count DESC);",
        "CREATE INDEX IF NOT EXISTS idx_dwell_times_camera ON dwell_times(camera_id, dwell_time_ms DESC);",
        "CREATE INDEX IF NOT EXISTS idx_dwell_times_class ON dwell_times(camera_id, class_name, dwell_time_ms DESC);",
        "CREATE INDEX IF NOT EXISTS idx_event_type_counts_camera ON event_type_counts(camera_id, event_type);"
    };
    
    // Execute table creation
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, createEventsTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DatabaseSink", "Failed to create telemetry_events table: " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    
    rc = sqlite3_exec(db_, createFramesTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DatabaseSink", "Failed to create frames table: " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }

    // Create aggregate tables
    const char* aggregateTables[] = {
        createAnalyticsSummaryTable,
        createTimeSeriesBucketsTable,
        createClassDistributionTable,
        createDwellTimesTable,
        createEventTypeCountsTable
    };

    for (const char* tableQuery : aggregateTables) {
        rc = sqlite3_exec(db_, tableQuery, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            LOG_ERROR("DatabaseSink", "Failed to create aggregate table: " + std::string(errMsg));
            sqlite3_free(errMsg);
            return false;
        }
    }
    
    // Create indexes with error handling
    for (const char* indexSql : createIndexes) {
        rc = sqlite3_exec(db_, indexSql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            LOG_WARN("DatabaseSink", "Failed to create index: " + std::string(errMsg) + " (SQL: " + std::string(indexSql) + ")");
            sqlite3_free(errMsg);
            errMsg = nullptr;
            // Continue with other indexes instead of failing completely
        }
    }
    
    // Set additional performance pragmas for large datasets
    sqlite3_exec(db_, "PRAGMA optimize;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA auto_vacuum=INCREMENTAL;", nullptr, nullptr, nullptr); // Better for large DBs
    sqlite3_exec(db_, "PRAGMA incremental_vacuum(1000);", nullptr, nullptr, nullptr); // Reclaim space gradually
    sqlite3_exec(db_, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, nullptr); // Checkpoint WAL for performance
    
    LOG_INFO("DatabaseSink", "Database tables and advanced indexes created successfully");
    return true;
}

bool DatabaseSink::processTelemetry(const cv::Mat& frame, const std::vector<TelemetryEvent>& events) {
    if (!isRunning() || !isInitialized_ || !db_) {
        return false;
    }
    
    // Filter events based on configuration
    std::vector<TelemetryEvent> filteredEvents;
    for (const auto& event : events) {
        bool shouldStore = false;
        switch (event.getType()) {
            case TelemetryEventType::DETECTION:
                shouldStore = storeDetectionEvents_;
                break;
            case TelemetryEventType::TRACKING:
                shouldStore = storeTrackingEvents_;
                break;
            case TelemetryEventType::CROSSING:
                shouldStore = storeCountingEvents_;
                break;
            case TelemetryEventType::CLASSIFICATION:
            case TelemetryEventType::CUSTOM:
                shouldStore = true;
                break;
        }
        
        if (shouldStore) {
            filteredEvents.push_back(event);
        }
    }
    
    if (filteredEvents.empty()) {
        return true;
    }
    
    // Simple synchronous processing
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    // Insert frame if thumbnails are enabled
    int64_t frameId = -1;
    if (storeThumbnails_ && !frame.empty()) {
        frameId = insertFrame(frame);
    }
    
    // Insert events directly
    for (const auto& event : filteredEvents) {
        if (insertEvent(event, frameId)) {
            insertedEvents_++;
        }
    }
    
    return true;
}

bool DatabaseSink::insertEvent(const TelemetryEvent& event, int64_t frameId) {
    if (!db_) {
        return false;
    }
    
    const char* sql = "INSERT INTO telemetry_events (camera_id, timestamp, event_type, source_id, properties, frame_id) VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DatabaseSink", "Failed to prepare event statement: " + std::string(sqlite3_errmsg(db_)));
        return false;
    }
    
    // Sanitize camera ID and source ID
    std::string cameraId = sanitizeUTF8(camera_ ? camera_->getId() : "unknown");
    std::string sourceId = sanitizeUTF8(event.getSourceId());
    
    sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, event.getTimestamp());
    sqlite3_bind_int(stmt, 3, static_cast<int>(event.getType()));
    sqlite3_bind_text(stmt, 4, sourceId.c_str(), -1, SQLITE_TRANSIENT);
    
    // Safe properties serialization with UTF-8 sanitization
    std::string propertiesJson;
    try {
        auto eventJson = event.toJson();
        nlohmann::json properties;
        
        if (eventJson.contains("properties") && eventJson["properties"].is_object()) {
            // Sanitize the properties JSON recursively
            properties = sanitizeJsonUTF8(eventJson["properties"]);
        } else {
            properties = nlohmann::json::object();
        }
        
        // Convert to string with proper UTF-8 handling
        propertiesJson = properties.dump();
        
        // Final validation - ensure the serialized JSON is valid UTF-8
        if (!isValidUTF8(propertiesJson)) {
            LOG_WARN("DatabaseSink", "Generated JSON contains invalid UTF-8, sanitizing");
            propertiesJson = sanitizeUTF8(propertiesJson);
        }
        
    } catch (const std::exception& e) {
        LOG_WARN("DatabaseSink", "Failed to serialize event properties safely, using empty object: " + std::string(e.what()));
        propertiesJson = "{}";
    }
    
    // Ensure we never store empty or null strings - use empty JSON object instead
    if (propertiesJson.empty()) {
        propertiesJson = "{}";
    }
    
    sqlite3_bind_text(stmt, 5, propertiesJson.c_str(), -1, SQLITE_TRANSIENT);
    
    if (frameId > 0) {
        sqlite3_bind_int64(stmt, 6, frameId);
    } else {
        sqlite3_bind_null(stmt, 6);
    }
    
    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE);
    
    if (!success) {
        LOG_ERROR("DatabaseSink", "Failed to insert event: " + std::string(sqlite3_errmsg(db_)));
    }
    
    sqlite3_finalize(stmt);

    // Update aggregate tables in real-time for high-performance analytics
    if (success) {
        updateAggregateTablesForEvent(event, cameraId);
    }
    
    return success;
}

int64_t DatabaseSink::insertFrame(const cv::Mat& frame) {
    if (!db_ || frame.empty()) {
        return -1;
    }
    
    std::string thumbnail = generateThumbnail(frame);
    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    const char* sql = "INSERT INTO frames (camera_id, timestamp, thumbnail, width, height) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("DatabaseSink", "Failed to prepare frame statement: " + std::string(sqlite3_errmsg(db_)));
        return -1;
    }
    
    std::string cameraId = camera_ ? camera_->getId() : "unknown";
    sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, timestamp);
    
    if (!thumbnail.empty()) {
        sqlite3_bind_blob(stmt, 3, thumbnail.data(), thumbnail.size(), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    
    sqlite3_bind_int(stmt, 4, thumbnailWidth_);
    sqlite3_bind_int(stmt, 5, thumbnailHeight_);
    
    rc = sqlite3_step(stmt);
    int64_t frameId = -1;
    if (rc == SQLITE_DONE) {
        frameId = sqlite3_last_insert_rowid(db_);
        insertedFrames_++;
    } else {
        LOG_ERROR("DatabaseSink", "Failed to insert frame: " + std::string(sqlite3_errmsg(db_)));
    }
    
    sqlite3_finalize(stmt);
    return frameId;
}

std::string DatabaseSink::generateThumbnail(const cv::Mat& frame) {
    if (frame.empty()) {
        return "";
    }
    
    try {
        cv::Mat thumbnail;
        cv::resize(frame, thumbnail, cv::Size(thumbnailWidth_, thumbnailHeight_));
        
        std::vector<uchar> buffer;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 85};
        
        if (!cv::imencode(".jpg", thumbnail, buffer, params)) {
            LOG_ERROR("DatabaseSink", "Failed to encode thumbnail as JPEG");
            return "";
        }
        
        return std::string(buffer.begin(), buffer.end());
        
    } catch (const std::exception& e) {
        LOG_ERROR("DatabaseSink", "Exception in generateThumbnail: " + std::string(e.what()));
        return "";
    }
}



bool DatabaseSink::deleteDataForCamera(const std::string& cameraId) {
    // Call the overloaded version with no progress callback
    return deleteDataForCamera(cameraId, nullptr);
}

bool DatabaseSink::deleteDataForCamera(const std::string& cameraId, std::function<void(double, std::string)> progressCallback) {
    LOG_INFO("DatabaseSink", "Deleting database file for camera: " + cameraId);
    
    if (progressCallback) progressCallback(10.0, "Starting database file deletion");
    
    // Construct the expected database path for this camera
    std::string targetDbPath = "./data/telemetry_" + cameraId + ".db";
    
    try {
        // Check if we're deleting the currently active database
        bool isCurrentDatabase = (camera_ && camera_->getId() == cameraId);
        
        if (isCurrentDatabase) {
            LOG_INFO("DatabaseSink", "Deleting current database, stopping operations first");
            if (progressCallback) progressCallback(30.0, "Stopping database operations");
            
            // Stop the sink to prevent further operations
            if (isRunning()) {
                stop();
            }
            
            // Close the database connection
            std::lock_guard<std::mutex> lock(dbMutex_);
            if (db_) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
            isInitialized_ = false;
            
            if (progressCallback) progressCallback(60.0, "Database connection closed");
        }
        
        // Check if the database file exists
        if (!std::filesystem::exists(targetDbPath)) {
            LOG_INFO("DatabaseSink", "Database file does not exist: " + targetDbPath);
            if (progressCallback) progressCallback(100.0, "Database file does not exist - nothing to delete");
            return true; // Not an error - already deleted or never existed
        }
        
        if (progressCallback) progressCallback(80.0, "Deleting database file");
        
        // Delete the database file
        std::error_code ec;
        bool deleted = std::filesystem::remove(targetDbPath, ec);
        
        if (deleted && !ec) {
            LOG_INFO("DatabaseSink", "Successfully deleted database file: " + targetDbPath);
            
            // Also try to delete WAL and SHM files if they exist
            std::string walPath = targetDbPath + "-wal";
            std::string shmPath = targetDbPath + "-shm";
            
            if (std::filesystem::exists(walPath)) {
                std::filesystem::remove(walPath, ec);
                if (!ec) {
                    LOG_INFO("DatabaseSink", "Deleted WAL file: " + walPath);
                }
            }
            
            if (std::filesystem::exists(shmPath)) {
                std::filesystem::remove(shmPath, ec);
                if (!ec) {
                    LOG_INFO("DatabaseSink", "Deleted SHM file: " + shmPath);
                }
            }
            
            if (progressCallback) progressCallback(100.0, "Database file deleted successfully");
            return true;
        } else {
            LOG_ERROR("DatabaseSink", "Failed to delete database file: " + targetDbPath + " - " + ec.message());
            if (progressCallback) progressCallback(100.0, "Failed to delete database file: " + ec.message());
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("DatabaseSink", "Exception during database file deletion: " + std::string(e.what()));
        if (progressCallback) progressCallback(100.0, "Exception during deletion: " + std::string(e.what()));
        return false;
    }
}

std::string DatabaseSink::getDatabasePath() const {
    return dbPath_;
}

nlohmann::json DatabaseSink::getAnalytics(const std::string& cameraId) const {
    if (!db_) {
        LOG_WARN("DatabaseSink", "Database not available for analytics");
        return nlohmann::json::object();
    }
    
    // Minimize mutex lock time - don't hold it during entire query
    sqlite3* dbConnection = nullptr;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        dbConnection = db_;
        if (!dbConnection) {
            return nlohmann::json::object();
        }
    }
    
    nlohmann::json result;
    
    try {
        // Set query timeout to prevent long-running queries
        sqlite3_exec(dbConnection, "PRAGMA busy_timeout=10000;", nullptr, nullptr, nullptr);
        
        // Get event counts by type from aggregate table (ultra-fast!)
        const char* eventTypeSql = R"(
            SELECT event_type, total_count 
            FROM event_type_counts 
            WHERE camera_id = ? 
            ORDER BY total_count DESC
        )";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(dbConnection, eventTypeSql, -1, &stmt, nullptr);
        
        nlohmann::json eventCounts = nlohmann::json::object();
        int totalEvents = 0;
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int eventType = sqlite3_column_int(stmt, 0);
                int count = sqlite3_column_int(stmt, 1);
                eventCounts[std::to_string(eventType)] = count;
                totalEvents += count;
            }
            sqlite3_finalize(stmt);
        }
        
        // Get class counts from aggregate table (ultra-fast!)
        const char* classSql = R"(
            SELECT class_name, SUM(total_count) as total_count
            FROM class_distribution 
            WHERE camera_id = ?
            GROUP BY class_name
            ORDER BY total_count DESC
            LIMIT 25
        )";
        
        rc = sqlite3_prepare_v2(dbConnection, classSql, -1, &stmt, nullptr);
        nlohmann::json classCounts = nlohmann::json::array();
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json classCount;
                const char* className = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                classCount["class_name"] = className ? className : "unknown";
                classCount["count"] = sqlite3_column_int(stmt, 1);
                classCounts.push_back(classCount);
            }
            sqlite3_finalize(stmt);
        }
        
        // Get time range and recent activity from analytics summary (ultra-fast!)
        const char* summarySql = R"(
            SELECT summary_key, summary_value 
            FROM analytics_summary 
            WHERE camera_id = ? AND summary_key IN ('time_range', 'recent_activity')
        )";
        
        rc = sqlite3_prepare_v2(dbConnection, summarySql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                
                if (key && value) {
                    try {
                        nlohmann::json valueJson = nlohmann::json::parse(value);
                        if (std::string(key) == "time_range") {
                            if (valueJson.contains("min_timestamp")) {
                                result["min_timestamp"] = valueJson["min_timestamp"];
                            }
                            if (valueJson.contains("max_timestamp")) {
                                result["max_timestamp"] = valueJson["max_timestamp"];
                            }
                        } else if (std::string(key) == "recent_activity") {
                            if (valueJson.contains("recent_events_24h")) {
                                result["recent_events_24h"] = valueJson["recent_events_24h"];
                            }
                        }
                    } catch (const std::exception& e) {
                        LOG_WARN("DatabaseSink", "Failed to parse analytics summary value: " + std::string(e.what()));
                    }
                }
            }
            sqlite3_finalize(stmt);
        }
        
        // Build result
        result["event_counts"] = eventCounts;
        result["class_counts"] = classCounts;
        result["total_events"] = totalEvents;
        result["success"] = true;
        
        LOG_INFO("DatabaseSink", "Fast analytics query completed for camera: " + cameraId);
        
    } catch (const std::exception& e) {
        LOG_ERROR("DatabaseSink", "Exception in getAnalytics: " + std::string(e.what()));
        result["success"] = false;
        result["error"] = e.what();
    }
    
    return result;
}

nlohmann::json DatabaseSink::getTimeSeriesData(const std::string& cameraId, int64_t start_time, int64_t end_time) const {
    if (!db_) {
        LOG_WARN("DatabaseSink", "Database not available for time series data");
        return nlohmann::json::array();
    }
    
    // Minimize mutex lock time
    sqlite3* dbConnection = nullptr;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        dbConnection = db_;
        if (!dbConnection) {
            return nlohmann::json::array();
        }
    }
    
    nlohmann::json result = nlohmann::json::array();
    
    try {
        // Set query timeout
        sqlite3_exec(dbConnection, "PRAGMA busy_timeout=10000;", nullptr, nullptr, nullptr);
        
        // Determine appropriate bucket size based on time range
        int64_t timeRange = end_time - start_time;
        int64_t bucketSize;
        
        if (start_time == 0 && end_time == 0) {
            // Default to recent data - use 5 minute buckets
            bucketSize = 300000; // 5 minutes
        } else if (timeRange <= 3600000) { // 1 hour
            bucketSize = 60000; // 1 minute
        } else if (timeRange <= 86400000) { // 24 hours
            bucketSize = 300000; // 5 minutes
        } else if (timeRange <= 604800000) { // 1 week
            bucketSize = 3600000; // 1 hour
        } else { // More than 1 week
            bucketSize = 86400000; // 1 day
        }
        
        LOG_INFO("DatabaseSink", "Fast time series query with bucket size: " + std::to_string(bucketSize));
        
        // Query pre-computed time series buckets (ultra-fast!)
        std::string timeSeriesSql = R"(
            SELECT bucket_timestamp, event_type, class_name, SUM(event_count) as total_count
            FROM time_series_buckets 
            WHERE camera_id = ? AND bucket_size = ?
        )";
        
        if (start_time > 0 && end_time > 0) {
            timeSeriesSql += " AND bucket_timestamp >= ? AND bucket_timestamp <= ?";
        }
        
        timeSeriesSql += R"(
            GROUP BY bucket_timestamp, event_type, class_name
            ORDER BY bucket_timestamp ASC
            LIMIT 5000
        )";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(dbConnection, timeSeriesSql.c_str(), -1, &stmt, nullptr);
        
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 2, bucketSize);
            
            if (start_time > 0 && end_time > 0) {
                sqlite3_bind_int64(stmt, 3, start_time);
                sqlite3_bind_int64(stmt, 4, end_time);
            }
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json dataPoint;
                dataPoint["timestamp"] = sqlite3_column_int64(stmt, 0);
                dataPoint["event_type"] = sqlite3_column_int(stmt, 1);
                dataPoint["count"] = sqlite3_column_int(stmt, 3);
                
                const char* className = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                if (className) {
                    dataPoint["class_name"] = sanitizeUTF8(className);
                } else {
                    dataPoint["class_name"] = nullptr;
                }
                
                result.push_back(dataPoint);
            }
            sqlite3_finalize(stmt);
            
            LOG_INFO("DatabaseSink", "Fast time series query returned " + std::to_string(result.size()) + " data points");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("DatabaseSink", "Exception in getTimeSeriesData: " + std::string(e.what()));
        result = nlohmann::json::array();
    }
    
    return result;
}

nlohmann::json DatabaseSink::getDwellTimeAnalytics(const std::string& cameraId, int64_t start_time, int64_t end_time) const {
    if (!db_) {
        LOG_WARN("DatabaseSink", "Database not available for dwell time analytics");
        return nlohmann::json::array();
    }
    
    // Minimize mutex lock time
    sqlite3* dbConnection = nullptr;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        dbConnection = db_;
        if (!dbConnection) {
            return nlohmann::json::array();
        }
    }
    
    nlohmann::json result = nlohmann::json::array();
    
    try {
        // Set query timeout
        sqlite3_exec(dbConnection, "PRAGMA busy_timeout=10000;", nullptr, nullptr, nullptr);
        
        LOG_INFO("DatabaseSink", "Fast dwell time query from pre-computed table");
        
        // Query pre-computed dwell times (ultra-fast!)
        std::string dwellTimeSql = R"(
            SELECT track_id, class_name, first_seen, last_seen, dwell_time_ms, detection_count
            FROM dwell_times 
            WHERE camera_id = ? AND dwell_time_ms > 1000
        )";
        
        if (start_time > 0 && end_time > 0) {
            dwellTimeSql += " AND first_seen >= ? AND last_seen <= ?";
        }
        
        dwellTimeSql += R"(
            ORDER BY dwell_time_ms DESC
            LIMIT 500
        )";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(dbConnection, dwellTimeSql.c_str(), -1, &stmt, nullptr);
        
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
            
            if (start_time > 0 && end_time > 0) {
                sqlite3_bind_int64(stmt, 2, start_time);
                sqlite3_bind_int64(stmt, 3, end_time);
            }
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json dwellRecord;
                
                const char* trackId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                dwellRecord["track_id"] = trackId ? sanitizeUTF8(trackId) : "unknown";
                
                const char* className = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                dwellRecord["class_name"] = className ? sanitizeUTF8(className) : "unknown";
                
                dwellRecord["first_seen"] = sqlite3_column_int64(stmt, 2);
                dwellRecord["last_seen"] = sqlite3_column_int64(stmt, 3);
                dwellRecord["dwell_time_ms"] = sqlite3_column_int64(stmt, 4);
                dwellRecord["dwell_time_seconds"] = sqlite3_column_int64(stmt, 4) / 1000.0;
                dwellRecord["detection_count"] = sqlite3_column_int(stmt, 5);
                
                result.push_back(dwellRecord);
            }
            sqlite3_finalize(stmt);
            
            LOG_INFO("DatabaseSink", "Fast dwell time query returned " + std::to_string(result.size()) + " records");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("DatabaseSink", "Exception in getDwellTimeAnalytics: " + std::string(e.what()));
        result = nlohmann::json::array();
    }
    
    return result;
}

nlohmann::json DatabaseSink::getHeatmapData(const std::string& cameraId) const {
    return nlohmann::json::array();
}

nlohmann::json DatabaseSink::getEventSummary(const std::string& cameraId) const {
    return nlohmann::json::object();
}

nlohmann::json DatabaseSink::getZoneLineCounts(const std::string& cameraId, int64_t start_time, int64_t end_time) const {
    return nlohmann::json::array();
}

nlohmann::json DatabaseSink::getClassBasedHeatmapData(const std::string& cameraId) const {
    return nlohmann::json::array();
}

DatabaseSink::BBoxAnchor DatabaseSink::stringToAnchor(const std::string& anchorStr) {
    return BBoxAnchor::CENTER;
}

std::pair<double, double> DatabaseSink::getAnchorPosition(const nlohmann::json& bbox, BBoxAnchor anchor) const {
    return {0.0, 0.0};
}

std::vector<uchar> DatabaseSink::generateHeatmapImage(const std::string& cameraId, const cv::Mat& backgroundImage, BBoxAnchor anchor, const std::vector<std::string>& classFilter, int quality) const {
    return std::vector<uchar>();
}

std::vector<std::string> DatabaseSink::getAvailableClasses(const std::string& cameraId) const {
    return std::vector<std::string>();
}

bool DatabaseSink::updateHeatmapMatrix(const std::string& cameraId, const nlohmann::json& properties) {
    return true;
}

bool DatabaseSink::checkSchemaVersion() {
    return true;
}

bool DatabaseSink::validateConfig(const nlohmann::json& config) {
    return true;
}

bool DatabaseSink::executeWithRetry(sqlite3_stmt* stmt, int maxRetries) {
    return sqlite3_step(stmt) == SQLITE_DONE;
}

bool DatabaseSink::createSchemaVersionTable() {
    return true;
}

int DatabaseSink::getCurrentSchemaVersion() {
    return 1;
}

bool DatabaseSink::setSchemaVersion(int version) {
    return true;
}

bool DatabaseSink::canAllocateMemory(size_t size) {
    return true;
}

void DatabaseSink::updateMemoryUsage(int64_t delta) {
    // Simplified - no memory tracking
}

bool DatabaseSink::insertTelemetryEvent(int64_t frameId, const TelemetryEvent& event) {
    std::vector<TelemetryEvent> events = {event};
    cv::Mat emptyFrame;
    return processTelemetry(emptyFrame, events);
}

bool DatabaseSink::createAggregateTables() {
    return true; // No aggregate tables in simplified version
}

bool DatabaseSink::processBatch(const TelemetryBatch& batch) {
    return processTelemetry(batch.frame, batch.events);
}

int64_t DatabaseSink::insertFrame(int64_t timestamp, const std::string& thumbnail) {
    // Not used in simplified version
    return -1;
}

bool DatabaseSink::updateAggregates(const std::vector<TelemetryEvent>& events, const std::string& cameraId) {
    return true; // No aggregates in simplified version
}

void DatabaseSink::cleanupOldData() {
    if (retentionDays_ <= 0 || !db_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(dbMutex_);
    
    int64_t cutoffTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 
        (retentionDays_ * 24 * 60 * 60 * 1000);
    
    LOG_INFO("DatabaseSink", "Cleaning up data older than " + std::to_string(retentionDays_) + " days");
    
    // Clean up raw telemetry events
    const char* deleteEventsSql = "DELETE FROM telemetry_events WHERE timestamp < ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, deleteEventsSql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, cutoffTime);
        sqlite3_step(stmt);
        int deletedEvents = sqlite3_changes(db_);
        sqlite3_finalize(stmt);
        
        if (deletedEvents > 0) {
            LOG_INFO("DatabaseSink", "Deleted " + std::to_string(deletedEvents) + " old events");
        }
    }
    
    // Clean up old frames
    const char* deleteFramesSql = "DELETE FROM frames WHERE timestamp < ?";
    rc = sqlite3_prepare_v2(db_, deleteFramesSql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, cutoffTime);
        sqlite3_step(stmt);
        int deletedFrames = sqlite3_changes(db_);
        sqlite3_finalize(stmt);
        
        if (deletedFrames > 0) {
            LOG_INFO("DatabaseSink", "Deleted " + std::to_string(deletedFrames) + " old frames");
        }
    }
    
    // Clean up aggregate tables
    cleanupAggregateData(cutoffTime);
    
    // Run database optimization after cleanup
    optimizeDatabase();
}

void DatabaseSink::cleanupAggregateData(int64_t cutoffTime) {
    LOG_INFO("DatabaseSink", "Cleaning up aggregate data older than cutoff time");
    
    // Clean up old time series buckets (keep more granular data for shorter periods)
    const char* deleteTimeSeriesSql = R"(
        DELETE FROM time_series_buckets 
        WHERE bucket_timestamp < ? 
        AND (
            (bucket_size <= 300000 AND bucket_timestamp < ?) OR  -- 5min buckets: keep 7 days
            (bucket_size <= 3600000 AND bucket_timestamp < ?) OR -- 1hr buckets: keep 30 days  
            (bucket_size > 3600000 AND bucket_timestamp < ?)     -- 1day buckets: keep full retention
        )
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, deleteTimeSeriesSql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        int64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        sqlite3_bind_int64(stmt, 1, cutoffTime);
        sqlite3_bind_int64(stmt, 2, currentTime - (7LL * 24 * 60 * 60 * 1000));   // 7 days for 5min buckets
        sqlite3_bind_int64(stmt, 3, currentTime - (30LL * 24 * 60 * 60 * 1000));  // 30 days for 1hr buckets
        sqlite3_bind_int64(stmt, 4, cutoffTime);                                 // Full retention for daily buckets
        
        sqlite3_step(stmt);
        int deletedTimeSeries = sqlite3_changes(db_);
        sqlite3_finalize(stmt);
        
        if (deletedTimeSeries > 0) {
            LOG_INFO("DatabaseSink", "Deleted " + std::to_string(deletedTimeSeries) + " old time series buckets");
        }
    }
    
    // Clean up old dwell times (beyond retention period)
    const char* deleteDwellTimesSql = "DELETE FROM dwell_times WHERE first_seen < ?";
    rc = sqlite3_prepare_v2(db_, deleteDwellTimesSql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, cutoffTime);
        sqlite3_step(stmt);
        int deletedDwellTimes = sqlite3_changes(db_);
        sqlite3_finalize(stmt);
        
        if (deletedDwellTimes > 0) {
            LOG_INFO("DatabaseSink", "Deleted " + std::to_string(deletedDwellTimes) + " old dwell time records");
        }
    }
    
    // Clean up old analytics summary entries (but keep recent summaries)
    const char* deleteAnalyticsSummarySql = "DELETE FROM analytics_summary WHERE last_updated < ?";
    rc = sqlite3_prepare_v2(db_, deleteAnalyticsSummarySql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, cutoffTime);
        sqlite3_step(stmt);
        int deletedSummaries = sqlite3_changes(db_);
        sqlite3_finalize(stmt);
        
        if (deletedSummaries > 0) {
            LOG_INFO("DatabaseSink", "Deleted " + std::to_string(deletedSummaries) + " old analytics summaries");
        }
    }
    
    // Reset 24h counts in event type counts table periodically
    resetRecent24hCounts();
}

void DatabaseSink::resetRecent24hCounts() {
    int64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    int64_t cutoff24h = currentTime - (24LL * 60 * 60 * 1000);
    
    const char* resetCountsSql = R"(
        UPDATE event_type_counts 
        SET recent_count_24h = 0, last_24h_updated = ?
        WHERE last_24h_updated < ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, resetCountsSql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, currentTime);
        sqlite3_bind_int64(stmt, 2, cutoff24h);
        sqlite3_step(stmt);
        int updatedCounts = sqlite3_changes(db_);
        sqlite3_finalize(stmt);
        
        if (updatedCounts > 0) {
            LOG_INFO("DatabaseSink", "Reset 24h counts for " + std::to_string(updatedCounts) + " event type entries");
        }
    }
}

void DatabaseSink::optimizeDatabase() {
    if (!db_) {
        return;
    }
    
    LOG_INFO("DatabaseSink", "Optimizing database");
    
    // Run VACUUM to reclaim space and reorganize data
    sqlite3_exec(db_, "VACUUM;", nullptr, nullptr, nullptr);
    
    // Analyze tables for query optimization
    sqlite3_exec(db_, "ANALYZE;", nullptr, nullptr, nullptr);
    
    // Optimize query planner
    sqlite3_exec(db_, "PRAGMA optimize;", nullptr, nullptr, nullptr);
    
    LOG_INFO("DatabaseSink", "Database optimization completed");
}

bool DatabaseSink::initializeReaderDB() {
    return true; // Not needed in simplified version
}

bool DatabaseSink::initializeWriterDB() {
    return true; // Not needed in simplified version
}

void DatabaseSink::writerThreadFunction() {
    // Not used in simplified version
}

nlohmann::json DatabaseSink::getDatabasePerformanceStats(const std::string& cameraId) const {
    if (!db_) {
        LOG_WARN("DatabaseSink", "Database not available for performance stats");
        return nlohmann::json::object();
    }
    
    // Minimize mutex lock time
    sqlite3* dbConnection = nullptr;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        dbConnection = db_;
        if (!dbConnection) {
            return nlohmann::json::object();
        }
    }
    
    nlohmann::json result;
    
    try {
        // Get database size and table statistics
        const char* dbStatsSql = R"(
            SELECT 
                name,
                (SELECT COUNT(*) FROM pragma_table_info(name)) as column_count,
                (SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND tbl_name=name) as index_count
            FROM sqlite_master 
            WHERE type='table' AND name IN ('telemetry_events', 'frames')
        )";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(dbConnection, dbStatsSql, -1, &stmt, nullptr);
        
        nlohmann::json tables = nlohmann::json::array();
        if (rc == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json table;
                const char* tableName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                table["name"] = tableName ? tableName : "";
                table["column_count"] = sqlite3_column_int(stmt, 1);
                table["index_count"] = sqlite3_column_int(stmt, 2);
                
                // Get row count for this table
                if (tableName) {
                    std::string countSql = "SELECT COUNT(*) FROM " + std::string(tableName);
                    if (std::string(tableName) == "telemetry_events") {
                        countSql += " WHERE camera_id = ?";
                    }
                    
                    sqlite3_stmt* countStmt;
                    int countRc = sqlite3_prepare_v2(dbConnection, countSql.c_str(), -1, &countStmt, nullptr);
                    if (countRc == SQLITE_OK) {
                        if (std::string(tableName) == "telemetry_events") {
                            sqlite3_bind_text(countStmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
                        }
                        if (sqlite3_step(countStmt) == SQLITE_ROW) {
                            table["row_count"] = sqlite3_column_int(countStmt, 0);
                        }
                        sqlite3_finalize(countStmt);
                    }
                }
                
                tables.push_back(table);
            }
            sqlite3_finalize(stmt);
        }
        
        // Get database file size information
        const char* pragmaPageCount = "PRAGMA page_count;";
        const char* pragmaPageSize = "PRAGMA page_size;";
        
        rc = sqlite3_prepare_v2(dbConnection, pragmaPageCount, -1, &stmt, nullptr);
        int pageCount = 0;
        if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            pageCount = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        
        rc = sqlite3_prepare_v2(dbConnection, pragmaPageSize, -1, &stmt, nullptr);
        int pageSize = 0;
        if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            pageSize = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        
        int64_t dbSizeBytes = static_cast<int64_t>(pageCount) * pageSize;
        
        // Get index statistics
        const char* indexStatsSql = R"(
            SELECT name, tbl_name, sql 
            FROM sqlite_master 
            WHERE type='index' AND tbl_name IN ('telemetry_events', 'frames')
            ORDER BY tbl_name, name
        )";
        
        rc = sqlite3_prepare_v2(dbConnection, indexStatsSql, -1, &stmt, nullptr);
        nlohmann::json indexes = nlohmann::json::array();
        if (rc == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json index;
                const char* indexName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                const char* tableName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                const char* sql = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                
                index["name"] = indexName ? indexName : "";
                index["table"] = tableName ? tableName : "";
                index["sql"] = sql ? sql : "";
                indexes.push_back(index);
            }
            sqlite3_finalize(stmt);
        }
        
        // Get recent query performance (if available)
        nlohmann::json queryStats = nlohmann::json::object();
        queryStats["note"] = "Enable query profiling with PRAGMA stats=ON for detailed query statistics";
        
        // Build result
        result["database_size_bytes"] = dbSizeBytes;
        result["database_size_mb"] = dbSizeBytes / (1024.0 * 1024.0);
        result["page_count"] = pageCount;
        result["page_size"] = pageSize;
        result["tables"] = tables;
        result["indexes"] = indexes;
        result["query_stats"] = queryStats;
        result["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        result["success"] = true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("DatabaseSink", "Exception in getDatabasePerformanceStats: " + std::string(e.what()));
        result["success"] = false;
        result["error"] = e.what();
    }
    
    return result;
}

nlohmann::json DatabaseSink::explainQuery(const std::string& query) const {
    if (!db_) {
        LOG_WARN("DatabaseSink", "Database not available for query explanation");
        return nlohmann::json::object();
    }
    
    // Minimize mutex lock time
    sqlite3* dbConnection = nullptr;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        dbConnection = db_;
        if (!dbConnection) {
            return nlohmann::json::object();
        }
    }
    
    nlohmann::json result;
    
    try {
        // Add EXPLAIN QUERY PLAN to the query
        std::string explainQuery = "EXPLAIN QUERY PLAN " + query;
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(dbConnection, explainQuery.c_str(), -1, &stmt, nullptr);
        
        nlohmann::json queryPlan = nlohmann::json::array();
        if (rc == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json planStep;
                planStep["id"] = sqlite3_column_int(stmt, 0);
                planStep["parent"] = sqlite3_column_int(stmt, 1);
                planStep["notused"] = sqlite3_column_int(stmt, 2);
                
                const char* detail = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                planStep["detail"] = detail ? detail : "";
                
                queryPlan.push_back(planStep);
            }
            sqlite3_finalize(stmt);
            
            result["query"] = query;
            result["query_plan"] = queryPlan;
            result["success"] = true;
        } else {
            result["query"] = query;
            result["error"] = "Failed to explain query: " + std::string(sqlite3_errmsg(dbConnection));
            result["success"] = false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("DatabaseSink", "Exception in explainQuery: " + std::string(e.what()));
        result["success"] = false;
        result["error"] = e.what();
    }
    
    return result;
}

void DatabaseSink::updateAggregateTablesForEvent(const TelemetryEvent& event, const std::string& cameraId) {
    if (!db_ || cameraId.empty()) {
        return;
    }
    
    try {
        int64_t timestamp = event.getTimestamp();
        int eventType = static_cast<int>(event.getType());
        
        // Extract class name and track ID from properties
        std::string className;
        std::string trackId;
        
        auto eventJson = event.toJson();
        if (eventJson.contains("properties") && eventJson["properties"].is_object()) {
            auto props = eventJson["properties"];
            if (props.contains("class_name") && props["class_name"].is_string()) {
                className = sanitizeUTF8(props["class_name"].get<std::string>());
            }
            if (props.contains("track_id")) {
                if (props["track_id"].is_string()) {
                    trackId = sanitizeUTF8(props["track_id"].get<std::string>());
                } else if (props["track_id"].is_number()) {
                    trackId = std::to_string(props["track_id"].get<int>());
                }
            }
        }
        
        // 1. Update event type counts
        updateEventTypeCounts(cameraId, eventType, timestamp);
        
        // 2. Update class distribution (if we have a class name)
        if (!className.empty()) {
            updateClassDistribution(cameraId, className, eventType);
        }
        
        // 3. Update time series buckets for multiple bucket sizes
        updateTimeSeriesBuckets(cameraId, timestamp, eventType, className);
        
        // 4. Update dwell times (for tracking/detection events with track_id)
        if ((eventType == 0 || eventType == 1) && !trackId.empty() && !className.empty()) {
            updateDwellTimes(cameraId, trackId, className, timestamp);
        }
        
        // 5. Update analytics summary (periodically)
        updateAnalyticsSummary(cameraId, timestamp);
        
    } catch (const std::exception& e) {
        LOG_WARN("DatabaseSink", "Failed to update aggregate tables: " + std::string(e.what()));
    }
}

void DatabaseSink::updateEventTypeCounts(const std::string& cameraId, int eventType, int64_t timestamp) {
    const char* sql = R"(
        INSERT INTO event_type_counts (camera_id, event_type, total_count, recent_count_24h, last_updated, last_24h_updated) 
        VALUES (?, ?, 1, 1, ?, ?)
        ON CONFLICT (camera_id, event_type) 
        DO UPDATE SET 
            total_count = total_count + 1,
            recent_count_24h = CASE 
                WHEN ? > (strftime('%s', 'now') - 86400) * 1000 
                THEN recent_count_24h + 1 
                ELSE recent_count_24h 
            END,
            last_updated = ?,
            last_24h_updated = CASE 
                WHEN ? > (strftime('%s', 'now') - 86400) * 1000 
                THEN ? 
                ELSE last_24h_updated 
            END
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        int64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            
        sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, eventType);
        sqlite3_bind_int64(stmt, 3, currentTime);
        sqlite3_bind_int64(stmt, 4, currentTime);
        sqlite3_bind_int64(stmt, 5, timestamp);
        sqlite3_bind_int64(stmt, 6, currentTime);
        sqlite3_bind_int64(stmt, 7, timestamp);
        sqlite3_bind_int64(stmt, 8, currentTime);
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void DatabaseSink::updateClassDistribution(const std::string& cameraId, const std::string& className, int eventType) {
    const char* sql = R"(
        INSERT INTO class_distribution (camera_id, class_name, event_type, total_count, last_updated) 
        VALUES (?, ?, ?, 1, strftime('%s', 'now') * 1000)
        ON CONFLICT (camera_id, class_name, event_type) 
        DO UPDATE SET 
            total_count = total_count + 1,
            last_updated = strftime('%s', 'now') * 1000
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, className.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, eventType);
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void DatabaseSink::updateTimeSeriesBuckets(const std::string& cameraId, int64_t timestamp, int eventType, const std::string& className) {
    // Update multiple bucket sizes for different zoom levels
    std::vector<int64_t> bucketSizes = {
        60000,      // 1 minute
        300000,     // 5 minutes  
        3600000,    // 1 hour
        86400000    // 1 day
    };
    
    for (int64_t bucketSize : bucketSizes) {
        int64_t bucketTimestamp = (timestamp / bucketSize) * bucketSize;
        
        const char* sql = R"(
            INSERT INTO time_series_buckets (camera_id, bucket_timestamp, bucket_size, event_type, class_name, event_count, last_updated) 
            VALUES (?, ?, ?, ?, ?, 1, strftime('%s', 'now') * 1000)
            ON CONFLICT (camera_id, bucket_timestamp, bucket_size, event_type, class_name) 
            DO UPDATE SET 
                event_count = event_count + 1,
                last_updated = strftime('%s', 'now') * 1000
        )";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 2, bucketTimestamp);
            sqlite3_bind_int64(stmt, 3, bucketSize);
            sqlite3_bind_int(stmt, 4, eventType);
            
            if (!className.empty()) {
                sqlite3_bind_text(stmt, 5, className.c_str(), -1, SQLITE_STATIC);
            } else {
                sqlite3_bind_null(stmt, 5);
            }
            
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void DatabaseSink::updateDwellTimes(const std::string& cameraId, const std::string& trackId, const std::string& className, int64_t timestamp) {
    const char* sql = R"(
        INSERT INTO dwell_times (camera_id, track_id, class_name, first_seen, last_seen, detection_count, last_updated) 
        VALUES (?, ?, ?, ?, ?, 1, strftime('%s', 'now') * 1000)
        ON CONFLICT (camera_id, track_id, class_name) 
        DO UPDATE SET 
            last_seen = MAX(last_seen, ?),
            first_seen = MIN(first_seen, ?),
            detection_count = detection_count + 1,
            last_updated = strftime('%s', 'now') * 1000
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, trackId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, className.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, timestamp);
        sqlite3_bind_int64(stmt, 5, timestamp);
        sqlite3_bind_int64(stmt, 6, timestamp);
        sqlite3_bind_int64(stmt, 7, timestamp);
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void DatabaseSink::updateAnalyticsSummary(const std::string& cameraId, int64_t timestamp) {
    // Update summary statistics periodically (not on every event to avoid overhead)
    static std::unordered_map<std::string, int64_t> lastUpdateTime;
    static std::mutex updateMutex;
    
    std::lock_guard<std::mutex> lock(updateMutex);
    
    int64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Only update summary every 30 seconds per camera to reduce overhead
    if (lastUpdateTime[cameraId] + 30000 > currentTime) {
        return;
    }
    
    lastUpdateTime[cameraId] = currentTime;
    
    // Update time range summary
    nlohmann::json timeRange;
    timeRange["min_timestamp"] = timestamp;
    timeRange["max_timestamp"] = timestamp;
    
    const char* timeRangeSql = R"(
        INSERT INTO analytics_summary (camera_id, summary_key, summary_value, last_updated) 
        VALUES (?, 'time_range', ?, ?)
        ON CONFLICT (camera_id, summary_key) 
        DO UPDATE SET 
            summary_value = json_patch(summary_value, ?),
            last_updated = ?
        WHERE json_extract(summary_value, '$.min_timestamp') IS NULL 
           OR json_extract(summary_value, '$.min_timestamp') > ? 
           OR json_extract(summary_value, '$.max_timestamp') < ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, timeRangeSql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        std::string timeRangeStr = timeRange.dump();
        sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, timeRangeStr.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, currentTime);
        sqlite3_bind_text(stmt, 4, timeRangeStr.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, currentTime);
        sqlite3_bind_int64(stmt, 6, timestamp);
        sqlite3_bind_int64(stmt, 7, timestamp);
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    // Update recent activity summary
    nlohmann::json recentActivity;
    
    // Get recent count from event type counts table
    const char* recentCountSql = R"(
        SELECT SUM(recent_count_24h) as total_recent 
        FROM event_type_counts 
        WHERE camera_id = ?
    )";
    
    rc = sqlite3_prepare_v2(db_, recentCountSql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            recentActivity["recent_events_24h"] = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    const char* recentActivitySql = R"(
        INSERT INTO analytics_summary (camera_id, summary_key, summary_value, last_updated) 
        VALUES (?, 'recent_activity', ?, ?)
        ON CONFLICT (camera_id, summary_key) 
        DO UPDATE SET 
            summary_value = ?,
            last_updated = ?
    )";
    
    rc = sqlite3_prepare_v2(db_, recentActivitySql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        std::string recentActivityStr = recentActivity.dump();
        sqlite3_bind_text(stmt, 1, cameraId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, recentActivityStr.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, currentTime);
        sqlite3_bind_text(stmt, 4, recentActivityStr.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, currentTime);
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

} // namespace tapi
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include "billing_client.h"
#include "database/postgres_connection.h"
#include "billing/repository.h"

namespace brinkbyte {
namespace billing {

// Usage event types
enum class UsageEventType {
    API_CALL,
    LLM_TOKENS,
    STORAGE_GB_DAYS,
    SMS_SENT,
    AGENT_EXECUTION,
    CLOUD_EXPORT_GB,
    WEBHOOK_CALL,
    EMAIL_SENT
};

// Usage event structure
struct UsageEvent {
    std::string tenant_id;
    std::string device_id;
    std::string camera_id;
    UsageEventType event_type;
    double quantity;
    std::string unit;
    std::string metadata_json;
    std::chrono::system_clock::time_point event_time;
};

/**
 * @brief Tracks usage metrics and reports to billing server
 * 
 * Features:
 * - Track various usage events (API calls, LLM tokens, storage, etc.)
 * - Batch events for efficient reporting (default: 100 events or 5 minutes)
 * - Store events in PostgreSQL for persistence
 * - Background thread for automatic syncing to billing server
 * - Retry logic with exponential backoff
 */
class UsageTracker {
public:
    /**
     * @brief Constructor
     * @param billing_client HTTP client for billing server communication
     * @param usage_repo Repository for usage event persistence
     * @param batch_size Number of events to batch before sending (default: 100)
     * @param batch_interval_seconds Time interval for batching (default: 300 = 5 minutes)
     */
    explicit UsageTracker(
        std::shared_ptr<BillingClient> billing_client,
        std::shared_ptr<UsageEventRepository> usage_repo,
        int batch_size = 100,
        int batch_interval_seconds = 300  // 5 minutes
    );
    
    ~UsageTracker();
    
    /**
     * @brief Start the background sync thread
     */
    void start();
    
    /**
     * @brief Stop the background sync thread
     */
    void stop();
    
    /**
     * @brief Track an API call
     * @param tenant_id Tenant identifier
     * @param endpoint API endpoint called
     */
    void trackAPICall(const std::string& tenant_id, const std::string& endpoint);
    
    /**
     * @brief Track LLM token usage
     * @param tenant_id Tenant identifier
     * @param camera_id Camera identifier
     * @param tokens Number of tokens used
     */
    void trackLLMTokens(const std::string& tenant_id, const std::string& camera_id, int tokens);
    
    /**
     * @brief Track storage usage
     * @param tenant_id Tenant identifier
     * @param gb_days Storage in GB-days
     */
    void trackStorage(const std::string& tenant_id, double gb_days);
    
    /**
     * @brief Track SMS sent
     * @param tenant_id Tenant identifier
     * @param camera_id Camera identifier
     * @param count Number of SMS sent
     */
    void trackSMS(const std::string& tenant_id, const std::string& camera_id, int count = 1);
    
    /**
     * @brief Track agent execution
     * @param tenant_id Tenant identifier
     * @param camera_id Camera identifier
     * @param agent_name Name of the agent
     */
    void trackAgentExecution(
        const std::string& tenant_id,
        const std::string& camera_id,
        const std::string& agent_name
    );
    
    /**
     * @brief Track cloud export
     * @param tenant_id Tenant identifier
     * @param camera_id Camera identifier
     * @param gb_exported GB exported to cloud
     */
    void trackCloudExport(const std::string& tenant_id, const std::string& camera_id, double gb_exported);
    
    /**
     * @brief Track webhook call
     * @param tenant_id Tenant identifier
     * @param camera_id Camera identifier
     */
    void trackWebhook(const std::string& tenant_id, const std::string& camera_id);
    
    /**
     * @brief Track email sent
     * @param tenant_id Tenant identifier
     * @param camera_id Camera identifier
     */
    void trackEmail(const std::string& tenant_id, const std::string& camera_id);
    
    /**
     * @brief Track a custom usage event
     * @param event Usage event to track
     */
    void trackEvent(const UsageEvent& event);
    
    /**
     * @brief Flush all pending events to billing server immediately
     * @return true if flush successful
     */
    bool flushEvents();
    
    /**
     * @brief Get the number of pending events
     * @return Number of events in queue
     */
    size_t getPendingEventCount() const;
    
    /**
     * @brief Get usage statistics for a tenant
     * @param tenant_id Tenant identifier
     * @param start_time Start of time range
     * @param end_time End of time range
     * @return Map of event type to total quantity
     */
    std::map<std::string, double> getUsageStats(
        const std::string& tenant_id,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time
    );
    
    /**
     * @brief Convert usage event type to string
     * @param type Event type enum
     * @return String representation
     */
    static std::string eventTypeToString(UsageEventType type);
    
    /**
     * @brief Convert string to usage event type
     * @param type_str String representation
     * @return Event type enum
     */
    static UsageEventType stringToEventType(const std::string& type_str);

private:
    std::shared_ptr<BillingClient> billing_client_;
    std::shared_ptr<UsageEventRepository> usage_repo_;
    
    int batch_size_;
    int batch_interval_seconds_;
    
    // Event queue
    std::queue<UsageEvent> event_queue_;
    mutable std::mutex queue_mutex_;
    
    // Background sync thread
    std::thread sync_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;
    
    // Sync state
    std::chrono::system_clock::time_point last_sync_time_;
    int consecutive_failures_;
    
    // Thread-safe queue operations
    void enqueueEvent(const UsageEvent& event);
    std::vector<UsageEvent> dequeueBatch(int max_count);
    
    // Sync logic
    void syncLoop();
    bool sendBatchToBillingServer(const std::vector<UsageEvent>& events);
    void storeBatchInDatabase(const std::vector<UsageEvent>& events);
    void loadUnsentEventsFromDatabase();
    
    // Helpers
    std::string getDeviceId() const;
    int getBackoffDelay() const;
};

} // namespace billing
} // namespace brinkbyte


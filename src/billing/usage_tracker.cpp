#include "billing/usage_tracker.h"
#include "logger.h"
#include <algorithm>
#include <cmath>

namespace brinkbyte {
namespace billing {

UsageTracker::UsageTracker(
    std::shared_ptr<BillingClient> billing_client,
    std::shared_ptr<UsageEventRepository> usage_repo,
    int batch_size,
    int batch_interval_seconds
)
    : billing_client_(billing_client),
      usage_repo_(usage_repo),
      batch_size_(batch_size),
      batch_interval_seconds_(batch_interval_seconds),
      running_(false),
      should_stop_(false),
      last_sync_time_(std::chrono::system_clock::now()),
      consecutive_failures_(0)
{
    LOG_INFO("UsageTracker", "Initialized with batch_size=" + std::to_string(batch_size) + 
             ", interval=" + std::to_string(batch_interval_seconds) + "s");
}

UsageTracker::~UsageTracker() {
    stop();
}

void UsageTracker::start() {
    if (running_.load()) {
        LOG_WARN("UsageTracker", "Already running");
        return;
    }
    
    should_stop_.store(false);
    running_.store(true);
    
    // Load any unsent events from database
    loadUnsentEventsFromDatabase();
    
    // Start background sync thread
    sync_thread_ = std::thread(&UsageTracker::syncLoop, this);
    
    LOG_INFO("UsageTracker", "Started background sync thread");
}

void UsageTracker::stop() {
    if (!running_.load()) {
        return;
    }
    
    LOG_INFO("UsageTracker", "Stopping usage tracker...");
    
    should_stop_.store(true);
    
    if (sync_thread_.joinable()) {
        sync_thread_.join();
    }
    
    running_.store(false);
    
    // Flush any remaining events
    flushEvents();
    
    LOG_INFO("UsageTracker", "Stopped");
}

void UsageTracker::trackAPICall(const std::string& tenant_id, const std::string& endpoint) {
    UsageEvent event;
    event.tenant_id = tenant_id;
    event.device_id = getDeviceId();
    event.camera_id = "";
    event.event_type = UsageEventType::API_CALL;
    event.quantity = 1.0;
    event.unit = "count";
    event.metadata_json = R"({"endpoint":")" + endpoint + R"("})";
    event.event_time = std::chrono::system_clock::now();
    
    enqueueEvent(event);
}

void UsageTracker::trackLLMTokens(const std::string& tenant_id, const std::string& camera_id, int tokens) {
    UsageEvent event;
    event.tenant_id = tenant_id;
    event.device_id = getDeviceId();
    event.camera_id = camera_id;
    event.event_type = UsageEventType::LLM_TOKENS;
    event.quantity = static_cast<double>(tokens);
    event.unit = "tokens";
    event.metadata_json = "{}";
    event.event_time = std::chrono::system_clock::now();
    
    enqueueEvent(event);
}

void UsageTracker::trackStorage(const std::string& tenant_id, double gb_days) {
    UsageEvent event;
    event.tenant_id = tenant_id;
    event.device_id = getDeviceId();
    event.camera_id = "";
    event.event_type = UsageEventType::STORAGE_GB_DAYS;
    event.quantity = gb_days;
    event.unit = "gb_days";
    event.metadata_json = "{}";
    event.event_time = std::chrono::system_clock::now();
    
    enqueueEvent(event);
}

void UsageTracker::trackSMS(const std::string& tenant_id, const std::string& camera_id, int count) {
    UsageEvent event;
    event.tenant_id = tenant_id;
    event.device_id = getDeviceId();
    event.camera_id = camera_id;
    event.event_type = UsageEventType::SMS_SENT;
    event.quantity = static_cast<double>(count);
    event.unit = "count";
    event.metadata_json = "{}";
    event.event_time = std::chrono::system_clock::now();
    
    enqueueEvent(event);
}

void UsageTracker::trackAgentExecution(
    const std::string& tenant_id,
    const std::string& camera_id,
    const std::string& agent_name
) {
    UsageEvent event;
    event.tenant_id = tenant_id;
    event.device_id = getDeviceId();
    event.camera_id = camera_id;
    event.event_type = UsageEventType::AGENT_EXECUTION;
    event.quantity = 1.0;
    event.unit = "count";
    event.metadata_json = R"({"agent":")" + agent_name + R"("})";
    event.event_time = std::chrono::system_clock::now();
    
    enqueueEvent(event);
}

void UsageTracker::trackCloudExport(const std::string& tenant_id, const std::string& camera_id, double gb_exported) {
    UsageEvent event;
    event.tenant_id = tenant_id;
    event.device_id = getDeviceId();
    event.camera_id = camera_id;
    event.event_type = UsageEventType::CLOUD_EXPORT_GB;
    event.quantity = gb_exported;
    event.unit = "gb";
    event.metadata_json = "{}";
    event.event_time = std::chrono::system_clock::now();
    
    enqueueEvent(event);
}

void UsageTracker::trackWebhook(const std::string& tenant_id, const std::string& camera_id) {
    UsageEvent event;
    event.tenant_id = tenant_id;
    event.device_id = getDeviceId();
    event.camera_id = camera_id;
    event.event_type = UsageEventType::WEBHOOK_CALL;
    event.quantity = 1.0;
    event.unit = "count";
    event.metadata_json = "{}";
    event.event_time = std::chrono::system_clock::now();
    
    enqueueEvent(event);
}

void UsageTracker::trackEmail(const std::string& tenant_id, const std::string& camera_id) {
    UsageEvent event;
    event.tenant_id = tenant_id;
    event.device_id = getDeviceId();
    event.camera_id = camera_id;
    event.event_type = UsageEventType::EMAIL_SENT;
    event.quantity = 1.0;
    event.unit = "count";
    event.metadata_json = "{}";
    event.event_time = std::chrono::system_clock::now();
    
    enqueueEvent(event);
}

void UsageTracker::trackEvent(const UsageEvent& event) {
    enqueueEvent(event);
}

bool UsageTracker::flushEvents() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (event_queue_.empty()) {
        return true;
    }
    
    LOG_INFO("UsageTracker", "Flushing " + std::to_string(event_queue_.size()) + " pending events");
    
    // Get all events from queue
    std::vector<UsageEvent> events;
    while (!event_queue_.empty()) {
        events.push_back(event_queue_.front());
        event_queue_.pop();
    }
    
    // Try to send to billing server
    bool success = sendBatchToBillingServer(events);
    
    if (!success) {
        // Re-queue events if send failed
        for (const auto& event : events) {
            event_queue_.push(event);
        }
        LOG_ERROR("UsageTracker", "Failed to flush events, re-queued");
        return false;
    }
    
    return true;
}

size_t UsageTracker::getPendingEventCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return event_queue_.size();
}

std::map<std::string, double> UsageTracker::getUsageStats(
    const std::string& tenant_id,
    std::chrono::system_clock::time_point start_time,
    std::chrono::system_clock::time_point end_time
) {
    time_t start_t = std::chrono::system_clock::to_time_t(start_time);
    time_t end_t = std::chrono::system_clock::to_time_t(end_time);
    
    return usage_repo_->sumByType(tenant_id, start_t, end_t);
}

// Static methods

std::string UsageTracker::eventTypeToString(UsageEventType type) {
    switch (type) {
        case UsageEventType::API_CALL: return "api_call";
        case UsageEventType::LLM_TOKENS: return "llm_tokens";
        case UsageEventType::STORAGE_GB_DAYS: return "storage_gb_days";
        case UsageEventType::SMS_SENT: return "sms_sent";
        case UsageEventType::AGENT_EXECUTION: return "agent_execution";
        case UsageEventType::CLOUD_EXPORT_GB: return "cloud_export_gb";
        case UsageEventType::WEBHOOK_CALL: return "webhook_call";
        case UsageEventType::EMAIL_SENT: return "email_sent";
        default: return "unknown";
    }
}

UsageTracker::UsageEventType UsageTracker::stringToEventType(const std::string& type_str) {
    if (type_str == "api_call") return UsageEventType::API_CALL;
    if (type_str == "llm_tokens") return UsageEventType::LLM_TOKENS;
    if (type_str == "storage_gb_days") return UsageEventType::STORAGE_GB_DAYS;
    if (type_str == "sms_sent") return UsageEventType::SMS_SENT;
    if (type_str == "agent_execution") return UsageEventType::AGENT_EXECUTION;
    if (type_str == "cloud_export_gb") return UsageEventType::CLOUD_EXPORT_GB;
    if (type_str == "webhook_call") return UsageEventType::WEBHOOK_CALL;
    if (type_str == "email_sent") return UsageEventType::EMAIL_SENT;
    return UsageEventType::API_CALL;  // Default
}

// Private methods

void UsageTracker::enqueueEvent(const UsageEvent& event) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    event_queue_.push(event);
    
    LOG_DEBUG("UsageTracker", "Enqueued " + eventTypeToString(event.event_type) + 
              " event for tenant: " + event.tenant_id + " (queue size: " + 
              std::to_string(event_queue_.size()) + ")");
}

std::vector<UsageEvent> UsageTracker::dequeueBatch(int max_count) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    std::vector<UsageEvent> batch;
    int count = std::min(max_count, static_cast<int>(event_queue_.size()));
    
    for (int i = 0; i < count; ++i) {
        batch.push_back(event_queue_.front());
        event_queue_.pop();
    }
    
    return batch;
}

void UsageTracker::syncLoop() {
    LOG_INFO("UsageTracker", "Sync loop started");
    
    while (!should_stop_.load()) {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_sync_time_);
        
        // Check if we should sync based on time or queue size
        bool should_sync = false;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            should_sync = (event_queue_.size() >= static_cast<size_t>(batch_size_)) ||
                         (elapsed.count() >= batch_interval_seconds_ && !event_queue_.empty());
        }
        
        if (should_sync) {
            // Dequeue a batch
            auto batch = dequeueBatch(batch_size_);
            
            if (!batch.empty()) {
                LOG_INFO("UsageTracker", "Syncing batch of " + std::to_string(batch.size()) + " events");
                
                // Store in database first (persistence)
                storeBatchInDatabase(batch);
                
                // Try to send to billing server
                bool success = sendBatchToBillingServer(batch);
                
                if (success) {
                    consecutive_failures_ = 0;
                    last_sync_time_ = now;
                } else {
                    consecutive_failures_++;
                    
                    // Re-queue events on failure
                    for (const auto& event : batch) {
                        enqueueEvent(event);
                    }
                    
                    LOG_ERROR("UsageTracker", "Sync failed (consecutive failures: " + 
                             std::to_string(consecutive_failures_) + ")");
                    
                    // Exponential backoff
                    int backoff_delay = getBackoffDelay();
                    LOG_INFO("UsageTracker", "Backing off for " + std::to_string(backoff_delay) + " seconds");
                    std::this_thread::sleep_for(std::chrono::seconds(backoff_delay));
                }
            }
        }
        
        // Sleep for a short interval
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    LOG_INFO("UsageTracker", "Sync loop stopped");
}

bool UsageTracker::sendBatchToBillingServer(const std::vector<UsageEvent>& events) {
    try {
        // Convert events to JSON array
        nlohmann::json events_json = nlohmann::json::array();
        
        for (const auto& event : events) {
            nlohmann::json event_json;
            event_json["tenant_id"] = event.tenant_id;
            event_json["device_id"] = event.device_id;
            event_json["camera_id"] = event.camera_id;
            event_json["event_type"] = eventTypeToString(event.event_type);
            event_json["quantity"] = event.quantity;
            event_json["unit"] = event.unit;
            
            // Parse metadata if it's valid JSON
            try {
                event_json["metadata"] = nlohmann::json::parse(event.metadata_json);
            } catch (...) {
                event_json["metadata"] = {};
            }
            
            events_json.push_back(event_json);
        }
        
        // Send to billing server
        nlohmann::json request;
        request["events"] = events_json;
        
        auto response = billing_client_->reportUsageBatch(request);
        
        if (response.contains("accepted_count")) {
            int accepted = response["accepted_count"];
            LOG_INFO("UsageTracker", "Billing server accepted " + std::to_string(accepted) + 
                     "/" + std::to_string(events.size()) + " events");
            return accepted == static_cast<int>(events.size());
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("UsageTracker", "Failed to send batch to billing server: " + std::string(e.what()));
        return false;
    }
}

void UsageTracker::storeBatchInDatabase(const std::vector<UsageEvent>& events) {
    std::vector<brinkbyte::billing::UsageEvent> db_events;
    
    for (const auto& event : events) {
        brinkbyte::billing::UsageEvent db_event;
        db_event.event_id = "";  // Will be generated by database
        db_event.tenant_id = event.tenant_id;
        db_event.device_id = event.device_id;
        db_event.camera_id = event.camera_id;
        db_event.event_type = eventTypeToString(event.event_type);
        db_event.quantity = event.quantity;
        db_event.unit = event.unit;
        db_event.metadata_json = event.metadata_json;
        db_event.event_time = std::chrono::system_clock::to_time_t(event.event_time);
        db_event.synced = false;
        
        db_events.push_back(db_event);
    }
    
    usage_repo_->saveBatch(db_events);
    LOG_DEBUG("UsageTracker", "Stored " + std::to_string(events.size()) + " events in database");
}

void UsageTracker::loadUnsentEventsFromDatabase() {
    auto unsent = usage_repo_->findUnsynced(1000);  // Load up to 1000 unsent events
    
    if (!unsent.empty()) {
        LOG_INFO("UsageTracker", "Loaded " + std::to_string(unsent.size()) + " unsent events from database");
        
        for (const auto& db_event : unsent) {
            UsageEvent event;
            event.tenant_id = db_event.tenant_id;
            event.device_id = db_event.device_id;
            event.camera_id = db_event.camera_id;
            event.event_type = stringToEventType(db_event.event_type);
            event.quantity = db_event.quantity;
            event.unit = db_event.unit;
            event.metadata_json = db_event.metadata_json;
            event.event_time = std::chrono::system_clock::from_time_t(db_event.event_time);
            
            enqueueEvent(event);
        }
    }
}

std::string UsageTracker::getDeviceId() const {
    // TODO: Get actual device ID from config
    return "device-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
}

int UsageTracker::getBackoffDelay() const {
    // Exponential backoff: 2^failures seconds, max 300 seconds (5 minutes)
    int delay = static_cast<int>(std::pow(2, std::min(consecutive_failures_, 8)));
    return std::min(delay, 300);
}

} // namespace billing
} // namespace brinkbyte


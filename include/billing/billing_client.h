#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <curl/curl.h>

namespace brinkbyte {
namespace billing {

// Forward declarations
class BillingConfig;

/**
 * License validation request/response
 */
struct LicenseValidationRequest {
    std::string camera_id;
    std::string tenant_id;
    std::string device_id;
};

struct LicenseValidationResponse {
    bool is_valid;
    std::string license_mode;
    std::vector<std::string> enabled_growth_packs;
    time_t valid_until;
    int cameras_allowed;
};

/**
 * Entitlement check request/response
 */
struct EntitlementCheckRequest {
    std::string tenant_id;
    std::string feature_category;
    std::string feature_name;
};

struct EntitlementCheckResponse {
    bool is_enabled;
    int quota_remaining;
    time_t valid_until;
};

/**
 * Usage event for reporting
 */
struct UsageEventReport {
    std::string tenant_id;
    std::string event_type;
    std::string resource_id;
    double quantity;
    std::string unit;
    time_t event_time;
    std::string metadata_json;
};

/**
 * Usage batch request/response
 */
struct UsageBatchRequest {
    std::vector<UsageEventReport> events;
};

struct UsageBatchResponse {
    int accepted_count;
    int rejected_count;
    std::vector<std::string> errors;
};

/**
 * Heartbeat request/response
 */
struct HeartbeatRequest {
    std::string device_id;
    std::string tenant_id;
    std::vector<std::string> active_camera_ids;
    std::string management_tier;
};

struct HeartbeatResponse {
    std::string status;
    int next_heartbeat_seconds;
};

/**
 * HTTP client for communicating with the billing service
 */
class BillingHttpClient {
public:
    explicit BillingHttpClient(std::shared_ptr<BillingConfig> config);
    ~BillingHttpClient();

    // Disable copy
    BillingHttpClient(const BillingHttpClient&) = delete;
    BillingHttpClient& operator=(const BillingHttpClient&) = delete;

    /**
     * Validate a camera license
     */
    std::optional<LicenseValidationResponse> validateLicense(const LicenseValidationRequest& request);

    /**
     * Check feature entitlement
     */
    std::optional<EntitlementCheckResponse> checkEntitlement(const EntitlementCheckRequest& request);

    /**
     * Report usage events in batch
     */
    std::optional<UsageBatchResponse> reportUsageBatch(const UsageBatchRequest& request);

    /**
     * Send device heartbeat
     */
    std::optional<HeartbeatResponse> sendHeartbeat(const HeartbeatRequest& request);

    /**
     * Check billing service health
     */
    bool checkHealth();

    /**
     * Get last error message
     */
    std::string getLastError() const { return last_error_; }

private:
    // HTTP methods
    std::optional<std::string> httpPost(const std::string& endpoint, const std::string& json_body);
    std::optional<std::string> httpGet(const std::string& endpoint);

    // CURL helpers
    CURL* createCurlHandle();
    void setupCurlHeaders(CURL* curl);
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

    // Response parsers
    std::optional<LicenseValidationResponse> parseLicenseResponse(const std::string& json);
    std::optional<EntitlementCheckResponse> parseEntitlementResponse(const std::string& json);
    std::optional<UsageBatchResponse> parseUsageBatchResponse(const std::string& json);
    std::optional<HeartbeatResponse> parseHeartbeatResponse(const std::string& json);

    // Configuration
    std::shared_ptr<BillingConfig> config_;
    
    // State
    std::string last_error_;
    
    // CURL initialization
    static bool curl_global_initialized_;
    static void ensureCurlInit();
};

/**
 * High-level billing client with retry logic
 */
class BillingClient {
public:
    BillingClient(std::shared_ptr<BillingConfig> config,
                  std::shared_ptr<BillingHttpClient> http_client);

    /**
     * Validate license with retry
     */
    std::optional<LicenseValidationResponse> validateLicense(
        const std::string& camera_id,
        const std::string& tenant_id,
        const std::string& device_id
    );

    /**
     * Check entitlement with retry
     */
    std::optional<EntitlementCheckResponse> checkEntitlement(
        const std::string& tenant_id,
        const std::string& feature_category,
        const std::string& feature_name
    );

    /**
     * Report usage batch with retry
     */
    bool reportUsageBatch(const std::vector<UsageEventReport>& events);

    /**
     * Send heartbeat with retry
     */
    bool sendHeartbeat(
        const std::string& device_id,
        const std::string& tenant_id,
        const std::vector<std::string>& active_camera_ids,
        const std::string& management_tier
    );

    /**
     * Check if billing service is available
     */
    bool isAvailable();

    /**
     * Get last error
     */
    std::string getLastError() const { return last_error_; }

private:
    // Retry logic
    template<typename T>
    std::optional<T> retryOperation(
        std::function<std::optional<T>()> operation,
        int max_retries
    );

    std::shared_ptr<BillingConfig> config_;
    std::shared_ptr<BillingHttpClient> http_client_;
    std::string last_error_;
};

} // namespace billing
} // namespace brinkbyte


#include "billing/billing_client.h"
#include "billing/billing_config.h"
#include <crow/logging.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <chrono>

namespace brinkbyte {
namespace billing {

using json = nlohmann::json;

// Static member initialization
bool BillingHttpClient::curl_global_initialized_ = false;

void BillingHttpClient::ensureCurlInit() {
    if (!curl_global_initialized_) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_global_initialized_ = true;
    }
}

// =============================================================================
// BillingHttpClient Implementation
// =============================================================================

BillingHttpClient::BillingHttpClient(std::shared_ptr<BillingConfig> config)
    : config_(config) {
    ensureCurlInit();
}

BillingHttpClient::~BillingHttpClient() {
    // Note: We don't call curl_global_cleanup() as it's per-process
}

size_t BillingHttpClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

CURL* BillingHttpClient::createCurlHandle() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        last_error_ = "Failed to initialize CURL";
        return nullptr;
    }

    // Set timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config_->billing_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000);
    
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    
    // Enable verbose for debugging (can be controlled by config)
    if (config_->debug_mode) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    return curl;
}

void BillingHttpClient::setupCurlHeaders(CURL* curl) {
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    std::string auth_header = "Authorization: Bearer " + config_->billing_api_key;
    headers = curl_slist_append(headers, auth_header.c_str());
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
}

std::optional<std::string> BillingHttpClient::httpPost(const std::string& endpoint, const std::string& json_body) {
    CURL* curl = createCurlHandle();
    if (!curl) {
        return std::nullopt;
    }

    std::string url = config_->billing_service_url + endpoint;
    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    
    setupCurlHeaders(curl);

    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        last_error_ = std::string("CURL error: ") + curl_easy_strerror(res);
        CROW_LOG_ERROR << "HTTP POST to " << url << " failed: " << last_error_;
        return std::nullopt;
    }

    if (http_code < 200 || http_code >= 300) {
        last_error_ = "HTTP error code: " + std::to_string(http_code);
        CROW_LOG_ERROR << "HTTP POST to " << url << " returned " << http_code;
        return std::nullopt;
    }

    return response_data;
}

std::optional<std::string> BillingHttpClient::httpGet(const std::string& endpoint) {
    CURL* curl = createCurlHandle();
    if (!curl) {
        return std::nullopt;
    }

    std::string url = config_->billing_service_url + endpoint;
    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    
    setupCurlHeaders(curl);

    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        last_error_ = std::string("CURL error: ") + curl_easy_strerror(res);
        CROW_LOG_ERROR << "HTTP GET to " << url << " failed: " << last_error_;
        return std::nullopt;
    }

    if (http_code < 200 || http_code >= 300) {
        last_error_ = "HTTP error code: " + std::to_string(http_code);
        CROW_LOG_ERROR << "HTTP GET to " << url << " returned " << http_code;
        return std::nullopt;
    }

    return response_data;
}

std::optional<LicenseValidationResponse> BillingHttpClient::validateLicense(const LicenseValidationRequest& request) {
    json req_json;
    req_json["camera_id"] = request.camera_id;
    req_json["tenant_id"] = request.tenant_id;
    req_json["device_id"] = request.device_id;

    auto response = httpPost("/licenses/validate", req_json.dump());
    if (!response) {
        return std::nullopt;
    }

    return parseLicenseResponse(*response);
}

std::optional<EntitlementCheckResponse> BillingHttpClient::checkEntitlement(const EntitlementCheckRequest& request) {
    json req_json;
    req_json["tenant_id"] = request.tenant_id;
    req_json["feature_category"] = request.feature_category;
    req_json["feature_name"] = request.feature_name;

    auto response = httpPost("/entitlements/check", req_json.dump());
    if (!response) {
        return std::nullopt;
    }

    return parseEntitlementResponse(*response);
}

std::optional<UsageBatchResponse> BillingHttpClient::reportUsageBatch(const UsageBatchRequest& request) {
    json req_json;
    json events_array = json::array();

    for (const auto& event : request.events) {
        json event_json;
        event_json["tenant_id"] = event.tenant_id;
        event_json["event_type"] = event.event_type;
        event_json["resource_id"] = event.resource_id;
        event_json["quantity"] = event.quantity;
        event_json["unit"] = event.unit;
        event_json["event_time"] = std::to_string(event.event_time);
        
        try {
            event_json["metadata"] = json::parse(event.metadata_json);
        } catch (...) {
            event_json["metadata"] = json::object();
        }
        
        events_array.push_back(event_json);
    }

    req_json["events"] = events_array;

    auto response = httpPost("/usage/batch", req_json.dump());
    if (!response) {
        return std::nullopt;
    }

    return parseUsageBatchResponse(*response);
}

std::optional<HeartbeatResponse> BillingHttpClient::sendHeartbeat(const HeartbeatRequest& request) {
    json req_json;
    req_json["device_id"] = request.device_id;
    req_json["tenant_id"] = request.tenant_id;
    req_json["active_camera_ids"] = request.active_camera_ids;
    req_json["management_tier"] = request.management_tier;

    auto response = httpPost("/heartbeat", req_json.dump());
    if (!response) {
        return std::nullopt;
    }

    return parseHeartbeatResponse(*response);
}

bool BillingHttpClient::checkHealth() {
    auto response = httpGet("/health");
    if (!response) {
        return false;
    }

    try {
        auto j = json::parse(*response);
        return j.contains("status") && j["status"] == "healthy";
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to parse health response: ") + e.what();
        return false;
    }
}

std::optional<LicenseValidationResponse> BillingHttpClient::parseLicenseResponse(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);
        
        LicenseValidationResponse response;
        response.is_valid = j.value("is_valid", false);
        response.license_mode = j.value("license_mode", "unlicensed");
        response.cameras_allowed = j.value("cameras_allowed", 0);
        
        if (j.contains("valid_until")) {
            std::string valid_until_str = j["valid_until"].get<std::string>();
            response.valid_until = std::stoll(valid_until_str);
        } else {
            response.valid_until = 0;
        }
        
        if (j.contains("enabled_growth_packs") && j["enabled_growth_packs"].is_array()) {
            for (const auto& pack : j["enabled_growth_packs"]) {
                response.enabled_growth_packs.push_back(pack.get<std::string>());
            }
        }

        return response;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to parse license response: ") + e.what();
        CROW_LOG_ERROR << last_error_;
        return std::nullopt;
    }
}

std::optional<EntitlementCheckResponse> BillingHttpClient::parseEntitlementResponse(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);
        
        EntitlementCheckResponse response;
        response.is_enabled = j.value("is_enabled", false);
        response.quota_remaining = j.value("quota_remaining", 0);
        
        if (j.contains("valid_until")) {
            std::string valid_until_str = j["valid_until"].get<std::string>();
            response.valid_until = std::stoll(valid_until_str);
        } else {
            response.valid_until = 0;
        }

        return response;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to parse entitlement response: ") + e.what();
        CROW_LOG_ERROR << last_error_;
        return std::nullopt;
    }
}

std::optional<UsageBatchResponse> BillingHttpClient::parseUsageBatchResponse(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);
        
        UsageBatchResponse response;
        response.accepted_count = j.value("accepted_count", 0);
        response.rejected_count = j.value("rejected_count", 0);
        
        if (j.contains("errors") && j["errors"].is_array()) {
            for (const auto& error : j["errors"]) {
                response.errors.push_back(error.get<std::string>());
            }
        }

        return response;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to parse usage batch response: ") + e.what();
        CROW_LOG_ERROR << last_error_;
        return std::nullopt;
    }
}

std::optional<HeartbeatResponse> BillingHttpClient::parseHeartbeatResponse(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);
        
        HeartbeatResponse response;
        response.status = j.value("status", "unknown");
        response.next_heartbeat_seconds = j.value("next_heartbeat_in_seconds", 900);

        return response;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to parse heartbeat response: ") + e.what();
        CROW_LOG_ERROR << last_error_;
        return std::nullopt;
    }
}

// =============================================================================
// BillingClient Implementation (High-level with retry logic)
// =============================================================================

BillingClient::BillingClient(std::shared_ptr<BillingConfig> config,
                             std::shared_ptr<BillingHttpClient> http_client)
    : config_(config), http_client_(http_client) {}

template<typename T>
std::optional<T> BillingClient::retryOperation(
    std::function<std::optional<T>()> operation,
    int max_retries) {
    
    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        if (attempt > 0) {
            // Exponential backoff: 100ms, 200ms, 400ms, etc.
            int delay_ms = 100 * (1 << (attempt - 1));
            CROW_LOG_DEBUG << "Retrying operation (attempt " << attempt + 1 << "), waiting " << delay_ms << "ms";
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        auto result = operation();
        if (result) {
            if (attempt > 0) {
                CROW_LOG_INFO << "Operation succeeded on retry attempt " << attempt + 1;
            }
            return result;
        }

        if (attempt < max_retries) {
            CROW_LOG_WARNING << "Operation failed, will retry (" << (max_retries - attempt) << " attempts remaining)";
        }
    }

    last_error_ = "Operation failed after " + std::to_string(max_retries + 1) + " attempts";
    CROW_LOG_ERROR << last_error_;
    return std::nullopt;
}

std::optional<LicenseValidationResponse> BillingClient::validateLicense(
    const std::string& camera_id,
    const std::string& tenant_id,
    const std::string& device_id) {
    
    LicenseValidationRequest request;
    request.camera_id = camera_id;
    request.tenant_id = tenant_id;
    request.device_id = device_id;

    auto operation = [this, &request]() -> std::optional<LicenseValidationResponse> {
        return http_client_->validateLicense(request);
    };

    return retryOperation<LicenseValidationResponse>(operation, config_->billing_max_retries);
}

std::optional<EntitlementCheckResponse> BillingClient::checkEntitlement(
    const std::string& tenant_id,
    const std::string& feature_category,
    const std::string& feature_name) {
    
    EntitlementCheckRequest request;
    request.tenant_id = tenant_id;
    request.feature_category = feature_category;
    request.feature_name = feature_name;

    auto operation = [this, &request]() -> std::optional<EntitlementCheckResponse> {
        return http_client_->checkEntitlement(request);
    };

    return retryOperation<EntitlementCheckResponse>(operation, config_->billing_max_retries);
}

bool BillingClient::reportUsageBatch(const std::vector<UsageEventReport>& events) {
    UsageBatchRequest request;
    request.events = events;

    auto operation = [this, &request]() -> std::optional<UsageBatchResponse> {
        return http_client_->reportUsageBatch(request);
    };

    auto response = retryOperation<UsageBatchResponse>(operation, config_->billing_max_retries);
    
    if (!response) {
        return false;
    }

    if (response->rejected_count > 0) {
        CROW_LOG_WARNING << "Usage batch had " << response->rejected_count << " rejected events";
        for (const auto& error : response->errors) {
            CROW_LOG_WARNING << "  - " << error;
        }
    }

    return response->accepted_count > 0;
}

bool BillingClient::sendHeartbeat(
    const std::string& device_id,
    const std::string& tenant_id,
    const std::vector<std::string>& active_camera_ids,
    const std::string& management_tier) {
    
    HeartbeatRequest request;
    request.device_id = device_id;
    request.tenant_id = tenant_id;
    request.active_camera_ids = active_camera_ids;
    request.management_tier = management_tier;

    auto operation = [this, &request]() -> std::optional<HeartbeatResponse> {
        return http_client_->sendHeartbeat(request);
    };

    auto response = retryOperation<HeartbeatResponse>(operation, config_->billing_max_retries);
    return response.has_value();
}

bool BillingClient::isAvailable() {
    return http_client_->checkHealth();
}

} // namespace billing
} // namespace brinkbyte


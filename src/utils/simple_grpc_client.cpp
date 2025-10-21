#include "utils/simple_grpc_client.h"
#include <iostream>
#include <curl/curl.h>

namespace tapi {

// CURL write callback
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    size_t totalSize = size * nmemb;
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

SimpleError SimpleGrpcClient::Create(
    std::unique_ptr<SimpleGrpcClient>* client,
    const std::string& server_url,
    bool verbose) {
    
    if (!client) {
        return SimpleError("Client pointer is null");
    }
    
    client->reset(new SimpleGrpcClient(server_url, verbose));
    return SimpleError(); // Success
}

SimpleGrpcClient::SimpleGrpcClient(const std::string& server_url, bool verbose)
    : server_url_(server_url), verbose_(verbose) {
    if (verbose_) {
        std::cout << "SimpleGrpcClient created for server: " << server_url_ << std::endl;
    }
}

SimpleError SimpleGrpcClient::IsServerLive(bool* live) {
    if (!live) {
        return SimpleError("Live pointer is null");
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        return SimpleError("Failed to initialize CURL");
    }
    
    std::string url = server_url_;
    if (url.back() != '/') {
        url += "/";
    }
    url += "v2/health/live";
    
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        *live = false;
        return SimpleError("CURL request failed: " + std::string(curl_easy_strerror(res)));
    }
    
    *live = (httpCode == 200);
    return SimpleError(); // Success
}

SimpleError SimpleGrpcClient::IsServerReady(bool* ready) {
    if (!ready) {
        return SimpleError("Ready pointer is null");
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        return SimpleError("Failed to initialize CURL");
    }
    
    std::string url = server_url_;
    if (url.back() != '/') {
        url += "/";
    }
    url += "v2/health/ready";
    
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        *ready = false;
        return SimpleError("CURL request failed: " + std::string(curl_easy_strerror(res)));
    }
    
    *ready = (httpCode == 200);
    return SimpleError(); // Success
}

SimpleError SimpleGrpcClient::Infer(
    const std::string& model_name,
    const std::vector<uint8_t>& input_data,
    const std::vector<int64_t>& input_shape,
    std::vector<uint8_t>& output_data) {
    
    // For now, this is a placeholder that returns an error
    // In a full implementation, this would make a gRPC call to Triton
    return SimpleError("gRPC inference not yet implemented - use HTTP client instead");
}

} // namespace tapi 
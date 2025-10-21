#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace tapi {

// Simple error class
class SimpleError {
public:
    SimpleError() = default;
    explicit SimpleError(const std::string& message) : message_(message) {}
    
    bool IsOk() const { return message_.empty(); }
    const std::string& Message() const { return message_; }
    
private:
    std::string message_;
};

// Simple gRPC client for basic inference
class SimpleGrpcClient {
public:
    static SimpleError Create(
        std::unique_ptr<SimpleGrpcClient>* client,
        const std::string& server_url,
        bool verbose = false);
    
    ~SimpleGrpcClient() = default;
    
    // Basic health check
    SimpleError IsServerLive(bool* live);
    SimpleError IsServerReady(bool* ready);
    
    // Simple inference method
    SimpleError Infer(
        const std::string& model_name,
        const std::vector<uint8_t>& input_data,
        const std::vector<int64_t>& input_shape,
        std::vector<uint8_t>& output_data);
    
private:
    explicit SimpleGrpcClient(const std::string& server_url, bool verbose);
    
    std::string server_url_;
    bool verbose_;
};

} // namespace tapi 
#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <functional>
#include <mutex>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

namespace tapi {
namespace database {

/**
 * @brief Redis cache manager for license and entitlement caching
 */
class RedisCache {
public:
    struct Config {
        std::string host = "localhost";
        int port = 6379;
        std::string password = "";
        int timeout_ms = 3000;
        int max_retries = 3;
        int max_memory_mb = 256;
    };
    
    RedisCache(const Config& config);
    ~RedisCache();
    
    // Disable copy
    RedisCache(const RedisCache&) = delete;
    RedisCache& operator=(const RedisCache&) = delete;
    
    // Connection management
    bool isConnected() const;
    bool reconnect();
    
    // Basic operations
    bool set(const std::string& key, const std::string& value);
    bool set(const std::string& key, const std::string& value, int ttl_seconds);
    bool get(const std::string& key, std::string& value);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    bool expire(const std::string& key, int ttl_seconds);
    int ttl(const std::string& key);  // Returns -1 if no expiry, -2 if key doesn't exist
    
    // JSON operations (serialize/deserialize automatically)
    bool setJson(const std::string& key, const nlohmann::json& value, int ttl_seconds = -1);
    bool getJson(const std::string& key, nlohmann::json& value);
    
    // Hash operations (for structured data)
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    bool hget(const std::string& key, const std::string& field, std::string& value);
    bool hdel(const std::string& key, const std::string& field);
    bool hexists(const std::string& key, const std::string& field);
    std::map<std::string, std::string> hgetall(const std::string& key);
    
    // List operations (for queues)
    bool lpush(const std::string& key, const std::string& value);
    bool rpush(const std::string& key, const std::string& value);
    bool lpop(const std::string& key, std::string& value);
    bool rpop(const std::string& key, std::string& value);
    int llen(const std::string& key);
    
    // Batch operations
    bool mset(const std::map<std::string, std::string>& keyvals);
    std::map<std::string, std::string> mget(const std::vector<std::string>& keys);
    
    // Pattern operations
    std::vector<std::string> keys(const std::string& pattern);
    int deletePattern(const std::string& pattern);  // Delete all keys matching pattern
    
    // Atomic increment/decrement
    long long incr(const std::string& key);
    long long incrby(const std::string& key, long long increment);
    long long decr(const std::string& key);
    long long decrby(const std::string& key, long long decrement);
    
    // Cache invalidation
    bool invalidate(const std::string& key);
    bool invalidatePattern(const std::string& pattern);
    bool flushAll();  // DANGEROUS: Clear entire cache
    
    // Health monitoring
    bool ping();
    std::string info();
    long long dbsize();
    
    // Helper: Generate cache keys
    static std::string makeLicenseKey(const std::string& camera_id);
    static std::string makeEntitlementKey(const std::string& tenant_id, const std::string& feature_category);
    static std::string makeUsageQuotaKey(const std::string& tenant_id, const std::string& quota_type);
    
private:
    Config config_;
    redisContext* context_;
    mutable std::mutex redis_mutex_;
    
    bool connect();
    void disconnect();
    void handleError(const std::string& operation);
    bool checkReply(redisReply* reply);
    void freeReply(redisReply* reply);
    
    // Retry wrapper
    template<typename Func>
    auto retryOperation(Func&& func) -> decltype(func());
};

/**
 * @brief Cache entry with TTL for in-memory caching alongside Redis
 */
template<typename T>
struct CacheEntry {
    T value;
    std::chrono::system_clock::time_point expiry;
    
    bool isExpired() const {
        return std::chrono::system_clock::now() > expiry;
    }
};

/**
 * @brief Two-level cache (memory + Redis) for ultimate performance
 */
template<typename T>
class TwoLevelCache {
public:
    TwoLevelCache(std::shared_ptr<RedisCache> redis_cache, int default_ttl_seconds = 300)
        : redis_cache_(redis_cache), default_ttl_seconds_(default_ttl_seconds) {}
    
    // Set value in both caches
    bool set(const std::string& key, const T& value, int ttl_seconds = -1) {
        int ttl = (ttl_seconds > 0) ? ttl_seconds : default_ttl_seconds_;
        
        // Set in memory cache
        {
            std::lock_guard<std::mutex> lock(memory_mutex_);
            memory_cache_[key] = CacheEntry<T>{
                value,
                std::chrono::system_clock::now() + std::chrono::seconds(ttl)
            };
        }
        
        // Set in Redis cache
        nlohmann::json j = value;
        return redis_cache_->setJson(key, j, ttl);
    }
    
    // Get value (memory first, then Redis)
    bool get(const std::string& key, T& value) {
        // Try memory cache first
        {
            std::lock_guard<std::mutex> lock(memory_mutex_);
            auto it = memory_cache_.find(key);
            if (it != memory_cache_.end()) {
                if (!it->second.isExpired()) {
                    value = it->second.value;
                    return true;
                } else {
                    memory_cache_.erase(it);  // Clean up expired entry
                }
            }
        }
        
        // Try Redis cache
        nlohmann::json j;
        if (redis_cache_->getJson(key, j)) {
            value = j.get<T>();
            
            // Populate memory cache
            int ttl = redis_cache_->ttl(key);
            if (ttl > 0) {
                std::lock_guard<std::mutex> lock(memory_mutex_);
                memory_cache_[key] = CacheEntry<T>{
                    value,
                    std::chrono::system_clock::now() + std::chrono::seconds(ttl)
                };
            }
            
            return true;
        }
        
        return false;
    }
    
    // Invalidate in both caches
    bool invalidate(const std::string& key) {
        {
            std::lock_guard<std::mutex> lock(memory_mutex_);
            memory_cache_.erase(key);
        }
        return redis_cache_->invalidate(key);
    }
    
    // Clear memory cache (Redis remains)
    void clearMemoryCache() {
        std::lock_guard<std::mutex> lock(memory_mutex_);
        memory_cache_.clear();
    }
    
private:
    std::shared_ptr<RedisCache> redis_cache_;
    int default_ttl_seconds_;
    std::map<std::string, CacheEntry<T>> memory_cache_;
    mutable std::mutex memory_mutex_;
};

} // namespace database
} // namespace tapi


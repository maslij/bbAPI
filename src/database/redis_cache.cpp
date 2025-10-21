#include "database/redis_cache.h"
#include "logger.h"
#include <thread>
#include <chrono>

namespace tapi {
namespace database {

// =====================================================
// RedisCache Implementation
// =====================================================

RedisCache::RedisCache(const Config& config)
    : config_(config), context_(nullptr) {
    connect();
}

RedisCache::~RedisCache() {
    disconnect();
}

bool RedisCache::connect() {
    std::lock_guard<std::mutex> lock(redis_mutex_);
    
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
    
    struct timeval timeout = { config_.timeout_ms / 1000, (config_.timeout_ms % 1000) * 1000 };
    context_ = redisConnectWithTimeout(config_.host.c_str(), config_.port, timeout);
    
    if (!context_ || context_->err) {
        handleError("connect");
        return false;
    }
    
    // Authenticate if password is set
    if (!config_.password.empty()) {
        redisReply* reply = (redisReply*)redisCommand(context_, "AUTH %s", config_.password.c_str());
        if (!checkReply(reply)) {
            freeReply(reply);
            return false;
        }
        freeReply(reply);
    }
    
    LOG_INFO("RedisCache", "Connected to Redis at " + config_.host + ":" + std::to_string(config_.port));
    return true;
}

void RedisCache::disconnect() {
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
}

bool RedisCache::isConnected() const {
    std::lock_guard<std::mutex> lock(redis_mutex_);
    return context_ && !context_->err;
}

bool RedisCache::reconnect() {
    LOG_INFO("RedisCache", "Attempting to reconnect...");
    return connect();
}

bool RedisCache::set(const std::string& key, const std::string& value) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "SET %s %s", key.c_str(), value.c_str());
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

bool RedisCache::set(const std::string& key, const std::string& value, int ttl_seconds) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "SETEX %s %d %s", 
                                                       key.c_str(), ttl_seconds, value.c_str());
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

bool RedisCache::get(const std::string& key, std::string& value) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());
        if (!reply || reply->type == REDIS_REPLY_NIL) {
            freeReply(reply);
            return false;
        }
        
        if (reply->type == REDIS_REPLY_STRING) {
            value = std::string(reply->str, reply->len);
            freeReply(reply);
            return true;
        }
        
        freeReply(reply);
        return false;
    });
}

bool RedisCache::del(const std::string& key) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "DEL %s", key.c_str());
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

bool RedisCache::exists(const std::string& key) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "EXISTS %s", key.c_str());
        bool exists = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
        freeReply(reply);
        return exists;
    });
}

bool RedisCache::expire(const std::string& key, int ttl_seconds) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "EXPIRE %s %d", key.c_str(), ttl_seconds);
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

int RedisCache::ttl(const std::string& key) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return -2;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "TTL %s", key.c_str());
        int ttl_value = -2;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            ttl_value = reply->integer;
        }
        freeReply(reply);
        return ttl_value;
    });
}

bool RedisCache::setJson(const std::string& key, const nlohmann::json& value, int ttl_seconds) {
    std::string json_str = value.dump();
    if (ttl_seconds > 0) {
        return set(key, json_str, ttl_seconds);
    } else {
        return set(key, json_str);
    }
}

bool RedisCache::getJson(const std::string& key, nlohmann::json& value) {
    std::string json_str;
    if (!get(key, json_str)) {
        return false;
    }
    
    try {
        value = nlohmann::json::parse(json_str);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("RedisCache", "Failed to parse JSON: " + std::string(e.what()));
        return false;
    }
}

bool RedisCache::hset(const std::string& key, const std::string& field, const std::string& value) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "HSET %s %s %s", 
                                                       key.c_str(), field.c_str(), value.c_str());
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

bool RedisCache::hget(const std::string& key, const std::string& field, std::string& value) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "HGET %s %s", key.c_str(), field.c_str());
        if (!reply || reply->type == REDIS_REPLY_NIL) {
            freeReply(reply);
            return false;
        }
        
        if (reply->type == REDIS_REPLY_STRING) {
            value = std::string(reply->str, reply->len);
            freeReply(reply);
            return true;
        }
        
        freeReply(reply);
        return false;
    });
}

bool RedisCache::hdel(const std::string& key, const std::string& field) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "HDEL %s %s", key.c_str(), field.c_str());
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

bool RedisCache::hexists(const std::string& key, const std::string& field) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "HEXISTS %s %s", key.c_str(), field.c_str());
        bool exists = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
        freeReply(reply);
        return exists;
    });
}

std::map<std::string, std::string> RedisCache::hgetall(const std::string& key) {
    std::map<std::string, std::string> result;
    
    retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "HGETALL %s", key.c_str());
        if (reply && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; i += 2) {
                if (i + 1 < reply->elements) {
                    std::string field(reply->element[i]->str, reply->element[i]->len);
                    std::string value(reply->element[i+1]->str, reply->element[i+1]->len);
                    result[field] = value;
                }
            }
        }
        freeReply(reply);
        return true;
    });
    
    return result;
}

bool RedisCache::lpush(const std::string& key, const std::string& value) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "LPUSH %s %s", key.c_str(), value.c_str());
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

bool RedisCache::rpush(const std::string& key, const std::string& value) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "RPUSH %s %s", key.c_str(), value.c_str());
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

bool RedisCache::lpop(const std::string& key, std::string& value) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "LPOP %s", key.c_str());
        if (!reply || reply->type == REDIS_REPLY_NIL) {
            freeReply(reply);
            return false;
        }
        
        if (reply->type == REDIS_REPLY_STRING) {
            value = std::string(reply->str, reply->len);
            freeReply(reply);
            return true;
        }
        
        freeReply(reply);
        return false;
    });
}

bool RedisCache::rpop(const std::string& key, std::string& value) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "RPOP %s", key.c_str());
        if (!reply || reply->type == REDIS_REPLY_NIL) {
            freeReply(reply);
            return false;
        }
        
        if (reply->type == REDIS_REPLY_STRING) {
            value = std::string(reply->str, reply->len);
            freeReply(reply);
            return true;
        }
        
        freeReply(reply);
        return false;
    });
}

int RedisCache::llen(const std::string& key) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return 0;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "LLEN %s", key.c_str());
        int length = 0;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            length = reply->integer;
        }
        freeReply(reply);
        return length;
    });
}

bool RedisCache::mset(const std::map<std::string, std::string>& keyvals) {
    if (keyvals.empty()) return true;
    
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        // Build command
        std::vector<const char*> argv;
        std::vector<size_t> argvlen;
        
        argv.push_back("MSET");
        argvlen.push_back(4);
        
        for (const auto& pair : keyvals) {
            argv.push_back(pair.first.c_str());
            argvlen.push_back(pair.first.length());
            argv.push_back(pair.second.c_str());
            argvlen.push_back(pair.second.length());
        }
        
        redisReply* reply = (redisReply*)redisCommandArgv(context_, argv.size(), argv.data(), argvlen.data());
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

std::map<std::string, std::string> RedisCache::mget(const std::vector<std::string>& keys) {
    std::map<std::string, std::string> result;
    if (keys.empty()) return result;
    
    retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        // Build command
        std::vector<const char*> argv;
        std::vector<size_t> argvlen;
        
        argv.push_back("MGET");
        argvlen.push_back(4);
        
        for (const auto& key : keys) {
            argv.push_back(key.c_str());
            argvlen.push_back(key.length());
        }
        
        redisReply* reply = (redisReply*)redisCommandArgv(context_, argv.size(), argv.data(), argvlen.data());
        if (reply && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements && i < keys.size(); ++i) {
                if (reply->element[i]->type == REDIS_REPLY_STRING) {
                    result[keys[i]] = std::string(reply->element[i]->str, reply->element[i]->len);
                }
            }
        }
        freeReply(reply);
        return true;
    });
    
    return result;
}

std::vector<std::string> RedisCache::keys(const std::string& pattern) {
    std::vector<std::string> result;
    
    retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "KEYS %s", pattern.c_str());
        if (reply && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                if (reply->element[i]->type == REDIS_REPLY_STRING) {
                    result.push_back(std::string(reply->element[i]->str, reply->element[i]->len));
                }
            }
        }
        freeReply(reply);
        return true;
    });
    
    return result;
}

int RedisCache::deletePattern(const std::string& pattern) {
    auto keys_to_delete = keys(pattern);
    int deleted = 0;
    
    for (const auto& key : keys_to_delete) {
        if (del(key)) {
            deleted++;
        }
    }
    
    return deleted;
}

long long RedisCache::incr(const std::string& key) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return 0LL;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "INCR %s", key.c_str());
        long long value = 0;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            value = reply->integer;
        }
        freeReply(reply);
        return value;
    });
}

long long RedisCache::incrby(const std::string& key, long long increment) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return 0LL;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "INCRBY %s %lld", key.c_str(), increment);
        long long value = 0;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            value = reply->integer;
        }
        freeReply(reply);
        return value;
    });
}

long long RedisCache::decr(const std::string& key) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return 0LL;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "DECR %s", key.c_str());
        long long value = 0;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            value = reply->integer;
        }
        freeReply(reply);
        return value;
    });
}

long long RedisCache::decrby(const std::string& key, long long decrement) {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return 0LL;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "DECRBY %s %lld", key.c_str(), decrement);
        long long value = 0;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            value = reply->integer;
        }
        freeReply(reply);
        return value;
    });
}

bool RedisCache::invalidate(const std::string& key) {
    return del(key);
}

bool RedisCache::invalidatePattern(const std::string& pattern) {
    return deletePattern(pattern) >= 0;
}

bool RedisCache::flushAll() {
    LOG_WARN("RedisCache", "FLUSHALL called - clearing entire Redis cache!");
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "FLUSHALL");
        bool success = checkReply(reply);
        freeReply(reply);
        return success;
    });
}

bool RedisCache::ping() {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "PING");
        bool success = reply && reply->type == REDIS_REPLY_STATUS;
        freeReply(reply);
        return success;
    });
}

std::string RedisCache::info() {
    std::string info_str;
    
    retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return false;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "INFO");
        if (reply && reply->type == REDIS_REPLY_STRING) {
            info_str = std::string(reply->str, reply->len);
        }
        freeReply(reply);
        return true;
    });
    
    return info_str;
}

long long RedisCache::dbsize() {
    return retryOperation([&]() {
        std::lock_guard<std::mutex> lock(redis_mutex_);
        if (!context_) return 0LL;
        
        redisReply* reply = (redisReply*)redisCommand(context_, "DBSIZE");
        long long size = 0;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            size = reply->integer;
        }
        freeReply(reply);
        return size;
    });
}

// Static helper methods
std::string RedisCache::makeLicenseKey(const std::string& camera_id) {
    return "license:" + camera_id;
}

std::string RedisCache::makeEntitlementKey(const std::string& tenant_id, const std::string& feature_category) {
    return "entitlement:" + tenant_id + ":" + feature_category;
}

std::string RedisCache::makeUsageQuotaKey(const std::string& tenant_id, const std::string& quota_type) {
    return "quota:" + tenant_id + ":" + quota_type;
}

// Private helper methods
void RedisCache::handleError(const std::string& operation) {
    if (context_) {
        LOG_ERROR("RedisCache", operation + " failed: " + std::string(context_->errstr));
    } else {
        LOG_ERROR("RedisCache", operation + " failed: unable to allocate context");
    }
}

bool RedisCache::checkReply(redisReply* reply) {
    if (!reply) {
        LOG_ERROR("RedisCache", "NULL reply from Redis");
        return false;
    }
    
    if (reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("RedisCache", "Redis error: " + std::string(reply->str, reply->len));
        return false;
    }
    
    return true;
}

void RedisCache::freeReply(redisReply* reply) {
    if (reply) {
        ::freeReplyObject(reply);
    }
}

template<typename Func>
auto RedisCache::retryOperation(Func&& func) -> decltype(func()) {
    for (int attempt = 0; attempt < config_.max_retries; ++attempt) {
        try {
            auto result = func();
            if (result || attempt == config_.max_retries - 1) {
                return result;
            }
        } catch (const std::exception& e) {
            LOG_WARN("RedisCache", "Operation failed (attempt " + std::to_string(attempt + 1) + "): " + e.what());
        }
        
        // Exponential backoff
        int delay_ms = 100 * (1 << attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        
        // Try to reconnect
        if (!isConnected()) {
            reconnect();
        }
    }
    
    return decltype(func())();
}

} // namespace database
} // namespace tapi


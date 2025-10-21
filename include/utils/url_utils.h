#pragma once

#include <string>
#include <cstddef> // for size_t

namespace tapi {
namespace utils {

/**
 * @brief Get server URL from environment variable or config manager
 * 
 * Checks for AI_SERVER_URL environment variable first,
 * then falls back to ConfigManager's "ai_server_url" setting,
 * and finally uses a default if neither is available.
 * 
 * @return std::string Server URL
 */
std::string getServerUrlFromEnvOrConfig();

/**
 * @brief CURL callback for writing response data
 * 
 * @param contents Response data
 * @param size Size of each item
 * @param nmemb Number of items
 * @param response Output string to store response
 * @return size_t Total bytes processed
 */
size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* response);

} // namespace utils
} // namespace tapi 
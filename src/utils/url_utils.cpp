#include "utils/url_utils.h"
#include "config_manager.h"
#include "global_config.h"
#include <cstdlib> // For getenv
#include <iostream>

namespace tapi {
namespace utils {

std::string getServerUrlFromEnvOrConfig() {
    // Now we just use GlobalConfig to get the server URL
    // It already handles environment variables and fallbacks
    return GlobalConfig::getInstance().getAiServerUrl();
}

// CURL callback for writing response data
size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    size_t totalSize = size * nmemb;
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

} // namespace utils
} // namespace tapi 
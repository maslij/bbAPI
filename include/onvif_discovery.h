#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace tapi {

/**
 * @brief Structure to hold information about an ONVIF camera
 */
struct OnvifCamera {
    std::string name;               ///< Camera name
    std::string ipAddress;          ///< IP address
    std::string hardware;           ///< Hardware information
    std::string endpointReference;  ///< Endpoint reference
    std::string types;              ///< Device types
    std::string xaddrs;             ///< Service endpoints
    std::vector<std::string> rtspUrls; ///< Possible RTSP URLs
};

/**
 * @brief Class for discovering ONVIF cameras on the network
 */
class OnvifDiscovery {
public:
    /**
     * @brief Constructor
     */
    OnvifDiscovery();
    
    /**
     * @brief Discover ONVIF cameras on the network
     * 
     * @param timeoutSeconds Timeout in seconds for the discovery process
     * @param networkInterface Network interface to use for discovery (optional)
     * @return Vector of discovered cameras
     */
    std::vector<OnvifCamera> discoverCameras(int timeoutSeconds = 5, const std::string& networkInterface = "");
    
    /**
     * @brief Convert cameras to JSON format
     * 
     * @param cameras Vector of cameras to convert
     * @return JSON representation of the cameras
     */
    nlohmann::json camerasToJson(const std::vector<OnvifCamera>& cameras);
    
private:
    /**
     * @brief Parse ONVIF discovery response
     * 
     * @param response XML response from camera
     * @param ipAddress IP address of the camera
     * @return Parsed camera information
     */
    OnvifCamera parseDiscoveryResponse(const std::string& response, const std::string& ipAddress);
    
    /**
     * @brief Try to get RTSP URLs for a camera
     * 
     * @param camera Camera to get RTSP URLs for
     */
    void getRtspUrlsForCamera(OnvifCamera& camera);
};

} // namespace tapi 
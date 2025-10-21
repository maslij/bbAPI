#include "../include/onvif_discovery.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <regex>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>

namespace tapi {

const std::string ONVIF_DISCOVERY_MESSAGE = 
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
    "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
    "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
    "<e:Header>"
    "<w:MessageID>uuid:84ede3de-7dec-11d0-c360-F01234567890</w:MessageID>"
    "<w:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
    "<w:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
    "</e:Header>"
    "<e:Body>"
    "<d:Probe>"
    "<d:Types>dn:NetworkVideoTransmitter</d:Types>"
    "</d:Probe>"
    "</e:Body>"
    "</e:Envelope>";

OnvifDiscovery::OnvifDiscovery() {}

std::vector<OnvifCamera> OnvifDiscovery::discoverCameras(int timeoutSeconds, const std::string& networkInterface) {
    const int discoveryPort = 3702; // Standard WS-Discovery port
    std::vector<OnvifCamera> discoveredCameras;
    
    try {
        std::cout << "Starting ONVIF discovery with timeout: " << timeoutSeconds << " seconds" << std::endl;
        if (!networkInterface.empty()) {
            std::cout << "Using network interface: " << networkInterface << std::endl;
        }
        
        // Get a list of network interfaces for debugging
        std::cout << "Available network interfaces:" << std::endl;
        std::vector<std::string> networkInterfaces;
        struct ifaddrs *ifaddr, *ifa;
        
        if (getifaddrs(&ifaddr) == -1) {
            std::cerr << "Failed to get network interfaces" << std::endl;
        } else {
            for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == nullptr) {
                    continue;
                }
                
                // Only consider IPv4 interfaces
                if (ifa->ifa_addr->sa_family == AF_INET) {
                    char host[NI_MAXHOST];
                    int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                                        host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
                    if (s == 0) {
                        std::cout << "  - " << ifa->ifa_name << ": " << host << std::endl;
                        networkInterfaces.push_back(std::string(ifa->ifa_name) + " (" + host + ")");
                    }
                }
            }
            freeifaddrs(ifaddr);
        }
        
        // Create a raw socket for improved multicast handling
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
            return discoveredCameras;
        }
        
        // Allow socket reuse
        int reuse = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            std::cerr << "Error setting SO_REUSEADDR: " << strerror(errno) << std::endl;
        }

        // Set receive timeout
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            std::cerr << "Error setting SO_RCVTIMEO: " << strerror(errno) << std::endl;
        }
        
        // Enable receiving messages from all interfaces
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(discoveryPort);
        
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Error binding socket: " << strerror(errno) << std::endl;
            close(sock);
            return discoveredCameras;
        }
        
        std::cout << "Socket bound successfully" << std::endl;
        
        // Join multicast group (239.255.255.250) on all interfaces
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        
        if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            std::cerr << "Error joining multicast group: " << strerror(errno) << std::endl;
        } else {
            std::cout << "Joined multicast group 239.255.255.250" << std::endl;
        }
        
        // Send discovery messages to multicast address
        struct sockaddr_in multicastAddr;
        memset(&multicastAddr, 0, sizeof(multicastAddr));
        multicastAddr.sin_family = AF_INET;
        multicastAddr.sin_addr.s_addr = inet_addr("239.255.255.250");
        multicastAddr.sin_port = htons(discoveryPort);
        
        // Try sending through all interfaces if not specified
        if (networkInterface.empty()) {
            for (const auto& iface : networkInterfaces) {
                size_t pos = iface.find("(");
                size_t endPos = iface.find(")");
                if (pos != std::string::npos && endPos != std::string::npos) {
                    std::string ifaceName = iface.substr(0, pos - 1);
                    std::string ipAddress = iface.substr(pos + 1, endPos - pos - 1);
                    
                    // Skip loopback addresses
                    if (ipAddress.substr(0, 3) == "127") {
                        continue;
                    }
                    
                    // Set outgoing interface
                    struct in_addr localInterface;
                    localInterface.s_addr = inet_addr(ipAddress.c_str());
                    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &localInterface, sizeof(localInterface)) < 0) {
                        std::cerr << "Error setting outgoing interface " << ipAddress << ": " << strerror(errno) << std::endl;
                        continue;
                    }
                    
                    std::cout << "Sending discovery message through interface " << ifaceName << " (" << ipAddress << ")" << std::endl;
                    if (sendto(sock, ONVIF_DISCOVERY_MESSAGE.c_str(), ONVIF_DISCOVERY_MESSAGE.length(), 0,
                               (struct sockaddr*)&multicastAddr, sizeof(multicastAddr)) < 0) {
                        std::cerr << "Error sending discovery message: " << strerror(errno) << std::endl;
                    }
                }
            }
        } else {
            // Send through specified interface
            struct in_addr localInterface;
            localInterface.s_addr = inet_addr(networkInterface.c_str());
            if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &localInterface, sizeof(localInterface)) < 0) {
                std::cerr << "Error setting outgoing interface " << networkInterface << ": " << strerror(errno) << std::endl;
            } else {
                std::cout << "Sending discovery message through interface " << networkInterface << std::endl;
                if (sendto(sock, ONVIF_DISCOVERY_MESSAGE.c_str(), ONVIF_DISCOVERY_MESSAGE.length(), 0,
                           (struct sockaddr*)&multicastAddr, sizeof(multicastAddr)) < 0) {
                    std::cerr << "Error sending discovery message: " << strerror(errno) << std::endl;
                }
            }
        }
        
        // Also try direct broadcast to local network
        struct sockaddr_in broadcastAddr;
        memset(&broadcastAddr, 0, sizeof(broadcastAddr));
        broadcastAddr.sin_family = AF_INET;
        broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        broadcastAddr.sin_port = htons(discoveryPort);
        
        // Enable broadcasting
        int broadcast = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
            std::cerr << "Error setting SO_BROADCAST: " << strerror(errno) << std::endl;
        } else {
            std::cout << "Sending broadcast discovery message" << std::endl;
            if (sendto(sock, ONVIF_DISCOVERY_MESSAGE.c_str(), ONVIF_DISCOVERY_MESSAGE.length(), 0,
                       (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr)) < 0) {
                std::cerr << "Error sending broadcast message: " << strerror(errno) << std::endl;
            }
        }
        
        // Receive responses until timeout
        auto startTime = std::chrono::steady_clock::now();
        std::chrono::seconds timeout(timeoutSeconds);
        
        char buffer[10240]; // 10KB buffer for responses
        
        std::cout << "Waiting for responses for " << timeoutSeconds << " seconds..." << std::endl;
        
        while (std::chrono::steady_clock::now() - startTime < timeout) {
            struct sockaddr_in senderAddr;
            socklen_t senderAddrLen = sizeof(senderAddr);
            
            int bytesReceived = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                                         (struct sockaddr*)&senderAddr, &senderAddrLen);
            
            if (bytesReceived > 0) {
                // Null-terminate the received data
                buffer[bytesReceived] = '\0';
                
                // Get sender's IP address
                char senderIP[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(senderAddr.sin_addr), senderIP, INET_ADDRSTRLEN);
                
                std::cout << "Received " << bytesReceived << " bytes from " << senderIP << std::endl;
                
                    // Process the response
                    std::string response(buffer, bytesReceived);
                
                // Debug: Print a portion of the response
                std::cout << "Response preview: " << response.substr(0, 100) << "..." << std::endl;
                    
                    // Parse the response to extract device information
                OnvifCamera camera = parseDiscoveryResponse(response, senderIP);
                    
                    // Only add if we got valid data
                    if (!camera.xaddrs.empty()) {
                    std::cout << "Found camera: " << camera.name 
                              << " at " << camera.ipAddress 
                              << " with xaddrs: " << camera.xaddrs << std::endl;
                    
                        // Check if we already have this camera
                        auto it = std::find_if(discoveredCameras.begin(), discoveredCameras.end(),
                            [&camera](const OnvifCamera& c) { 
                                return c.xaddrs == camera.xaddrs; 
                            });
                        
                        // If this is a new camera, add it
                        if (it == discoveredCameras.end()) {
                        std::cout << "Adding new camera to results" << std::endl;
                            // Try to get RTSP URLs for this camera
                            getRtspUrlsForCamera(camera);
                            discoveredCameras.push_back(camera);
                    } else {
                        std::cout << "Camera already in results, skipping" << std::endl;
                    }
                } else {
                    std::cout << "Received response didn't contain valid camera information" << std::endl;
                }
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
            }
            
            // Display status every second
            static auto lastStatusTime = startTime;
            auto now = std::chrono::steady_clock::now();
            if (now - lastStatusTime >= std::chrono::seconds(1)) {
                lastStatusTime = now;
                int elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
                std::cout << "Still listening for responses... (" << elapsedSeconds << "s elapsed)" << std::endl;
                
                // Re-send discovery message every 5 seconds to improve chances of discovery
                if (elapsedSeconds % 5 == 0) {
                    std::cout << "Re-sending discovery messages..." << std::endl;
                    
                    // Send through all interfaces again
                    if (networkInterface.empty()) {
                        for (const auto& iface : networkInterfaces) {
                            size_t pos = iface.find("(");
                            size_t endPos = iface.find(")");
                            if (pos != std::string::npos && endPos != std::string::npos) {
                                std::string ipAddress = iface.substr(pos + 1, endPos - pos - 1);
                                // Skip loopback addresses
                                if (ipAddress.substr(0, 3) == "127") {
                                    continue;
                                }
                                struct in_addr localInterface;
                                localInterface.s_addr = inet_addr(ipAddress.c_str());
                                setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &localInterface, sizeof(localInterface));
                                sendto(sock, ONVIF_DISCOVERY_MESSAGE.c_str(), ONVIF_DISCOVERY_MESSAGE.length(), 0,
                                       (struct sockaddr*)&multicastAddr, sizeof(multicastAddr));
                            }
                        }
                    } else {
                        struct in_addr localInterface;
                        localInterface.s_addr = inet_addr(networkInterface.c_str());
                        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &localInterface, sizeof(localInterface));
                        sendto(sock, ONVIF_DISCOVERY_MESSAGE.c_str(), ONVIF_DISCOVERY_MESSAGE.length(), 0,
                               (struct sockaddr*)&multicastAddr, sizeof(multicastAddr));
                    }
                    
                    // Also broadcast
                    sendto(sock, ONVIF_DISCOVERY_MESSAGE.c_str(), ONVIF_DISCOVERY_MESSAGE.length(), 0,
                           (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
                }
            }
        }
        
        // Clean up
        close(sock);
        
        std::cout << "Discovery completed: found " << discoveredCameras.size() << " ONVIF camera(s)" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error in ONVIF discovery: " << e.what() << std::endl;
    }
    
    return discoveredCameras;
}

OnvifCamera OnvifDiscovery::parseDiscoveryResponse(const std::string& response, const std::string& ipAddress) {
    std::cout << "Parsing discovery response from " << ipAddress << std::endl;
    
    OnvifCamera camera;
    camera.ipAddress = ipAddress;
    
    // Extract XAddrs (service endpoints)
    std::regex xaddrsRegex("<d:XAddrs>([^<]+)</d:XAddrs>");
    std::smatch xaddrsMatch;
    if (std::regex_search(response, xaddrsMatch, xaddrsRegex)) {
        camera.xaddrs = xaddrsMatch[1];
        std::cout << "Found XAddrs: " << camera.xaddrs << std::endl;
    } else {
        std::cout << "XAddrs not found in response" << std::endl;
        
        // Try alternative regex patterns
        std::regex altXaddrsRegex("<XAddrs>([^<]+)</XAddrs>");
        if (std::regex_search(response, xaddrsMatch, altXaddrsRegex)) {
            camera.xaddrs = xaddrsMatch[1];
            std::cout << "Found XAddrs (alternative pattern): " << camera.xaddrs << std::endl;
        }
    }
    
    // Extract device types
    std::regex typesRegex("<d:Types>([^<]+)</d:Types>");
    std::smatch typesMatch;
    if (std::regex_search(response, typesMatch, typesRegex)) {
        camera.types = typesMatch[1];
        std::cout << "Found Types: " << camera.types << std::endl;
    } else {
        std::cout << "Types not found in response" << std::endl;
        
        // Try alternative regex pattern
        std::regex altTypesRegex("<Types>([^<]+)</Types>");
        if (std::regex_search(response, typesMatch, altTypesRegex)) {
            camera.types = typesMatch[1];
            std::cout << "Found Types (alternative pattern): " << camera.types << std::endl;
        }
    }
    
    // Extract endpoint reference address
    std::regex epRefRegex("<wsa:Address>([^<]+)</wsa:Address>");
    std::smatch epRefMatch;
    if (std::regex_search(response, epRefMatch, epRefRegex)) {
        camera.endpointReference = epRefMatch[1];
        std::cout << "Found Endpoint Reference: " << camera.endpointReference << std::endl;
    } else {
        std::cout << "Endpoint Reference not found in response" << std::endl;
        
        // Try alternative regex pattern
        std::regex altEpRefRegex("<Address>([^<]+)</Address>");
        if (std::regex_search(response, epRefMatch, altEpRefRegex)) {
            camera.endpointReference = epRefMatch[1];
            std::cout << "Found Endpoint Reference (alternative pattern): " << camera.endpointReference << std::endl;
        }
    }
    
    // Extract scope information
    std::regex scopesRegex("<d:Scopes>([^<]+)</d:Scopes>");
    std::smatch scopesMatch;
    if (std::regex_search(response, scopesMatch, scopesRegex)) {
        std::string scopes = scopesMatch[1];
        std::cout << "Found Scopes: " << scopes << std::endl;
        
        // Extract name from scopes
        std::regex nameRegex("onvif://www\\.onvif\\.org/name/([^ ]+)");
        std::smatch nameMatch;
        if (std::regex_search(scopes, nameMatch, nameRegex)) {
            // URL decode the name
            camera.name = nameMatch[1];
            camera.name = std::regex_replace(camera.name, std::regex("%20"), " ");
            std::cout << "Found Name: " << camera.name << std::endl;
        } else {
            std::cout << "Name not found in scopes" << std::endl;
        }
        
        // Extract hardware information
        std::regex hardwareRegex("onvif://www\\.onvif\\.org/hardware/([^ ]+)");
        std::smatch hardwareMatch;
        if (std::regex_search(scopes, hardwareMatch, hardwareRegex)) {
            camera.hardware = hardwareMatch[1];
            camera.hardware = std::regex_replace(camera.hardware, std::regex("%20"), " ");
            std::cout << "Found Hardware: " << camera.hardware << std::endl;
        } else {
            std::cout << "Hardware not found in scopes" << std::endl;
        }
    } else {
        std::cout << "Scopes not found in response" << std::endl;
        
        // Try alternative regex pattern
        std::regex altScopesRegex("<Scopes>([^<]+)</Scopes>");
        if (std::regex_search(response, scopesMatch, altScopesRegex)) {
            std::string scopes = scopesMatch[1];
            std::cout << "Found Scopes (alternative pattern): " << scopes << std::endl;
            
            // Process scopes as above...
            std::regex nameRegex("onvif://www\\.onvif\\.org/name/([^ ]+)");
            std::smatch nameMatch;
            if (std::regex_search(scopes, nameMatch, nameRegex)) {
                camera.name = nameMatch[1];
                camera.name = std::regex_replace(camera.name, std::regex("%20"), " ");
                std::cout << "Found Name (from alt scopes): " << camera.name << std::endl;
            }
            
            std::regex hardwareRegex("onvif://www\\.onvif\\.org/hardware/([^ ]+)");
            std::smatch hardwareMatch;
            if (std::regex_search(scopes, hardwareMatch, hardwareRegex)) {
                camera.hardware = hardwareMatch[1];
                camera.hardware = std::regex_replace(camera.hardware, std::regex("%20"), " ");
                std::cout << "Found Hardware (from alt scopes): " << camera.hardware << std::endl;
            }
        }
    }
    
    // If no name was found, use IP address as a fallback
    if (camera.name.empty()) {
        camera.name = "ONVIF Camera (" + ipAddress + ")";
        std::cout << "Using fallback name: " << camera.name << std::endl;
    }
    
    return camera;
}

void OnvifDiscovery::getRtspUrlsForCamera(OnvifCamera& camera) {
    std::cout << "Generating RTSP URLs for camera at " << camera.ipAddress << std::endl;
    
    // Extract service URL
    std::string serviceUrl = camera.xaddrs;
    if (serviceUrl.empty()) {
        std::cout << "Warning: No XAddrs available, cannot reliably determine RTSP URLs" << std::endl;
        return;
    }
    
    // Many ONVIF cameras follow standard RTSP URL patterns
    // Try to build some common RTSP URL patterns based on the IP address
    
    // The most common RTSP URL patterns
    std::vector<std::string> rtspPatterns = {
        "rtsp://" + camera.ipAddress + ":554/onvif1",
        "rtsp://" + camera.ipAddress + ":554/Streaming/Channels/101",
        "rtsp://" + camera.ipAddress + ":554/Streaming/Channels/1",
        "rtsp://" + camera.ipAddress + ":554/cam/realmonitor?channel=1&subtype=0",
        "rtsp://" + camera.ipAddress + ":554/live",
        "rtsp://" + camera.ipAddress + ":554/media/media.amp",
        "rtsp://" + camera.ipAddress + ":554/h264",
        "rtsp://" + camera.ipAddress + ":554/11",
        "rtsp://" + camera.ipAddress + ":554/profile1",
        "rtsp://" + camera.ipAddress + ":554/profile2",
        "rtsp://" + camera.ipAddress + ":554/mpeg4/media.amp",
        "rtsp://" + camera.ipAddress + ":554/live/ch0",
        "rtsp://" + camera.ipAddress + ":554/live/ch1",
        "rtsp://" + camera.ipAddress + ":554/live/main",
        "rtsp://" + camera.ipAddress + ":554/live/sub",
        "rtsp://" + camera.ipAddress + ":554/videoinput_1/h264_1/media.stm",
        "rtsp://" + camera.ipAddress + ":554/video1",
        "rtsp://" + camera.ipAddress + ":554/video"
    };
    
    // Add these patterns to the camera's RTSP URLs
    camera.rtspUrls = rtspPatterns;
    
    std::cout << "Generated " << rtspPatterns.size() << " potential RTSP URLs" << std::endl;
    for (const auto& url : rtspPatterns) {
        std::cout << "  - " << url << std::endl;
    }
    
    // Note: A more robust implementation would make an actual ONVIF request
    // to get the exact RTSP URLs, but that requires ONVIF client implementation
    // which is beyond the scope of this example.
}

nlohmann::json OnvifDiscovery::camerasToJson(const std::vector<OnvifCamera>& cameras) {
    nlohmann::json result = nlohmann::json::array();
    
    for (const auto& camera : cameras) {
        nlohmann::json cameraJson;
        cameraJson["name"] = camera.name;
        cameraJson["ip_address"] = camera.ipAddress;
        cameraJson["hardware"] = camera.hardware;
        cameraJson["endpoint_reference"] = camera.endpointReference;
        cameraJson["types"] = camera.types;
        cameraJson["xaddrs"] = camera.xaddrs;
        
        // Add RTSP URLs
        nlohmann::json rtspUrls = nlohmann::json::array();
        for (const auto& url : camera.rtspUrls) {
            rtspUrls.push_back(url);
        }
        cameraJson["rtsp_urls"] = rtspUrls;
        
        result.push_back(cameraJson);
    }
    
    return result;
}

} // namespace tapi 
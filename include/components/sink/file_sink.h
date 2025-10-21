#pragma once

#include "component.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <mutex>
#include <memory>

namespace tapi {

/**
 * @brief File sink component for saving video to a file
 */
class FileSink : public SinkComponent {
public:
    /**
     * @brief Construct a new File Sink object
     * 
     * @param id Component ID
     * @param camera Parent camera
     * @param type Component type (should be "file")
     * @param config Initial configuration
     */
    FileSink(const std::string& id, Camera* camera, const std::string& type, const nlohmann::json& config);
    
    /**
     * @brief Destructor
     */
    ~FileSink() override;

    /**
     * @brief Initialize the file sink
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize() override;
    
    /**
     * @brief Start the file sink
     * 
     * @return true if start succeeded, false otherwise
     */
    bool start() override;
    
    /**
     * @brief Stop the file sink
     * 
     * @return true if stop succeeded, false otherwise
     */
    bool stop() override;
    
    /**
     * @brief Update component configuration
     * 
     * @param config JSON configuration
     * @return true if update succeeded, false otherwise
     */
    bool updateConfig(const nlohmann::json& config) override;
    
    /**
     * @brief Get component configuration
     * 
     * @return nlohmann::json Current configuration
     */
    nlohmann::json getConfig() const override;

    /**
     * @brief Get component status
     * 
     * @return nlohmann::json Component status
     */
    nlohmann::json getStatus() const override;
    
    /**
     * @brief Process a frame and write it to file
     * 
     * @param frame The frame to process
     * @return true if processing succeeded, false otherwise
     */
    bool processFrame(const cv::Mat& frame);
    
    /**
     * @brief Get the file path
     * 
     * @return std::string The current file path
     */
    std::string getFilePath() const;

private:
    std::string type_;                    ///< Component type name
    std::string filePath_;                ///< Path to output file
    int frameWidth_;                      ///< Width of output frames
    int frameHeight_;                     ///< Height of output frames
    int fps_;                             ///< Frames per second
    std::string fourcc_;                  ///< FourCC codec code
    bool useRawFrame_;                    ///< Whether to use raw (unannotated) frames
    
    std::mutex videoWriterMutex_;         ///< Mutex for video writer access
    cv::VideoWriter videoWriter_;         ///< OpenCV video writer
    bool isInitialized_;                  ///< Whether the writer is initialized
    size_t frameCount_;                   ///< Number of frames written
};

} // namespace tapi 
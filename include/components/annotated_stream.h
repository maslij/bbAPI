#pragma once

#include "component_instance.h"
#include "geometry/line_zone.h"
#include "vision_processing_engine.h"
#include <opencv2/opencv.hpp>
#include <mutex>
#include <deque>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace tapi {

/**
 * @brief Component that creates an annotated video stream with detections
 */
class AnnotatedStream : public ComponentInstance {
public:
    /**
     * @brief Class for drawing LineZone and object counts
     */
    class LineZoneAnnotator {
    public:
        /**
         * @brief Construct a LineZoneAnnotator with default settings
         */
        LineZoneAnnotator();
        
        /**
         * @brief Set the line thickness
         */
        void setThickness(int thickness);
        
        /**
         * @brief Set the line color
         */
        void setColor(const cv::Scalar& color);
        
        /**
         * @brief Set the text thickness
         */
        void setTextThickness(int textThickness);
        
        /**
         * @brief Set the text color
         */
        void setTextColor(const cv::Scalar& textColor);
        
        /**
         * @brief Set the text scale
         */
        void setTextScale(float textScale);
        
        /**
         * @brief Set the text offset from the line
         */
        void setTextOffset(float textOffset);
        
        /**
         * @brief Set the text padding inside the background box
         */
        void setTextPadding(int textPadding);
        
        /**
         * @brief Set custom text for in direction
         */
        void setInText(const std::string& text);
        
        /**
         * @brief Set custom text for out direction
         */
        void setOutText(const std::string& text);
        
        /**
         * @brief Control whether to display in count
         */
        void setDisplayInCount(bool display);
        
        /**
         * @brief Control whether to display out count
         */
        void setDisplayOutCount(bool display);
        
        /**
         * @brief Control whether to display text background box
         */
        void setDisplayTextBox(bool display);
        
        /**
         * @brief Control whether text should orient along the line
         */
        void setTextOrientToLine(bool orient);
        
        /**
         * @brief Control whether text should be centered on the line
         */
        void setTextCentered(bool centered);
        
        /**
         * @brief Draw a line zone on the frame
         * 
         * @param frame Frame to annotate
         * @param lineZone LineZone component to annotate
         * @param inCount Number of objects that crossed in
         * @param outCount Number of objects that crossed out
         * @return Annotated frame
         */
        cv::Mat annotate(cv::Mat& frame, const Point& startPoint, const Point& endPoint, 
                         int inCount, int outCount);
        
    private:
        int thickness_;
        cv::Scalar color_;
        int textThickness_;
        cv::Scalar textColor_;
        float textScale_;
        float textOffset_;
        int textPadding_;
        std::string inText_;
        std::string outText_;
        bool displayInCount_;
        bool displayOutCount_;
        bool displayTextBox_;
        bool textOrientToLine_;
        bool textCentered_;
        
        /**
         * @brief Calculate the angle of a line in degrees
         */
        float getLineAngle(const Point& start, const Point& end) const;
        
        /**
         * @brief Draw basic horizontal text
         */
        void drawBasicLabel(cv::Mat& frame, const Point& lineCenter, 
                          const std::string& text, bool isInCount);
        
        /**
         * @brief Draw text oriented along the line
         */
        void drawOrientedLabel(cv::Mat& frame, const Point& start, const Point& end,
                             const std::string& text, bool isInCount);
        
        /**
         * @brief Calculate where to place the count label
         */
        cv::Point calculateAnchorInFrame(const Point& start, const Point& end,
                                       int textWidth, int textHeight, 
                                       bool isInCount, int labelDimension) const;
        
        /**
         * @brief Create a rotated text label image
         */
        cv::Mat makeLabelImage(const std::string& text, float lineAngleDegrees);
        
        /**
         * @brief Overlay an image onto another with alpha blending
         */
        void overlayImage(cv::Mat& background, const cv::Mat& foreground, 
                        const cv::Point& location);
        
        /**
         * @brief Draw text with optional background box
         */
        void drawText(cv::Mat& scene, const std::string& text, const cv::Point& textAnchor, 
                     const cv::Scalar& textColor, float textScale, int textThickness, 
                     int textPadding, const cv::Scalar* backgroundColor = nullptr);
    };
    
    /**
     * @brief Construct a new Annotated Stream component
     * 
     * @param node Pipeline node
     * @param componentDef Component definition
     */
    AnnotatedStream(const PipelineNode& node, 
                   std::shared_ptr<VisionComponent> componentDef);
    
    /**
     * @brief Destructor
     */
    ~AnnotatedStream() override = default;
    
    /**
     * @brief Initialize the component
     */
    bool initialize() override;
    
    /**
     * @brief Process frame to add annotations
     */
    std::map<std::string, DataContainer> process(
        const std::map<std::string, DataContainer>& inputs) override;
    
    /**
     * @brief Reset the component
     */
    void reset() override;
    
    /**
     * @brief Update component configuration
     * 
     * @param newConfig The new configuration
     * @return true if the configuration was updated successfully
     */
    bool updateConfig(const std::map<std::string, nlohmann::json>& newConfig) override;
    
    /**
     * @brief Get the latest annotated frame
     */
    cv::Mat getLatestFrame() const;
    
    /**
     * @brief Get the parent processing engine
     * @return Shared pointer to the VisionProcessingEngine, or nullptr if not available
     */
    std::shared_ptr<VisionProcessingEngine> getProcessingEngine() const;
    
    /**
     * @brief Override to set the parent component and maintain a strong reference
     * @param parent Shared pointer to the parent processing engine
     */
    void setParentComponent(std::shared_ptr<VisionProcessingEngine> parent);
    
    /**
     * @brief Attempt to restore the parent component reference from the cache
     * @return true if restoration succeeded, false otherwise
     */
    bool restoreParentFromCache();

private:
    cv::Mat latestFrame_;           ///< Latest annotated frame
    mutable std::mutex frameMutex_; ///< Mutex for thread-safe frame access
    bool showLabels_;               ///< Whether to show labels
    bool showBoundingBoxes_;        ///< Whether to show bounding boxes
    bool showTracks_;               ///< Whether to show tracks
    bool showTitle_;                ///< Whether to show title
    bool showTimestamp_;            ///< Whether to show timestamp
    bool showPerformanceMetrics_;   ///< Whether to show performance metrics
    bool showLineZones_;            ///< Whether to show line zones
    float labelFontScale_;          ///< Font scale for labels
    cv::Scalar textColor_;          ///< Text color
    cv::Point titlePosition_;       ///< Position for title
    cv::Point timestampPosition_;   ///< Position for timestamp
    std::string title_;             ///< Stream title
    double lastProcessingTime_;     ///< Last frame processing time in ms
    LineZoneAnnotator lineZoneAnnotator_; ///< Line zone annotator
    
    // FPS tracking
    static const size_t FPS_WINDOW_SIZE = 30; // Track last 30 frames for averaging
    std::deque<double> processingTimes_;
    std::deque<int64_t> frameTimestamps_;
    int64_t lastFrameTimestamp_;
    std::mutex fpsMutex_;
    
    /**
     * @brief Draw detections on frame
     */
    void drawDetections(cv::Mat& frame, const std::vector<Detection>& detections);
    
    /**
     * @brief Draw tracks on frame
     */
    void drawTracks(cv::Mat& frame, const std::vector<Track>& tracks);
    
    /**
     * @brief Draw line zones and crossing counts
     */
    void drawLineZones(cv::Mat& frame, const std::vector<Event>& crossingEvents);
    
    /**
     * @brief Draw crossing events only (fallback method when no line zones available)
     */
    void drawCrossingEvents(cv::Mat& frame, const std::vector<Event>& crossingEvents);
    
    /**
     * @brief Draw title and timestamp on frame
     */
    void drawInfo(cv::Mat& frame, int64_t timestamp);
    
    /**
     * @brief Format timestamp as string
     */
    std::string formatTimestamp(int64_t timestamp) const;
};

} // namespace tapi 
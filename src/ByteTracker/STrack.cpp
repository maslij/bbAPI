#include "ByteTrack/STrack.h"

#include <cstddef>

byte_track::STrack::STrack(const Rect<float>& rect, const float& score, const int& label) :
    kalman_filter_(),
    mean_(),
    covariance_(),
    rect_(rect),
    state_(STrackState::New),
    is_activated_(false),
    score_(score),
    track_id_(0),
    frame_id_(0),
    start_frame_id_(0),
    tracklet_len_(0),
    label_(label)
{
}

byte_track::STrack::~STrack()
{
}

const byte_track::Rect<float>& byte_track::STrack::getRect() const
{
    return rect_;
}

const byte_track::STrackState& byte_track::STrack::getSTrackState() const
{
    return state_;
}

const bool& byte_track::STrack::isActivated() const
{
    return is_activated_;
}
const float& byte_track::STrack::getScore() const
{
    return score_;
}

const size_t& byte_track::STrack::getTrackId() const
{
    return track_id_;
}

const size_t& byte_track::STrack::getFrameId() const
{
    return frame_id_;
}

const size_t& byte_track::STrack::getStartFrameId() const
{
    return start_frame_id_;
}

const size_t& byte_track::STrack::getTrackletLength() const
{
    return tracklet_len_;
}

const int& byte_track::STrack::getLabel() const
{
    return label_;
}

void byte_track::STrack::activate(const size_t& frame_id, const size_t& track_id)
{
    // Initialize Kalman filter with the detection
    kalman_filter_.initiate(mean_, covariance_, rect_.getXyah());

    // Store original width and height
    float original_width = rect_.width();
    float original_height = rect_.height();
    
    // Update the position to match Kalman filter's initial state
    // but keep the original dimensions
    float center_x = mean_[0];
    float center_y = mean_[1];
    rect_.x() = center_x - original_width / 2;
    rect_.y() = center_y - original_height / 2;

    state_ = STrackState::Tracked;
    if (frame_id == 1)
    {
        is_activated_ = true;
    }
    track_id_ = track_id;
    frame_id_ = frame_id;
    start_frame_id_ = frame_id;
    tracklet_len_ = 0;
}

void byte_track::STrack::reActivate(const STrack &new_track, const size_t &frame_id, const int &new_track_id)
{
    // Update state with the new detection
    kalman_filter_.update(mean_, covariance_, new_track.getRect().getXyah());
    
    // Copy the dimensions directly from the new detection to ensure aspect ratio is preserved
    const auto& new_rect = new_track.getRect();
    rect_.width() = new_rect.width();
    rect_.height() = new_rect.height();
    
    // Update the position based on Kalman filter but keep the new detection's dimensions
    rect_.x() = mean_[0] - rect_.width() / 2;
    rect_.y() = mean_[1] - rect_.height() / 2;

    state_ = STrackState::Tracked;
    is_activated_ = true;
    score_ = new_track.getScore();
    // Update the label with the new track's label
    label_ = new_track.getLabel();
    if (0 <= new_track_id)
    {
        track_id_ = new_track_id;
    }
    frame_id_ = frame_id;
    tracklet_len_ = 0;
}

void byte_track::STrack::predict()
{
    if (state_ != STrackState::Tracked)
    {
        mean_[7] = 0;
    }
    kalman_filter_.predict(mean_, covariance_);
    // https://github.com/Vertical-Beach/ByteTrack-cpp/issues/22
    updateRect();
}

void byte_track::STrack::update(const STrack &new_track, const size_t &frame_id)
{
    // Update Kalman filter state with new measurements
    kalman_filter_.update(mean_, covariance_, new_track.getRect().getXyah());

    // Copy width and height from the new detection to preserve aspect ratio
    const auto& new_rect = new_track.getRect();
    rect_.width() = new_rect.width();
    rect_.height() = new_rect.height();
    
    // Update position based on Kalman filter but keep detection's dimensions
    rect_.x() = mean_[0] - rect_.width() / 2;
    rect_.y() = mean_[1] - rect_.height() / 2;

    state_ = STrackState::Tracked;
    is_activated_ = true;
    score_ = new_track.getScore();
    // Update the label with the new track's label
    label_ = new_track.getLabel();
    frame_id_ = frame_id;
    tracklet_len_++;
}

void byte_track::STrack::markAsLost()
{
    state_ = STrackState::Lost;
}

void byte_track::STrack::markAsRemoved()
{
    state_ = STrackState::Removed;
}

void byte_track::STrack::updateRect()
{
    // Instead of calculating width using the aspect ratio (mean_[2]), which can cause wide boxes,
    // we'll maintain the width and height proportions while centering on the Kalman filter's position

    // Store original width and height before updating
    float original_width = rect_.width();
    float original_height = rect_.height();
    
    // Calculate center position from Kalman filter (mean_[0], mean_[1])
    float center_x = mean_[0];
    float center_y = mean_[1];
    
    // If this is the first update (original rect is empty), use the Kalman filter's dimensions
    if (original_width <= 0 || original_height <= 0) {
        rect_.width() = mean_[2] * mean_[3];  // aspect_ratio * height
        rect_.height() = mean_[3];
    }
    // Otherwise keep the original size to maintain aspect ratio
    
    // Update the position to center the box on the Kalman filter's estimate
    rect_.x() = center_x - rect_.width() / 2;
    rect_.y() = center_y - rect_.height() / 2;
}
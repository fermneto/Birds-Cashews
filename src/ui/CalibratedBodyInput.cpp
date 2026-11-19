#include "ui/CalibratedBodyInput.h"
#include "vision/Clock.h"
#include "vision/VisionOverlay.h"
#include <algorithm>

FlightControls CalibratedBodyInput::poll() {
    const bool newSample = vision_.snapshot(view_, sequence_);
    if (newSample) previewFresh_ = poseFresh_ = true;
    return controls_.read(view_.gesture, newSample, nowSeconds());
}

bool CalibratedBodyInput::preview(cv::Mat& image) {
    if (!previewFresh_) return false;
    previewFresh_ = false;
    image = view_.frame.clone();
    drawVisionSkeleton(image, view_.pose);
    return true;
}

bool CalibratedBodyInput::latestPose(cv::Mat& mirroredFrame, Pose& pose) {
    if (!poseFresh_) return false;
    poseFresh_ = false;
    mirroredFrame = view_.frame;
    pose = view_.pose;
    return true;
}

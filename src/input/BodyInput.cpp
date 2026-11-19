#include "input/BodyInput.h"
#include "vision/VisionOverlay.h"
#include "vision/Clock.h"
#include <algorithm>

BodyInput::BodyInput(int cameraIndex, const std::string& modelPath) : vision_(cameraIndex, modelPath) {}

FlightControls BodyInput::poll() {
    const bool newSample = vision_.snapshot(view_, sequence_);
    if (newSample) fresh_ = true;
    return controls_.read(view_.gesture, newSample, nowSeconds());
}

bool BodyInput::preview(cv::Mat& image) {
    if (!fresh_) return false;
    fresh_ = false;
    image = view_.frame.clone();
    const VisionMetrics m = vision_.metrics();
    drawVisionOverlay(image, view_.pose, view_.gesture, m.processed / std::max(m.seconds, 0.001));
    return true;
}

#include "input/BodyInput.h"
#include "vision/VisionOverlay.h"
#include "vision/Clock.h"
#include <algorithm>

BodyInput::BodyInput(int cameraIndex, const std::string& modelPath) : vision_(cameraIndex, modelPath) {}

FlightControls BodyInput::poll() {
    if (vision_.snapshot(view_, sequence_)) {
        fresh_ = true;
        lastSample_ = nowSeconds();
    }
    const GestureState& gesture = view_.gesture;
    FlightControls controls;
    controls.mode = gesture.mode;
    controls.bodyControlled = true;
    controls.dive = gesture.dive;
    controls.pullUp = gesture.pullUp;
    controls.stroke = nowSeconds() - lastSample_ <= 0.20 ? gesture.stroke : 0;
    controls.leftWingHeight = gesture.leftWingHeight;
    controls.rightWingHeight = gesture.rightWingHeight;
    controls.bank = gesture.bank;
    return controls;
}

bool BodyInput::preview(cv::Mat& image) {
    if (!fresh_) return false;
    fresh_ = false;
    image = view_.frame.clone();
    const VisionMetrics m = vision_.metrics();
    drawVisionOverlay(image, view_.pose, view_.gesture, m.processed / std::max(m.seconds, 0.001));
    return true;
}

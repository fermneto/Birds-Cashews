#include "input/BodyInput.h"
#include "vision/VisionOverlay.h"
#include <algorithm>

BodyInput::BodyInput(int cameraIndex, const std::string& modelPath) : vision_(cameraIndex, modelPath) {}

FlightControls BodyInput::poll() {
    if (vision_.snapshot(view_, sequence_)) fresh_ = true;
    const GestureState& gesture = view_.gesture;
    FlightControls controls;
    controls.mode = gesture.mode;
    controls.bank = gesture.bank;
    controls.flaps = gesture.flapCount - lastFlapCount_;
    controls.flapStrength = gesture.flapStrength;
    lastFlapCount_ = gesture.flapCount;
    if (gesture.menuCount != lastMenuCount_) controls.menuPressed = gesture.menuChoice;
    lastMenuCount_ = gesture.menuCount;
    controls.raisedArm = gesture.raisedArm;
    controls.menuProgress = gesture.menuProgress;
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

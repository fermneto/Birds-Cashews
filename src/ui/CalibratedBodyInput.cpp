#include "ui/CalibratedBodyInput.h"
#include "vision/VisionOverlay.h"
#include <algorithm>

FlightControls CalibratedBodyInput::poll() {
    if (vision_.snapshot(view_, sequence_)) previewFresh_ = poseFresh_ = true;
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

bool CalibratedBodyInput::preview(cv::Mat& image) {
    if (!previewFresh_) return false;
    previewFresh_ = false;
    image = view_.frame.clone();
    const VisionMetrics m = vision_.metrics();
    drawVisionOverlay(image, view_.pose, view_.gesture, m.processed / std::max(m.seconds, 0.001));
    return true;
}

bool CalibratedBodyInput::latestPose(cv::Mat& mirroredFrame, Pose& pose) {
    if (!poseFresh_) return false;
    poseFresh_ = false;
    mirroredFrame = view_.frame;
    pose = view_.pose;
    return true;
}

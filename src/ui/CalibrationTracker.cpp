#include "ui/CalibrationTracker.h"
#include <algorithm>

namespace {
constexpr float kMinConfidence = 0.3f;
constexpr float kFrameMargin = 0.01f;     // Pontos colados na borda costumam ser estimados.
constexpr float kBodyHoldSeconds = 0.5f;
constexpr float kRegionHoldSeconds = 0.2f;

constexpr int kBodyJoints[] = {Nose, LShoulder, RShoulder, LElbow, RElbow, LWrist, RWrist,
                               LHip, RHip, LKnee, RKnee, LAnkle, RAnkle};

cv::Point2f normalized(cv::Point2f point, cv::Size frame) {
    return {point.x / std::max(frame.width, 1), point.y / std::max(frame.height, 1)};
}
}

void CalibrationTracker::reset() {
    *this = CalibrationTracker{};
}

bool CalibrationTracker::inRegion(CalRegion region, cv::Point2f p) {
    if (p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1) return false;
    switch (region) {
        case CalRegion::Left: return p.x <= kCalRegionBand;
        case CalRegion::Right: return p.x >= 1.0f - kCalRegionBand;
        case CalRegion::Top: return p.y <= kCalRegionBand;
        case CalRegion::Bottom: return p.y >= 1.0f - kCalRegionBand;
    }
    return false;
}

bool CalibrationTracker::isFullBody(const Pose& pose, cv::Size frame) {
    if (!pose.valid) return false;
    return std::all_of(std::begin(kBodyJoints), std::end(kBodyJoints), [&](int joint) {
        if (!pose.has(joint, kMinConfidence)) return false;
        const cv::Point2f p = normalized(pose.at(joint), frame);
        return p.x >= kFrameMargin && p.x <= 1.0f - kFrameMargin && p.y >= kFrameMargin && p.y <= 1.0f - kFrameMargin;
    });
}

void CalibrationTracker::updateBody(const Pose& pose, cv::Size frame, float seconds) {
    bodyNow_ = isFullBody(pose, frame);
    bodyHeld_ = bodyNow_ ? bodyHeld_ + seconds : 0.0f;
    if (bodyHeld_ >= kBodyHoldSeconds) bodyConfirmed_ = true;
}

// Exige um instante na regiao para que um ponto ruidoso nao marque sozinho.
void CalibrationTracker::updateRegions(const Pose& pose, cv::Size frame, float seconds) {
    for (int i = 0; i < kCalRegionCount; i++) {
        const auto region = static_cast<CalRegion>(i);
        bool inside = false;
        for (int wrist : {LWrist, RWrist})
            inside = inside || (pose.has(wrist, kMinConfidence) && inRegion(region, normalized(pose.at(wrist), frame)));
        held_[i] = inside ? held_[i] + seconds : 0.0f;
        if (held_[i] >= kRegionHoldSeconds) reached_[i] = true;
    }
}

void CalibrationTracker::update(const Pose& pose, cv::Size frame, float seconds) {
    updateBody(pose, frame, seconds);
    updateRegions(pose, frame, seconds);
}

int CalibrationTracker::marksDone() const {
    return static_cast<int>(std::count(reached_.begin(), reached_.end(), true)) + (bodyConfirmed_ ? 1 : 0);
}

bool CalibrationTracker::complete() const {
    return marksDone() == kCalRegionCount + 1;
}

bool CalibrationTracker::acceptsGesture(const FlightControls& controls) {
    if (!complete()) {
        armsLowered_ = false;
        return false;
    }
    if (controls.raisedArm == ArmSide::None && controls.menuPressed == ArmSide::None) armsLowered_ = true;
    return armsLowered_;
}

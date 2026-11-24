#include "ui/CalibrationTracker.h"
#include <algorithm>

namespace {
constexpr float kMinConfidence = 0.3f;
constexpr float kFrameMargin = 0.01f;     // Pontos colados na borda costumam ser estimados.
constexpr float kBodyHoldSeconds = 0.4f;
constexpr float kRegionHoldSeconds = 0.15f;
constexpr int kBodySamples = 3;           // Poses diferentes do detector, para nao marcar por um quadro ruidoso.
constexpr int kRegionSamples = 2;
constexpr float kGapSeconds = 0.3f;       // Falha tolerada do detector antes de zerar a contagem.

constexpr int kBodyJoints[] = {Nose, LShoulder, RShoulder, LElbow, RElbow, LWrist, RWrist,
                               LHip, RHip, LKnee, RKnee, LAnkle, RAnkle};

cv::Point2f normalized(cv::Point2f point, cv::Size frame) {
    return {point.x / std::max(frame.width, 1), point.y / std::max(frame.height, 1)};
}
}

void CalibrationTracker::Hold::step(bool inside, float seconds, bool newSample, float needed, int minSamples) {
    if (done) return;
    if (inside) {
        missing = 0;
        held += seconds;
        if (newSample) samples++;
        done = held >= needed && samples >= minSamples;
        return;
    }
    // Pontos perto da borda somem em quadros isolados; so uma ausencia mais longa recomeca.
    missing += seconds;
    if (missing > kGapSeconds) held = 0, samples = 0;
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

// Cada regiao conta sozinha, entao as duas maos podem marcar esquerda e direita juntas.
void CalibrationTracker::update(const Pose& pose, cv::Size frame, float seconds, bool newSample) {
    bodyNow_ = isFullBody(pose, frame);
    body_.step(bodyNow_, seconds, newSample, kBodyHoldSeconds, kBodySamples);
    for (int i = 0; i < kCalRegionCount; i++) {
        const auto region = static_cast<CalRegion>(i);
        bool inside = false;
        for (int wrist : {LWrist, RWrist})
            inside = inside || (pose.has(wrist, kMinConfidence) && inRegion(region, normalized(pose.at(wrist), frame)));
        regions_[i].step(inside, seconds, newSample, kRegionHoldSeconds, kRegionSamples);
    }
}

int CalibrationTracker::marksDone() const {
    const int regions = static_cast<int>(std::count_if(regions_.begin(), regions_.end(), [](const Hold& h) { return h.done; }));
    return regions + (body_.done ? 1 : 0);
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

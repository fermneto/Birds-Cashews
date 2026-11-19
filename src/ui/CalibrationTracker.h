#pragma once
#include "entity/FlightControls.h"
#include "vision/Pose.h"
#include <array>

enum class CalRegion { Left, Right, Top, Bottom };
constexpr int kCalRegionCount = 4;

// Faixa de cada regiao, como fracao do quadro espelhado.
constexpr float kCalRegionBand = 0.2f;

// Confere se o jogador e detectado em todo o video: corpo inteiro e maos nas quatro bordas.
class CalibrationTracker {
public:
    void reset();
    void update(const Pose& mirroredPose, cv::Size frame, float seconds);

    static bool inRegion(CalRegion region, cv::Point2f normalized);
    static bool isFullBody(const Pose& pose, cv::Size frame);

    bool bodyVisibleNow() const { return bodyNow_; }
    bool bodyConfirmed() const { return bodyConfirmed_; }
    bool reached(CalRegion region) const { return reached_[static_cast<size_t>(region)]; }
    int marksDone() const;
    bool complete() const;

    // Levar a mao ao alto imita o gesto do menu, entao os gestos so valem depois de concluir
    // e de o jogador baixar os bracos.
    bool acceptsGesture(const FlightControls& controls);

private:
    void updateBody(const Pose& pose, cv::Size frame, float seconds);
    void updateRegions(const Pose& pose, cv::Size frame, float seconds);

    std::array<bool, kCalRegionCount> reached_{};
    std::array<float, kCalRegionCount> held_{};
    float bodyHeld_ = 0;
    bool bodyNow_ = false, bodyConfirmed_ = false, armsLowered_ = false;
};

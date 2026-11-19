#pragma once
#include "input/FlightInput.h"
#include "vision/VisionPipeline.h"

// Controla o passaro com o corpo, pela webcam.
class BodyInput : public FlightInput {
public:
    BodyInput(int cameraIndex, const std::string& modelPath);
    bool start() { return vision_.start(); }
    FlightControls poll() override;
    bool preview(cv::Mat& image) override;
    VisionMetrics metrics() { return vision_.metrics(); }

private:
    VisionPipeline vision_;
    VisionSnapshot view_;
    uint64_t sequence_ = 0;
    uint32_t lastFlapCount_ = 0, lastMenuCount_ = 0;
    bool fresh_ = false;
};

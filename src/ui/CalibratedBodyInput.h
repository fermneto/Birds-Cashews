#pragma once
#include "input/FlightInput.h"
#include "ui/PoseSource.h"
#include "vision/VisionPipeline.h"

// Mesmo controle pelo corpo do BodyInput, com acesso a imagem limpa e ao esqueleto para a calibracao.
class CalibratedBodyInput : public FlightInput, public PoseSource {
public:
    CalibratedBodyInput(int cameraIndex, const std::string& modelPath) : vision_(cameraIndex, modelPath) {}

    bool start() { return vision_.start(); }
    FlightControls poll() override;
    bool preview(cv::Mat& image) override;
    bool latestPose(cv::Mat& mirroredFrame, Pose& pose) override;
    bool bodyDetected() const override { return view_.pose.valid; }

private:
    VisionPipeline vision_;
    VisionSnapshot view_;
    uint64_t sequence_ = 0;
    uint32_t lastFlapCount_ = 0, lastMenuCount_ = 0;
    bool previewFresh_ = false, poseFresh_ = false;
};

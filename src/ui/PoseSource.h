#pragma once
#include "vision/Pose.h"

// Imagem espelhada e esqueleto usados pela calibracao.
class PoseSource {
public:
    virtual ~PoseSource() = default;
    virtual bool latestPose(cv::Mat& mirroredFrame, Pose& pose) = 0;   // Retorna true quando ha quadro novo.
    virtual bool bodyDetected() const = 0;
};

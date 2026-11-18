#pragma once
#include "vision/Pose.h"
#include <opencv2/dnn.hpp>
#include <string>
#include <vector>

// Detecta o jogador com YOLOv8-pose usando o modulo DNN do OpenCV.
class PoseDetector {
public:
    explicit PoseDetector(const std::string& modelPath);
    Pose detect(const cv::Mat& frame);   // Retorna o esqueleto suavizado do jogador principal.

private:
    struct Candidate { float score = 0; float area = 0; Pose pose; };

    cv::Mat letterbox(const cv::Mat& frame, float& scale, cv::Point2f& pad) const;
    void decodeScale(const cv::Mat& box, const cv::Mat& cls, const cv::Mat& kpt, int stride,
                     Candidate& best) const;
    float distanceFromBins(const float* bins) const;
    void restoreCoordinates(Pose& pose, float scale, cv::Point2f pad) const;
    Pose smooth(const Pose& current);

    cv::dnn::Net net_;
    std::vector<std::string> outputNames_;
    Pose last_;
    int missedFrames_ = 0;
    static constexpr int kInputSize = 320;
};

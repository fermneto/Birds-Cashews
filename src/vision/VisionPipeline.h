#pragma once
#include "vision/Camera.h"
#include "vision/GestureRecognizer.h"
#include "vision/PoseDetector.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// Ultimo resultado da visao, com imagem e esqueleto ja espelhados.
struct VisionSnapshot {
    cv::Mat frame;
    Pose pose;
    GestureState gesture;
    uint64_t sequence = 0;
};

struct VisionMetrics {
    uint64_t captured = 0, processed = 0, withPerson = 0, withArms = 0;
    double inferenceMs = 0, latencyMs = 0, worstInferenceMs = 0, seconds = 0;
};

// Liga camera, detector e reconhecedor em uma thread separada do jogo.
class VisionPipeline {
public:
    VisionPipeline(int cameraIndex, const std::string& modelPath);
    ~VisionPipeline();

    bool start();
    void stop();
    bool snapshot(VisionSnapshot& out, uint64_t& lastSequence);
    VisionMetrics metrics();

private:
    void loop();
    void process(const cv::Mat& frame, uint64_t sequence, double stamp);
    void record(const Pose& pose, double inferenceMs, double stamp);

    Camera camera_;
    PoseDetector detector_;
    GestureRecognizer recognizer_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    VisionSnapshot latest_;
    VisionMetrics metrics_;
    double startedAt_ = 0;
};

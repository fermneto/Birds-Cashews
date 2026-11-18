#include "vision/VisionPipeline.h"
#include "vision/Clock.h"
#include <algorithm>
#include <chrono>

VisionPipeline::VisionPipeline(int cameraIndex, const std::string& modelPath)
    : camera_(cameraIndex), detector_(modelPath) {}

VisionPipeline::~VisionPipeline() { stop(); }

bool VisionPipeline::start() {
    if (!camera_.start()) return false;
    startedAt_ = nowSeconds();
    running_ = true;
    thread_ = std::thread(&VisionPipeline::loop, this);
    return true;
}

void VisionPipeline::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    camera_.stop();
}

void VisionPipeline::loop() {
    cv::Mat frame;
    uint64_t sequence = 0;
    double stamp = 0;
    while (running_) {
        if (camera_.latest(frame, sequence, stamp)) process(frame, sequence, stamp);
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void VisionPipeline::process(const cv::Mat& frame, uint64_t sequence, double stamp) {
    const double before = nowSeconds();
    const Pose pose = detector_.detect(frame).mirrored(frame.cols);
    const double inferenceMs = (nowSeconds() - before) * 1000.0;
    const GestureState gesture = recognizer_.update(pose, stamp);

    cv::Mat mirrored;
    cv::flip(frame, mirrored, 1);
    std::lock_guard<std::mutex> lock(mutex_);
    latest_.frame = mirrored;
    latest_.pose = pose;
    latest_.gesture = gesture;
    latest_.sequence = sequence;
    record(pose, inferenceMs, stamp);
}

void VisionPipeline::record(const Pose& pose, double inferenceMs, double stamp) {
    metrics_.processed++;
    metrics_.inferenceMs += inferenceMs;
    metrics_.worstInferenceMs = std::max(metrics_.worstInferenceMs, inferenceMs);
    metrics_.latencyMs += (nowSeconds() - stamp) * 1000.0;
    if (pose.valid) metrics_.withPerson++;
    if (pose.has(LShoulder) && pose.has(RShoulder) && pose.has(LWrist) && pose.has(RWrist)) metrics_.withArms++;
}

bool VisionPipeline::snapshot(VisionSnapshot& out, uint64_t& lastSequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (latest_.frame.empty() || latest_.sequence == lastSequence) return false;
    out = latest_;
    lastSequence = latest_.sequence;
    return true;
}

VisionMetrics VisionPipeline::metrics() {
    std::lock_guard<std::mutex> lock(mutex_);
    VisionMetrics result = metrics_;
    result.captured = camera_.framesRead();
    result.seconds = nowSeconds() - startedAt_;
    return result;
}

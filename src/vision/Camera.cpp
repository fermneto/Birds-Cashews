#include "vision/Camera.h"
#include "vision/Clock.h"

Camera::Camera(int index) : index_(index) {}

Camera::~Camera() { stop(); }

void Camera::configure() {
    capture_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    capture_.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    capture_.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    capture_.set(cv::CAP_PROP_FPS, 30);
    capture_.set(cv::CAP_PROP_BUFFERSIZE, 1);
}

bool Camera::start() {
    if (!capture_.open(index_, cv::CAP_V4L2)) return false;
    configure();
    running_ = true;
    thread_ = std::thread(&Camera::loop, this);
    return true;
}

void Camera::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    capture_.release();
}

void Camera::loop() {
    cv::Mat grabbed;
    while (running_) {
        if (!capture_.read(grabbed) || grabbed.empty()) continue;
        const double stamp = nowSeconds();
        std::lock_guard<std::mutex> lock(mutex_);
        grabbed.copyTo(frame_);
        stamp_ = stamp;
        sequence_++;
    }
}

bool Camera::latest(cv::Mat& frame, uint64_t& sequence, double& stamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frame_.empty() || sequence_.load() == sequence) return false;
    frame_.copyTo(frame);
    sequence = sequence_.load();
    stamp = stamp_;
    return true;
}

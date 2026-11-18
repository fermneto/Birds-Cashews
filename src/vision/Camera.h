#pragma once
#include <opencv2/videoio.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

// Le a webcam em uma thread propria e guarda apenas o quadro mais recente,
// evitando que a fila do driver acumule atraso quando a deteccao for mais lenta.
class Camera {
public:
    explicit Camera(int index);
    ~Camera();
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    bool start();
    void stop();
    bool latest(cv::Mat& frame, uint64_t& sequence, double& stamp);
    uint64_t framesRead() const { return sequence_.load(); }

private:
    void configure();
    void loop();

    int index_;
    cv::VideoCapture capture_;
    std::thread thread_;
    std::mutex mutex_;
    cv::Mat frame_;
    double stamp_ = 0;
    std::atomic<uint64_t> sequence_{0};
    std::atomic<bool> running_{false};
};

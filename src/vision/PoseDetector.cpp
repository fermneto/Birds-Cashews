#include "vision/PoseDetector.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

static float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

PoseDetector::PoseDetector(const std::string& modelPath) {
    net_ = cv::dnn::readNetFromONNX(modelPath);
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    outputNames_ = net_.getUnconnectedOutLayersNames();
}

// Redimensiona mantendo a proporcao e completa o restante com bordas.
cv::Mat PoseDetector::letterbox(const cv::Mat& frame, float& scale, cv::Point2f& pad) const {
    scale = static_cast<float>(kInputSize) / std::max(frame.cols, frame.rows);
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(), scale, scale, cv::INTER_LINEAR);
    int padX = (kInputSize - resized.cols) / 2, padY = (kInputSize - resized.rows) / 2;
    pad = cv::Point2f(static_cast<float>(padX), static_cast<float>(padY));
    cv::Mat square;
    cv::copyMakeBorder(resized, square, padY, kInputSize - resized.rows - padY, padX,
                       kInputSize - resized.cols - padX, cv::BORDER_CONSTANT, cv::Scalar::all(114));
    return square;
}

// Distribution Focal Loss: a distancia e o valor esperado de 16 faixas.
float PoseDetector::distanceFromBins(const float* bins) const {
    float maxValue = bins[0];
    for (int i = 1; i < 16; i++) maxValue = std::max(maxValue, bins[i]);
    float sum = 0, expected = 0;
    for (int i = 0; i < 16; i++) {
        float weight = std::exp(bins[i] - maxValue);
        sum += weight;
        expected += weight * i;
    }
    return expected / sum;
}

void PoseDetector::decodeScale(const cv::Mat& box, const cv::Mat& cls, const cv::Mat& kpt, int stride,
                               Candidate& best) const {
    const int rows = cls.size[2], cols = cls.size[3];
    const float* clsData = cls.ptr<float>();
    const float* boxData = box.ptr<float>();
    const float* kptData = kpt.ptr<float>();

    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            const int cell = y * cols + x;
            const float score = sigmoid(clsData[cell]);
            if (score < 0.4f) continue;

            float sides[4];
            for (int s = 0; s < 4; s++) {
                float bins[16];
                for (int b = 0; b < 16; b++) bins[b] = boxData[(s * 16 + b) * rows * cols + cell];
                sides[s] = distanceFromBins(bins);
            }
            const float area = (sides[0] + sides[2]) * (sides[1] + sides[3]) * stride * stride;
            if (area * score <= best.area * best.score) continue;

            best.score = score;
            best.area = area;
            best.pose.valid = true;
            for (int j = 0; j < JointCount; j++) {
                const float rawX = kptData[(j * 3) * rows * cols + cell];
                const float rawY = kptData[(j * 3 + 1) * rows * cols + cell];
                const float rawC = kptData[(j * 3 + 2) * rows * cols + cell];
                best.pose.joints[j].pos = cv::Point2f((rawX * 2 + x + 0.5f) * stride,
                                                      (rawY * 2 + y + 0.5f) * stride);
                best.pose.joints[j].conf = sigmoid(rawC);
            }
        }
    }
}

void PoseDetector::restoreCoordinates(Pose& pose, float scale, cv::Point2f pad) const {
    for (auto& joint : pose.joints) joint.pos = (joint.pos - pad) * (1.0f / scale);
}

// Media movel exponencial reduz a tremedeira; falhas curtas mantem o ultimo esqueleto.
Pose PoseDetector::smooth(const Pose& current) {
    if (!current.valid) {
        if (++missedFrames_ > 6) last_.valid = false;
        return last_;
    }
    missedFrames_ = 0;
    if (!last_.valid) { last_ = current; return last_; }
    const float alpha = 0.6f;
    for (int j = 0; j < JointCount; j++) {
        last_.joints[j].pos = last_.joints[j].pos * (1 - alpha) + current.joints[j].pos * alpha;
        last_.joints[j].conf = last_.joints[j].conf * (1 - alpha) + current.joints[j].conf * alpha;
    }
    return last_;
}

Pose PoseDetector::detect(const cv::Mat& frame) {
    float scale;
    cv::Point2f pad;
    cv::Mat input = letterbox(frame, scale, pad);
    cv::Mat blob = cv::dnn::blobFromImage(input, 1.0 / 255.0, cv::Size(), cv::Scalar(), true, false);
    net_.setInput(blob);
    std::vector<cv::Mat> outputs;
    net_.forward(outputs, outputNames_);

    Candidate best;
    for (int stride : {8, 16, 32}) {
        const int cells = kInputSize / stride;
        const cv::Mat* box = nullptr, *cls = nullptr, *kpt = nullptr;
        for (const auto& out : outputs) {
            if (out.size[2] != cells) continue;
            if (out.size[1] == 64) box = &out;
            else if (out.size[1] == 1) cls = &out;
            else if (out.size[1] == 51) kpt = &out;
        }
        if (box && cls && kpt) decodeScale(*box, *cls, *kpt, stride, best);
    }
    if (best.pose.valid) restoreCoordinates(best.pose, scale, pad);
    return smooth(best.pose);
}

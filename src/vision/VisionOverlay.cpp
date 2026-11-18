#include "vision/VisionOverlay.h"
#include <opencv2/imgproc.hpp>
#include <string>

void drawOutlinedText(cv::Mat& image, const std::string& text, cv::Point at, double size, cv::Scalar color) {
    cv::putText(image, text, at, cv::FONT_HERSHEY_SIMPLEX, size, cv::Scalar(0, 0, 0), 5, cv::LINE_AA);
    cv::putText(image, text, at, cv::FONT_HERSHEY_SIMPLEX, size, color, 2, cv::LINE_AA);
}

namespace {
void drawSkeleton(cv::Mat& image, const Pose& pose) {
    static const int bones[][2] = {{LShoulder, RShoulder}, {LShoulder, LElbow}, {LElbow, LWrist},
                                   {RShoulder, RElbow}, {RElbow, RWrist}, {LShoulder, LHip},
                                   {RShoulder, RHip}, {LHip, RHip}};
    for (const auto& bone : bones)
        if (pose.has(bone[0]) && pose.has(bone[1]))
            cv::line(image, pose.at(bone[0]), pose.at(bone[1]), cv::Scalar(90, 230, 120), 3, cv::LINE_AA);
    for (int joint : {LShoulder, RShoulder, LElbow, RElbow, LWrist, RWrist, LHip, RHip})
        if (pose.has(joint)) cv::circle(image, pose.at(joint), 6, cv::Scalar(40, 200, 255), -1, cv::LINE_AA);
}

void drawBankBar(cv::Mat& image, float bank) {
    const cv::Point center(image.cols / 2, image.rows - 34);
    cv::rectangle(image, center - cv::Point(150, 10), center + cv::Point(150, 10), cv::Scalar(40, 40, 40), -1);
    cv::rectangle(image, center - cv::Point(0, 10), center + cv::Point(static_cast<int>(bank * 150), 10),
                  cv::Scalar(60, 200, 255), -1);
    cv::line(image, center - cv::Point(0, 16), center + cv::Point(0, 16), cv::Scalar(255, 255, 255), 2);
}

void drawGesture(cv::Mat& image, const GestureState& g, double fps) {
    const cv::Scalar color = g.mode == FlightMode::Lost ? cv::Scalar(70, 70, 255) : cv::Scalar(120, 255, 120);
    drawOutlinedText(image, flightModeName(g.mode), {20, 50}, 1.2, color);
    drawOutlinedText(image, "Batidas: " + std::to_string(g.flapCount), {20, 90}, 0.7, cv::Scalar(255, 255, 255));
    drawOutlinedText(image, "FPS: " + std::to_string(static_cast<int>(fps)), {20, 120}, 0.7, cv::Scalar(255, 255, 255));
    const char* arm = g.raisedArm == ArmSide::Left ? "Menu: ESQUERDO" : g.raisedArm == ArmSide::Right ? "Menu: DIREITO" : "";
    drawOutlinedText(image, arm, {20, 150}, 0.7, cv::Scalar(255, 220, 120));
    if (g.menuProgress > 0)
        cv::rectangle(image, cv::Rect(20, 160, static_cast<int>(200 * g.menuProgress), 10), cv::Scalar(255, 220, 120), -1);
    drawBankBar(image, g.bank);
}
}

void drawVisionOverlay(cv::Mat& image, const Pose& pose, const GestureState& gesture, double fps) {
    drawSkeleton(image, pose);
    drawGesture(image, gesture, fps);
}

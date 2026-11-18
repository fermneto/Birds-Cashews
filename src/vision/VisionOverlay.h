#pragma once
#include "vision/GestureRecognizer.h"
#include <opencv2/core.hpp>

// Desenha esqueleto e comandos reconhecidos sobre a imagem espelhada da webcam.
void drawVisionOverlay(cv::Mat& image, const Pose& pose, const GestureState& gesture, double fps);
void drawOutlinedText(cv::Mat& image, const std::string& text, cv::Point at, double size, cv::Scalar color);

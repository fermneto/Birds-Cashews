#pragma once
#include <opencv2/core.hpp>

// Cores da interface, em BGR.
namespace theme {
const cv::Scalar kInk(38, 26, 20);
const cv::Scalar kPanel(48, 36, 28);
const cv::Scalar kAccent(36, 116, 226);
const cv::Scalar kGold(70, 200, 250);
const cv::Scalar kGreen(110, 214, 112);
const cv::Scalar kRed(84, 92, 240);
const cv::Scalar kWhite(245, 245, 245);
const cv::Scalar kMuted(196, 204, 208);
}

// Posicao da imagem da webcam no canto da tela, na resolucao logica.
const cv::Rect kPreviewArea(948, 12, 320, 240);

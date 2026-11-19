#pragma once
#include <opencv2/core.hpp>

// Paleta da interface, em BGR: tons de mata ao entardecer com o amarelo do caju como destaque.
namespace theme {
const cv::Scalar kInk(26, 32, 18);
const cv::Scalar kPanel(32, 40, 24);
const cv::Scalar kAccent(58, 104, 232);
const cv::Scalar kGold(84, 200, 255);
const cv::Scalar kGreen(140, 220, 150);
const cv::Scalar kRed(92, 106, 244);
const cv::Scalar kWhite(236, 248, 255);
const cv::Scalar kMuted(198, 216, 208);
}

// Margem externa da interface, dentro da area segura da tela.
constexpr int kUiMargin = 40;

// Posicao da imagem da webcam no canto da tela, na resolucao logica.
const cv::Rect kPreviewArea(1036, 28, 204, 153);

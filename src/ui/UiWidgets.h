#pragma once
#include "ui/UiScenes.h"
#include "ui/UiTheme.h"

TextStyle uiStyle(int size, cv::Scalar color = theme::kWhite, TextAlign align = TextAlign::Left);

// Indica o que um braco faz, com a barra de progresso do gesto mantido.
void drawArmPrompt(UiKit& kit, const cv::Rect& area, ArmSide arm, const std::string& action, bool active, float progress,
                   float opacity = 1.0f);

// Moldura discreta da webcam, usada apenas quando ha camera ligada. Deve ser desenhada depois dos fundos.
void drawPreviewFrame(UiKit& kit);

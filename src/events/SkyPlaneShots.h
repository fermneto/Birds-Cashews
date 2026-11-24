#pragma once
#include "game/GameplaySession.h"
#include <glm/glm.hpp>
#include <opencv2/core.hpp>
#include <functional>
#include <string>

// Fotos do --render-test: o aviao do teto de perto e de longe, como o jogador o ve chegando.
// "shoot" aponta a camera de eye para target e devolve o quadro desenhado.
void captureSkyPlaneShots(const std::string& outputDir, const std::function<cv::Mat(glm::vec3 eye, glm::vec3 target)>& shoot,
                          GameplaySession& session);

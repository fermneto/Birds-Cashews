#pragma once
#include "entity/FlightControls.h"
#include <opencv2/core.hpp>

// Origem dos comandos de voo.
class FlightInput {
public:
    virtual ~FlightInput() = default;
    virtual FlightControls poll() = 0;
    virtual bool preview(cv::Mat& image) = 0;   // Retorna true quando ha imagem nova.
};

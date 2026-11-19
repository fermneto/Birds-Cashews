#pragma once
#include "input/FlightInput.h"

// Controle alternativo para testar o jogo sem a webcam.
class KeyboardInput : public FlightInput {
public:
    FlightControls poll() override;
    bool preview(cv::Mat& image) override;

private:
    bool spaceHeld_ = false, shown_ = false, leftHeld_ = false, rightHeld_ = false;
    float bank_ = 0;
    uint32_t lastFlapTicks_ = 0;
};

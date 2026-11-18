#include "input/KeyboardInput.h"
#include "vision/VisionOverlay.h"
#include <SDL.h>

FlightControls KeyboardInput::poll() {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const Uint32 now = SDL_GetTicks();
    FlightControls controls;

    const bool space = keys[SDL_SCANCODE_SPACE];
    if (space && !spaceHeld_) {
        controls.flaps = 1;
        controls.flapStrength = 0.8f;
        lastFlapTicks_ = now;
    }
    spaceHeld_ = space;

    const float target = static_cast<float>(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
                       - static_cast<float>(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
    bank_ += (target - bank_) * 0.12f;
    controls.bank = bank_;

    // Q e E equivalem a erguer o braco esquerdo e o direito nos menus.
    const bool left = keys[SDL_SCANCODE_Q], right = keys[SDL_SCANCODE_E];
    if (left && !leftHeld_) controls.menuPressed = ArmSide::Left;
    else if (right && !rightHeld_) controls.menuPressed = ArmSide::Right;
    leftHeld_ = left;
    rightHeld_ = right;

    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) controls.mode = FlightMode::Dive;
    else if (lastFlapTicks_ != 0 && now - lastFlapTicks_ < 700) controls.mode = FlightMode::Flap;
    return controls;
}

bool KeyboardInput::preview(cv::Mat& image) {
    if (shown_) return false;
    shown_ = true;
    image = cv::Mat(480, 640, CV_8UC3, cv::Scalar(45, 35, 30));
    drawOutlinedText(image, "MODO TECLADO", {30, 80}, 1.3, cv::Scalar(120, 255, 120));
    drawOutlinedText(image, "Espaco: bater", {30, 170}, 1.0, cv::Scalar(255, 255, 255));
    drawOutlinedText(image, "S: mergulhar", {30, 230}, 1.0, cv::Scalar(255, 255, 255));
    drawOutlinedText(image, "A / D: curvar", {30, 290}, 1.0, cv::Scalar(255, 255, 255));
    drawOutlinedText(image, "Q / E: menus", {30, 350}, 1.0, cv::Scalar(255, 255, 255));
    return true;
}

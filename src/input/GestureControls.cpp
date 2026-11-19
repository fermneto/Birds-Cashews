#include "input/GestureControls.h"

namespace {
constexpr double kStrokeLifetime = 0.20;   // Sem imagem nova, a batida nao continua empurrando o passaro.
}

FlightControls GestureControls::read(const GestureState& gesture, bool newSample, double now) {
    if (newSample) lastSample_ = now;
    FlightControls controls;
    controls.mode = gesture.mode;
    controls.bank = gesture.bank;
    controls.bodyControlled = true;
    controls.dive = gesture.dive;
    controls.pullUp = gesture.pullUp;
    controls.stroke = lastSample_ >= 0 && now - lastSample_ <= kStrokeLifetime ? gesture.stroke : 0.0f;
    controls.leftWingHeight = gesture.leftWingHeight;
    controls.rightWingHeight = gesture.rightWingHeight;
    controls.flaps = gesture.flapCount - lastFlapCount_;
    controls.flapStrength = gesture.flapStrength;
    lastFlapCount_ = gesture.flapCount;
    if (gesture.menuCount != lastMenuCount_) controls.menuPressed = gesture.menuChoice;
    lastMenuCount_ = gesture.menuCount;
    controls.raisedArm = gesture.raisedArm;
    controls.menuProgress = gesture.menuProgress;
    return controls;
}

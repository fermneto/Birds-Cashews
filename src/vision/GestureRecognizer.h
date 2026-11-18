#pragma once
#include "vision/Pose.h"
#include <cstdint>

enum class FlightMode { Lost, Glide, Flap, Dive };
enum class ArmSide { None, Left, Right };

const char* flightModeName(FlightMode mode);

// Resultado do reconhecimento. Os contadores sao cumulativos para que quem consome
// nao perca eventos quando roda em ritmo diferente da camera.
struct GestureState {
    FlightMode mode = FlightMode::Lost;
    float bank = 0;                  // -1 (esquerda) a 1 (direita).
    uint32_t flapCount = 0;
    float flapStrength = 0;          // 0 a 1, forca da ultima batida.
    ArmSide raisedArm = ArmSide::None;
    float menuProgress = 0;          // 0 a 1 enquanto a postura do menu e mantida.
    uint32_t menuCount = 0;
    ArmSide menuChoice = ArmSide::None;
};

// Converte o esqueleto espelhado nos comandos de voo.
class GestureRecognizer {
public:
    GestureState update(const Pose& mirroredPose, double time);
    const GestureState& state() const { return state_; }

private:
    struct Arm { float out = 0; float drop = 0; };
    struct Features { Arm left, right; float wristAngleDeg = 0; };

    bool extract(const Pose& pose, Features& features);
    float bodyScale(const Pose& pose);
    void updateFlap(const Features& f, double time);
    void restartStroke(const Features& f, double time);
    void updateMode(const Features& f, double time);
    void updateBank(const Features& f, double dt);
    void updateMenu(const Features& f, double time);
    void handleMissing(double time, double dt);
    void resetTracking();

    GestureState state_;
    float scale_ = 0;
    double lastTime_ = -1, lastSeen_ = -1;

    bool strokeDown_ = false, strokeReady_ = false;
    float topHeight_ = 0, topLeft_ = 0, topRight_ = 0, bottomHeight_ = 0;
    double topTime_ = 0, lastFlapTime_ = -100;

    double diveSince_ = -1;
    ArmSide heldArm_ = ArmSide::None;
    double heldSince_ = 0;
    bool menuFired_ = false;
};

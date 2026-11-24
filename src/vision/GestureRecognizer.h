#pragma once
#include "vision/Pose.h"
#include <cstdint>
#include <deque>

enum class FlightMode { Lost, Glide, Flap, Dive };
enum class ArmSide { None, Left, Right };

const char* flightModeName(FlightMode mode);

// Pontos sem os quais nao ha comando: o voo inteiro sai de ombros e punhos. Os quadris so
// refinam a escala quando aparecem, e pernas nao entram, entao o jogador pode ficar perto.
constexpr int kControlJoints[] = {LShoulder, RShoulder, LWrist, RWrist};
constexpr float kControlMinConfidence = 0.3f;

// Resultado do reconhecimento. Os contadores sao cumulativos para que quem consome
// nao perca eventos quando roda em ritmo diferente da camera.
struct GestureState {
    FlightMode mode = FlightMode::Lost;
    float dive = 0, pullUp = 0, stroke = 0;
    float leftWingHeight = 0, rightWingHeight = 0;
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
    struct Features { Arm left, right; float wristAngleDeg = 0, shoulderAngleDeg = 0; };

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

    struct WristSample { double time; float left, right; };
    std::deque<WristSample> wristHistory_;
    bool strokeDown_ = false, strokeReady_ = false;
    float topHeight_ = 0, topLeft_ = 0, topRight_ = 0, bottomHeight_ = 0;
    float strokeRecovery_ = 0;
    float strokeStartHeight_ = 0;
    double topTime_ = 0, lastFlapTime_ = -100;

    double diveSince_ = -1;
    ArmSide heldArm_ = ArmSide::None;
    double heldSince_ = 0;
    bool menuFired_ = false;
};

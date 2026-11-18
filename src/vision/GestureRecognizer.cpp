#include "vision/GestureRecognizer.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinConfidence = 0.3f;
constexpr float kFlapAmplitude = 0.6f;     // Descida media minima, em larguras de corpo.
constexpr float kFlapArmMin = 0.4f;        // Descida minima de cada braco.
constexpr float kFlapRecover = 0.35f;      // Subida necessaria para armar a proxima batida.
constexpr double kStrokeWindow = 0.7;      // Descidas mais lentas nao contam como batida.
constexpr double kFlapHold = 0.9;          // Tempo em modo Flap depois da ultima batida.
constexpr double kDiveHold = 0.25;
constexpr double kDiveHoldAfterFlap = 0.6; // Evita mergulhar no ponto baixo de uma batida.
constexpr float kDiveMaxOut = 0.55f, kDiveMinDrop = 0.9f;
constexpr float kGlideMinOut = 0.7f, kGlideMinDrop = -1.4f, kGlideMaxDrop = 1.3f;
constexpr float kBankMinOut = 0.4f, kBankDeadZone = 8.0f, kBankMax = 45.0f;
constexpr double kBankSmoothing = 0.08;
constexpr double kLostGrace = 0.25;
constexpr float kMenuRaised = -0.6f, kMenuOtherMax = -0.1f;
constexpr double kMenuHold = 0.7;
}

const char* flightModeName(FlightMode mode) {
    switch (mode) {
        case FlightMode::Glide: return "PLANAR";
        case FlightMode::Flap:  return "BATER";
        case FlightMode::Dive:  return "MERGULHAR";
        default:                return "SEM JOGADOR";
    }
}

// Usa o maior entre ombros e tronco: ao girar o corpo, a largura dos ombros encolhe.
float GestureRecognizer::bodyScale(const Pose& pose) {
    float scale = static_cast<float>(cv::norm(pose.at(LShoulder) - pose.at(RShoulder)));
    if (pose.has(LHip, kMinConfidence) && pose.has(RHip, kMinConfidence)) {
        const cv::Point2f shoulders = (pose.at(LShoulder) + pose.at(RShoulder)) * 0.5f;
        const cv::Point2f hips = (pose.at(LHip) + pose.at(RHip)) * 0.5f;
        scale = std::max(scale, 0.6f * static_cast<float>(cv::norm(shoulders - hips)));
    }
    scale_ = scale_ > 0 ? scale_ * 0.8f + scale * 0.2f : scale;
    return scale_;
}

bool GestureRecognizer::extract(const Pose& pose, Features& f) {
    for (int joint : {LShoulder, RShoulder, LWrist, RWrist})
        if (!pose.has(joint, kMinConfidence)) return false;

    const float scale = bodyScale(pose);
    if (scale < 8.0f) return false;

    // Na imagem espelhada, o lado esquerdo do jogador fica a esquerda da tela.
    const bool swapped = pose.at(LShoulder).x > pose.at(RShoulder).x;
    const cv::Point2f ls = pose.at(swapped ? RShoulder : LShoulder), rs = pose.at(swapped ? LShoulder : RShoulder);
    const cv::Point2f lw = pose.at(swapped ? RWrist : LWrist), rw = pose.at(swapped ? LWrist : RWrist);

    f.left = {(ls.x - lw.x) / scale, (lw.y - ls.y) / scale};
    f.right = {(rw.x - rs.x) / scale, (rw.y - rs.y) / scale};
    f.wristAngleDeg = static_cast<float>(std::atan2(rw.y - lw.y, rw.x - lw.x) * 180.0 / CV_PI);
    return true;
}

void GestureRecognizer::restartStroke(const Features& f, double time) {
    topLeft_ = f.left.drop;
    topRight_ = f.right.drop;
    topHeight_ = (topLeft_ + topRight_) * 0.5f;
    topTime_ = time;
    strokeReady_ = true;
}

// Maquina de estados: procura o topo, conta a batida na descida e espera os bracos subirem.
void GestureRecognizer::updateFlap(const Features& f, double time) {
    const float height = (f.left.drop + f.right.drop) * 0.5f;
    if (strokeDown_) {
        bottomHeight_ = std::max(bottomHeight_, height);
        if (bottomHeight_ - height >= kFlapRecover) {
            strokeDown_ = false;
            restartStroke(f, time);
        }
        return;
    }
    if (!strokeReady_ || height < topHeight_ || time - topTime_ > kStrokeWindow) restartStroke(f, time);

    const bool bothArms = std::min(f.left.drop - topLeft_, f.right.drop - topRight_) >= kFlapArmMin;
    if (height - topHeight_ >= kFlapAmplitude && bothArms) {
        state_.flapCount++;
        state_.flapStrength = std::clamp((0.9f - topHeight_) / 1.2f, 0.55f, 1.0f);
        lastFlapTime_ = time;
        strokeDown_ = true;
        bottomHeight_ = height;
    }
}

void GestureRecognizer::updateMode(const Features& f, double time) {
    const auto isDown = [](const Arm& a) { return a.out < kDiveMaxOut && a.drop > kDiveMinDrop; };
    const auto isOpen = [](const Arm& a) {
        return a.out > kGlideMinOut && a.drop > kGlideMinDrop && a.drop < kGlideMaxDrop;
    };
    const bool flapping = time - lastFlapTime_ < kFlapHold;

    if (isDown(f.left) && isDown(f.right)) {
        if (diveSince_ < 0) diveSince_ = time;
    } else {
        diveSince_ = -1;
    }
    const double needed = flapping ? kDiveHoldAfterFlap : kDiveHold;
    if (diveSince_ >= 0 && time - diveSince_ >= needed) state_.mode = FlightMode::Dive;
    else if (flapping) state_.mode = FlightMode::Flap;
    else if (isOpen(f.left) && isOpen(f.right)) state_.mode = FlightMode::Glide;
    else if (state_.mode == FlightMode::Lost || state_.mode == FlightMode::Flap) state_.mode = FlightMode::Glide;
}

void GestureRecognizer::updateBank(const Features& f, double dt) {
    float target = 0;
    const bool armsOpen = f.left.out > kBankMinOut && f.right.out > kBankMinOut;
    if (state_.mode == FlightMode::Glide && armsOpen) {
        const float magnitude = std::abs(f.wristAngleDeg);
        const float amount = std::clamp((magnitude - kBankDeadZone) / (kBankMax - kBankDeadZone), 0.0f, 1.0f);
        target = std::copysign(amount, f.wristAngleDeg);
    }
    const float blend = 1.0f - static_cast<float>(std::exp(-dt / kBankSmoothing));
    state_.bank += (target - state_.bank) * blend;
    if (target == 0 && std::abs(state_.bank) < 0.01f) state_.bank = 0;
}

// Um braco erguido e mantido escolhe a opcao. Os dois erguidos nao contam.
void GestureRecognizer::updateMenu(const Features& f, double time) {
    ArmSide side = ArmSide::None;
    if (f.left.drop < kMenuRaised && f.right.drop > kMenuOtherMax) side = ArmSide::Left;
    else if (f.right.drop < kMenuRaised && f.left.drop > kMenuOtherMax) side = ArmSide::Right;

    if (side != heldArm_) {
        heldArm_ = side;
        heldSince_ = time;
        menuFired_ = false;
    }
    state_.raisedArm = side;
    state_.menuProgress = side == ArmSide::None ? 0.0f
        : static_cast<float>(std::min((time - heldSince_) / kMenuHold, 1.0));
    if (side != ArmSide::None && !menuFired_ && time - heldSince_ >= kMenuHold) {
        menuFired_ = true;
        state_.menuCount++;
        state_.menuChoice = side;
    }
}

void GestureRecognizer::resetTracking() {
    strokeDown_ = strokeReady_ = false;
    diveSince_ = -1;
    heldArm_ = ArmSide::None;
    menuFired_ = false;
    state_.raisedArm = ArmSide::None;
    state_.menuProgress = 0;
    lastFlapTime_ = -100;
}

// Falhas curtas mantem o ultimo comando. Depois disso o passaro nivela sozinho.
void GestureRecognizer::handleMissing(double time, double dt) {
    if (lastSeen_ >= 0 && time - lastSeen_ <= kLostGrace) return;
    state_.mode = FlightMode::Lost;
    resetTracking();
    state_.bank *= static_cast<float>(std::exp(-dt / kBankSmoothing));
    if (std::abs(state_.bank) < 0.01f) state_.bank = 0;
}

GestureState GestureRecognizer::update(const Pose& pose, double time) {
    const double dt = lastTime_ < 0 ? 0.0 : std::max(0.0, time - lastTime_);
    lastTime_ = time;

    Features features;
    if (!pose.valid || !extract(pose, features)) {
        handleMissing(time, dt);
        return state_;
    }
    lastSeen_ = time;
    updateFlap(features, time);
    updateMode(features, time);
    updateBank(features, dt);
    updateMenu(features, time);
    return state_;
}

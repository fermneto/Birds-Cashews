#include "vision/GestureRecognizer.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinConfidence = 0.3f;
constexpr float kFlapAmplitude = 0.6f;     // Descida media minima, em larguras de corpo.
constexpr float kFlapArmMin = 0.4f;        // Descida minima de cada braco.
constexpr float kFlapRecover = 0.35f;      // Subida necessaria para armar a proxima batida.
constexpr double kSpeedWindow = 0.20;
constexpr double kMinSpeedInterval = 0.09;
constexpr float kMinDownSpeed = 2.4f; // Larguras de corpo por segundo, em cada punho.
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
    for (int joint : kControlJoints)
        if (!pose.has(joint, kControlMinConfidence)) return false;

    const float scale = bodyScale(pose);
    if (scale < 8.0f) return false;

    // Na imagem espelhada, o lado esquerdo do jogador fica a esquerda da tela.
    const bool swapped = pose.at(LShoulder).x > pose.at(RShoulder).x;
    const cv::Point2f ls = pose.at(swapped ? RShoulder : LShoulder), rs = pose.at(swapped ? LShoulder : RShoulder);
    const cv::Point2f lw = pose.at(swapped ? RWrist : LWrist), rw = pose.at(swapped ? LWrist : RWrist);

    f.left = {(ls.x - lw.x) / scale, (lw.y - ls.y) / scale};
    f.right = {(rw.x - rs.x) / scale, (rw.y - rs.y) / scale};
    f.shoulderAngleDeg = std::atan2(rs.y - ls.y, rs.x - ls.x) * 180.0f / CV_PI;
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
    if (!wristHistory_.empty() && time - wristHistory_.back().time > kSpeedWindow) {
        wristHistory_.clear();
        strokeReady_ = false;
    }
    if (wristHistory_.empty()) strokeStartHeight_ = height;
    wristHistory_.push_back({time, f.left.drop, f.right.drop});
    while (wristHistory_.size() > 1 && wristHistory_[1].time <= time - kSpeedWindow)
        wristHistory_.pop_front();
    const auto& previous = wristHistory_.front();
    const double elapsed = time - previous.time;
    const float descent = std::min(f.left.drop - previous.left, f.right.drop - previous.right);
    // Velocidade conjunta dos punhos em larguras de corpo por segundo.
    const float speed = elapsed >= kMinSpeedInterval ? static_cast<float>(descent / elapsed) : 0;
    if (height < strokeStartHeight_ || (f.left.drop < previous.left && f.right.drop < previous.right))
        strokeStartHeight_ = height;
    const float travel = std::clamp((height - strokeStartHeight_) / 1.5f, 0.0f, 1.0f);
    const float rate = std::clamp((speed - 1.35f) / 2.8f, 0.0f, 1.6f);
    state_.stroke = rate * (0.75f + 0.25f * travel);
    state_.leftWingHeight = std::clamp(-f.left.drop / 1.4f, -1.0f, 1.0f);
    state_.rightWingHeight = std::clamp(-f.right.drop / 1.4f, -1.0f, 1.0f);
    const bool fastDownstroke = elapsed >= kMinSpeedInterval && descent >= kMinDownSpeed * elapsed;
    if (strokeDown_) {
        bottomHeight_ = std::max(bottomHeight_, height);
        if (bottomHeight_ - height >= kFlapRecover) {
            strokeDown_ = false;
            restartStroke(f, time);
        }
        return;
    }
    if (!strokeReady_ || height <= topHeight_ || time - topTime_ > kStrokeWindow) restartStroke(f, time);

    const bool bothArms = std::min(f.left.drop - topLeft_, f.right.drop - topRight_) >= kFlapArmMin;
    if (height - topHeight_ >= kFlapAmplitude && bothArms && fastDownstroke) {
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
    const float open = std::clamp((std::min(f.left.out, f.right.out) - 0.1f) / kBankMinOut, 0.0f, 1.0f);
    const float difference = f.right.drop - f.left.drop;
    const float armTilt = std::atan2(difference, 1.8f) * 180.0f / CV_PI;
    const float tuckedAngle = f.shoulderAngleDeg + armTilt;
    const float angle = tuckedAngle + open * (f.wristAngleDeg - tuckedAngle);
    const float target = std::copysign(std::clamp((std::abs(angle) - kBankDeadZone) /
                                        (kBankMax - kBankDeadZone), 0.0f, 1.0f), angle);
    const float blend = 1.0f - static_cast<float>(std::exp(-dt / (kBankSmoothing + 0.08 * std::abs(target))));
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
    wristHistory_.clear();
    strokeRecovery_ = 0;
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
    state_.stroke = 0;
    if (lastSeen_ >= 0 && time - lastSeen_ <= kLostGrace) return;
    state_.mode = FlightMode::Lost;
    state_.leftWingHeight = state_.rightWingHeight = 0;
    state_.dive = state_.pullUp = state_.stroke = 0;
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
    const float drop = (features.left.drop + features.right.drop) * 0.5f;
    const float blend = 1.0f - std::exp(-static_cast<float>(dt) / 0.14f);
    // Enquanto as asas recuperam altura, o fim da descida ainda faz parte
    // do ciclo. Uma postura baixa mantida volta gradualmente ao mergulho.
    strokeRecovery_ = std::max(std::min(1.0f, 2.5f * state_.stroke),
                               strokeRecovery_ * std::exp(-static_cast<float>(dt) / 0.40f));
    state_.dive += blend * (std::clamp(drop / 1.45f, 0.0f, 1.0f) * (1.0f - strokeRecovery_) - state_.dive);
    state_.pullUp += blend * (std::clamp(-drop / 1.2f, 0.0f, 1.0f) - state_.pullUp);
    updateBank(features, dt);
    updateMenu(features, time);
    return state_;
}

#include "entity/FlightModel.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kFlapLift = 14.0f, kFlapThrust = 3.0f, kMaxClimb = 22.0f, kMaxFlapSpeed = 44.0f;
constexpr float kFlapClimb = 5.0f, kFlapSpeedBonus = 8.0f;
constexpr float kGlideSink = 2.0f, kSwoopFactor = 0.55f;
constexpr float kDiveSink = 34.0f, kDiveSpeed = 46.0f;
constexpr float kGroundFriction = 30.0f;
constexpr float kTurnRate = glm::radians(60.0f), kMaxRoll = glm::radians(50.0f);

float approach(float value, float target, float rate, float dt) {
    return value + (target - value) * (1.0f - std::exp(-rate * dt));
}
}

void FlightModel::place(glm::vec3 position, float yaw, float speed) {
    position_ = position;
    yaw_ = yaw;
    speed_ = speed;
    verticalSpeed_ = pitch_ = roll_ = 0;
    grounded_ = false;
}

glm::vec3 FlightModel::heading() const { return {std::sin(yaw_), 0, -std::cos(yaw_)}; }

void FlightModel::flap(float strength) {
    grounded_ = false;
    // A batida cancela a queda antes de somar a subida, para responder na hora.
    verticalSpeed_ = std::min(std::max(verticalSpeed_, 0.0f) + kFlapLift * strength, kMaxClimb);
    const float limit = kMaxFlapSpeed * stats_.speed;
    if (speed_ < limit) speed_ = std::min(speed_ + kFlapThrust * strength, limit);
}

// Sair de um mergulho converte o excesso de velocidade em subida.
void FlightModel::updateSpeeds(FlightMode mode, float dt) {
    if (grounded_) {
        speed_ = std::max(0.0f, speed_ - kGroundFriction * dt);
        verticalSpeed_ = -1.0f;
        return;
    }
    const float cruise = kCruiseSpeed * stats_.speed;
    if (mode == FlightMode::Dive) {
        speed_ = approach(speed_, kDiveSpeed * stats_.speed, 0.8f, dt);
        verticalSpeed_ = approach(verticalSpeed_, -kDiveSink, 1.6f, dt);
        return;
    }
    if (mode == FlightMode::Flap) {
        speed_ = approach(speed_, cruise + kFlapSpeedBonus * stats_.speed, 0.7f, dt);
        verticalSpeed_ = approach(verticalSpeed_, kFlapClimb, 0.9f, dt);
        return;
    }
    const float excess = std::max(0.0f, speed_ - cruise);
    speed_ = approach(speed_, cruise, 0.5f, dt);
    verticalSpeed_ = approach(verticalSpeed_, -kGlideSink + excess * kSwoopFactor, 1.2f, dt);
}

void FlightModel::updateAttitude(const FlightControls& controls, float dt) {
    const float bank = controls.mode == FlightMode::Lost ? 0.0f : controls.bank;
    if (!grounded_) yaw_ += bank * kTurnRate * stats_.agility * dt;
    roll_ = approach(roll_, bank * kMaxRoll, 5.0f, dt);
    pitch_ = approach(pitch_, targetPitch(controls.mode), 4.0f, dt);
}

// O nariz sobe ao bater, fica quase nivelado ao planar e aponta para baixo no mergulho.
float FlightModel::targetPitch(FlightMode mode) const {
    if (grounded_) return 0.0f;
    const float path = std::atan2(verticalSpeed_, std::max(speed_, 8.0f));
    if (mode == FlightMode::Flap) return std::max(path, glm::radians(7.0f));
    if (mode == FlightMode::Dive) return std::min(path, glm::radians(-42.0f));
    return path < 0.0f ? path * 0.45f : path;
}

void FlightModel::applyCeiling(float dt) {
    if (position_.y > kCeiling) verticalSpeed_ -= (position_.y - kCeiling) * 2.0f * dt;
    if (position_.y > kHardCeiling) {
        position_.y = kHardCeiling;
        verticalSpeed_ = std::min(verticalSpeed_, 0.0f);
    }
}

void FlightModel::step(const FlightControls& controls, float dt) {
    updateSpeeds(controls.mode, dt);
    updateAttitude(controls, dt);
    position_ += heading() * speed_ * dt + glm::vec3(0, verticalSpeed_ * dt, 0);
    applyCeiling(dt);
}

// Retorna a intensidade do impacto para efeitos de camera e som.
float FlightModel::applyContact(const Contact& contact) {
    if (contact.surface == Surface::Water) {
        const float impact = std::max(0.0f, -verticalSpeed_);
        verticalSpeed_ = std::max(verticalSpeed_, 7.0f);
        speed_ *= 0.98f;
        return impact;
    }
    if (contact.surface == Surface::Ground && contact.normal.y > 0.6f) {
        const float impact = std::max(0.0f, -verticalSpeed_);
        grounded_ = verticalSpeed_ <= 0.0f;
        verticalSpeed_ = std::max(verticalSpeed_, 0.0f);
        return impact;
    }
    glm::vec3 motion = velocity();
    const float into = glm::dot(motion, contact.normal);
    if (into >= 0) return 0;
    motion = (motion - contact.normal * into * 1.4f) * 0.55f;
    const float horizontal = glm::length(glm::vec2(motion.x, motion.z));
    if (horizontal > 0.5f) yaw_ = std::atan2(motion.x, -motion.z);
    speed_ = horizontal;
    verticalSpeed_ = motion.y;
    return -into;
}

void FlightModel::steerToward(float targetYaw, float rate, float dt) {
    const float difference = std::remainder(targetYaw - yaw_, 6.2831853f);
    yaw_ += std::clamp(difference, -rate * dt, rate * dt);
}

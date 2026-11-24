#include "entity/PlayerBird.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
constexpr float kFlapDuration = 0.45f;
constexpr float kBaseFov = 72.0f, kMinFov = 64.0f, kMaxFov = 112.0f;
constexpr float kFovPerSpeed = 0.85f, kDiveFovBonus = 6.0f;
constexpr float kFovOpenRate = 7.0f, kFovCloseRate = 3.5f;
constexpr float kBeakOpenRate = 12.0f, kBeakCloseRate = 5.0f;
constexpr float kOuterRadius = World::kBoundaryRadius + 180.0f;   // Ninguem passa disso, nem esperando a baleia.
}

void PlayerBird::spawn(glm::vec3 position, float yaw) { flight_.place(position, yaw, FlightModel::kCruiseSpeed); }

// Com o corpo, cada descida dos punhos que passa do limite conta uma batida; no teclado, cada Espaco.
void PlayerBird::applyControls(const FlightControls& controls) {
    controls_ = controls;
    if (controls.bodyControlled) {
        constexpr float kStrokeStart = 0.3f;
        if (controls.stroke > kStrokeStart && lastStroke_ <= kStrokeStart) flapSound_ = std::max(flapSound_, std::min(controls.stroke, 1.0f));
        lastStroke_ = controls.stroke;
        return;
    }
    lastStroke_ = 0;
    if (controls.flaps > 0) flapSound_ = std::max(flapSound_, std::clamp(controls.flapStrength, 0.3f, 1.0f));
    for (uint32_t i = 0; i < controls.flaps; i++) {
        flight_.flap(controls.flapStrength);
        flapTimer_ = kFlapDuration;
        animation_.kick = controls.flapStrength;
    }
}

// Alem do limite quem age e a baleia (events/BoundaryWhale). O vento continua como reserva, para quando
// ela ainda nao respondeu ou acabou de atacar, e uma parede invisivel garante que ninguem se perde no mar.
void PlayerBird::enforceBoundary(float dt) {
    glm::vec3& p = flight_.position();
    const float radius = glm::length(glm::vec2(p.x, p.z));
    nearBoundary_ = radius > World::kWarningRadius;
    if (radius <= World::kBoundaryRadius) return;
    const float strength = std::clamp((radius - World::kBoundaryRadius) / 120.0f, 0.15f, 1.0f);
    flight_.steerToward(std::atan2(-p.x, p.z), 2.5f * strength, dt);
    if (radius > kOuterRadius) {
        p.x *= kOuterRadius / radius;
        p.z *= kOuterRadius / radius;
    }
}

void PlayerBird::step(const World& world, float dt) {
    flight_.step(controls_, dt);
    enforceBoundary(dt);
    const Contact contact = world.resolve(flight_.position(), kRadius);
    if (!contact.hit()) {
        flight_.leaveGround();
        return;
    }
    const float impact = flight_.applyContact(contact);
    if (contact.surface != Surface::Solid) return;
    animation_.shake = std::max(animation_.shake, std::min(impact / 30.0f, 1.0f));
    solidImpact_ = std::max(solidImpact_, impact);
}

float PlayerBird::takeFlap() {
    const float flap = flapSound_;
    flapSound_ = 0;
    return flap;
}

float PlayerBird::takeSolidImpact() {
    const float impact = solidImpact_;
    solidImpact_ = 0;
    return impact;
}

// Usa a velocidade total, entao a queda do mergulho tambem abre o campo de visao.
float PlayerBird::targetFov() const {
    const float excess = glm::length(flight_.velocity()) - FlightModel::kCruiseSpeed;
    const float bonus = controls_.diveAmount() * kDiveFovBonus;
    return std::clamp(kBaseFov + excess * kFovPerSpeed + bonus, kMinFov, kMaxFov);
}

void PlayerBird::setBeakTarget(BeakSource source, float openness) {
    beakTargets_[static_cast<int>(source)] = std::clamp(openness, 0.0f, 1.0f);
}

void PlayerBird::animateBeak(float dt) {
    const float target = *std::max_element(std::begin(beakTargets_), std::end(beakTargets_));
    const float rate = target > animation_.beakOpen ? kBeakOpenRate : kBeakCloseRate;
    animation_.beakOpen += (target - animation_.beakOpen) * (1.0f - std::exp(-rate * dt));
}

void PlayerBird::animate(float dt) {
    animateBeak(dt);
    flapTimer_ = std::max(0.0f, flapTimer_ - dt);
    float angle = -4.0f - 64.0f * controls_.diveAmount() + 28.0f * controls_.pullUp;
    const float sweep = 40.0f * controls_.diveAmount();
    if (flapTimer_ > 0)
        angle += 36.0f * std::sin(6.2831853f * (1.0f - flapTimer_ / kFlapDuration)) * controls_.flapStrength;
    const float blend = 1.0f - std::exp(-18.0f * dt);
    const float left = controls_.bodyControlled ? -4.0f + controls_.leftWingHeight * 57.0f : angle;
    const float right = controls_.bodyControlled ? -4.0f + controls_.rightWingHeight * 57.0f : angle;
    animation_.leftWingAngle += (left - animation_.leftWingAngle) * blend;
    animation_.rightWingAngle += (right - animation_.rightWingAngle) * blend;
    animation_.wingSweep += (sweep - animation_.wingSweep) * blend;
    animation_.shake *= std::exp(-5.0f * dt);
    animation_.kick *= std::exp(-6.0f * dt);
    const float speed = glm::length(flight_.velocity());
    const float vibrationTarget = glm::smoothstep(0.90f, 1.0f, speed / flight_.fullDiveSpeed());
    animation_.speedVibration += (vibrationTarget - animation_.speedVibration) * (1.0f - std::exp(-3.5f * dt));
    const float windTarget = glm::smoothstep(FlightModel::kCruiseSpeed, 48.0f, speed);
    animation_.wind += (windTarget - animation_.wind) * (1.0f - std::exp(-5.0f * dt));
    animation_.windPhase += dt * (0.48f + animation_.wind);
    const float target = targetFov();
    const float rate = target > animation_.fov ? kFovOpenRate : kFovCloseRate;
    animation_.fov += (target - animation_.fov) * (1.0f - std::exp(-rate * dt));
}

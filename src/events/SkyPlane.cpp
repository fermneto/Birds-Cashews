#include "events/SkyPlane.h"
#include "entity/PlayerBird.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kRequestRetry = 3.0f;        // Sem resposta da autoridade, pede de novo.
constexpr float kHitNoticeSeconds = 3.0f;
constexpr float kTurnRate = 6.0f;            // Quao rapido o desenho acompanha a perseguicao.
constexpr float kBankPerTurn = 0.6f;         // Radianos de inclinacao por radiano/s de curva.
constexpr float kMaxBank = 0.8f;
constexpr float kDepartClimb = 0.08f;        // Depois de passar, o aviao sobe de leve.
constexpr float kKeepSpeed = 0.4f;           // Parte da velocidade do passaro que sobra depois da batida.

float yawOf(glm::vec3 direction) { return std::atan2(direction.x, -direction.z); }

float approach(float value, float target, float rate, float dt) {
    return value + (target - value) * (1.0f - std::exp(-rate * dt));
}

const PlayerPublicState* findPlayer(const std::vector<PlayerPublicState>& players, PlayerId id) {
    for (const PlayerPublicState& player : players)
        if (player.id == id) return &player;
    return nullptr;
}
}

glm::vec3 planeAimFor(glm::vec3 target) {
    return {target.x, std::clamp(target.y, kPlaneFloor, FlightModel::kHardCeiling + 10.0f), target.z};
}

// ---- Aviao ----

SkyPlane SkyPlane::fromLaunch(const GameEvent& launch) {
    SkyPlane plane;
    plane.serial = launch.value;
    plane.target = launch.target;
    plane.position = launch.position;
    return plane;
}

// Perseguicao pura: a cada passo o aviao aponta para o alvo. Como e bem mais rapido que qualquer passaro,
// sempre chega. O ponto mirado nunca fica abaixo de kPlaneFloor, entao quem desceu o bastante ve o aviao passar
// por cima.
bool SkyPlane::chase(const glm::vec3* target, float dt) {
    const bool first = age == 0;
    age += dt;
    if (!target || age > kPlaneApproachLimit) {
        depart();
        return false;
    }
    const glm::vec3 aim = planeAimFor(*target);
    const glm::vec3 toAim = aim - position;
    const float distance = glm::length(toAim);
    const float step = kPlaneSpeed * dt;
    if (distance <= step || glm::length(*target - position) <= kPlaneHitRadius) {
        if (distance <= step) position = aim;
        depart();
        return true;
    }
    const glm::vec3 desired = toAim / distance;
    const float before = yawOf(forward);
    forward = first ? desired : glm::normalize(glm::mix(forward, desired, 1.0f - std::exp(-kTurnRate * dt)));
    if (dt > 0) {
        const float turn = std::remainder(yawOf(forward) - before, 6.2831853f) / dt;
        roll = approach(roll, std::clamp(turn * kBankPerTurn, -kMaxBank, kMaxBank), 4.0f, dt);
    }
    position += desired * step;
    return false;
}

void SkyPlane::depart() {
    if (phase == SkyPlanePhase::Departing) return;
    phase = SkyPlanePhase::Departing;
    age = 0;
    forward.y = std::max(forward.y, kDepartClimb);
    forward = glm::normalize(forward);
}

void SkyPlane::fly(float dt) {
    age += dt;
    roll = approach(roll, 0.0f, 2.0f, dt);
    position += forward * kPlaneSpeed * dt;
}

// ---- Autoridade ----

// O aviao aparece a frente do alvo, ate 52 graus para um lado ou outro, para ser visto chegando.
bool SkyPlaneLedger::acceptLaunch(GameEvent& event, double now, const PlayerPublicState* target, bool checkTiming) {
    if (event.target >= kMaxPlayers) return false;
    Entry& entry = entries_[event.target];
    glm::vec3 where = event.position;
    if (checkTiming) {
        if (!target || target->position.y < kPlaneTriggerHeight - kPlaneLaunchSlack) return false;
        if (now - entry.launchedAt < kPlaneRelaunchInterval) return false;
        where = target->position;
    }
    serial_ = serial_ == 255 ? 1 : static_cast<uint8_t>(serial_ + 1);
    entry = Entry{true, false, serial_, now};

    const float offset = (static_cast<float>((serial_ * 37) % 101) / 100.0f - 0.5f) * 1.8f;
    const float angle = (target ? target->yaw : 0.0f) + offset;
    event.actor = kSkyPlaneActor;
    event.value = serial_;
    event.cashew = kNoCashew;
    event.position = where + glm::vec3(std::sin(angle), 0, -std::cos(angle)) * kPlaneSpawnDistance;
    event.position.y = std::max(where.y, kPlaneFloor) + kPlaneSpawnRise;
    return true;
}

bool SkyPlaneLedger::acceptStrike(GameEvent& event, double now, bool checkTiming) {
    if (event.target >= kMaxPlayers) return false;
    Entry& entry = entries_[event.target];
    if (!entry.active || entry.struck || entry.serial != event.value) return false;
    if (checkTiming && now - entry.launchedAt > kPlaneStrikeWindow) return false;
    entry.struck = true;
    event.actor = kSkyPlaneActor;
    event.cashew = kNoCashew;
    return true;
}

void SkyPlaneLedger::forget(PlayerId player) {
    if (player < kMaxPlayers) entries_[player] = Entry{};
}

GameEvent SkyPlaneLedger::cargoDropFor(const GameEvent& strike) {
    GameEvent drop;
    drop.type = GameEventType::CashewDropped;
    drop.actor = kSkyPlaneActor;
    drop.target = strike.target;
    drop.value = 255;   // Tudo: a autoridade corta no que o alvo carrega.
    drop.position = strike.position;
    return drop;
}

// ---- Cada computador ----

SkyPlane* SkyPlaneEvent::find(uint8_t serial) {
    for (SkyPlane& plane : planes_)
        if (plane.serial == serial) return &plane;
    return nullptr;
}

void SkyPlaneEvent::handle(const GameEvent& event) {
    if (event.type == GameEventType::PlaneLaunched) {
        if (event.target == localPlayer_) requestWait_ = 0;
        if (SkyPlane* old = find(event.value)) *old = SkyPlane::fromLaunch(event);   // Numero reaproveitado.
        else planes_.push_back(SkyPlane::fromLaunch(event));
        return;
    }
    if (event.type == GameEventType::PlaneStruck) {
        SkyPlane* plane = find(event.value);
        if (plane && plane->struck) return;   // O proprio dono ja aplicou a batida.
        if (plane) {
            plane->struck = true;
            plane->depart();
        }
        if (event.target != localPlayer_) impacts_.push_back(event.position);
        return;
    }
    if (event.type == GameEventType::PlayerLeft)
        for (SkyPlane& plane : planes_)
            if (plane.target == event.actor) plane.depart();
}

void SkyPlaneEvent::update(float dt, PlayerBird& bird, const std::vector<PlayerPublicState>& players,
                           GameAuthority& authority, bool perched) {
    cooldown_ = std::max(0.0f, cooldown_ - dt);
    requestWait_ = std::max(0.0f, requestWait_ - dt);
    hitNotice_ = std::max(0.0f, hitNotice_ - dt);
    if (stun_ > 0) tumble(bird, dt);

    const glm::vec3 own = bird.flight().position();
    for (SkyPlane& plane : planes_) {
        if (plane.phase != SkyPlanePhase::Approach) {
            plane.fly(dt);
            continue;
        }
        const bool mine = plane.target == localPlayer_;
        const PlayerPublicState* other = mine ? nullptr : findPlayer(players, plane.target);
        const glm::vec3* target = mine ? &own : other ? &other->position : nullptr;
        const bool passed = plane.chase(target, dt);
        if (!mine) continue;
        if (plane.phase == SkyPlanePhase::Departing) cooldown_ = kPlaneCooldown;
        if (passed && glm::length(plane.position - own) <= kPlaneHitRadius) strike(plane, bird, authority);
    }
    planes_.erase(std::remove_if(planes_.begin(), planes_.end(), [](const SkyPlane& p) { return p.gone(); }), planes_.end());
    requestPlane(bird, authority, perched, dt);
}

// So o dono pede, uma vez por subida: o pedido espera a resposta e depois o intervalo entre avioes.
void SkyPlaneEvent::requestPlane(const PlayerBird& bird, GameAuthority& authority, bool perched, float) {
    if (perched || requestWait_ > 0 || cooldown_ > 0 || incoming()) return;
    const glm::vec3 position = bird.flight().position();
    if (position.y < kPlaneTriggerHeight) return;
    GameEvent event;
    event.type = GameEventType::PlaneLaunched;
    event.actor = localPlayer_;
    event.target = localPlayer_;
    event.position = position;
    requestWait_ = kRequestRetry;
    authority.request(event);
}

// O tranco vale na hora, sem esperar a rede. A carga cai quando a autoridade confirmar a batida.
void SkyPlaneEvent::strike(SkyPlane& plane, PlayerBird& bird, GameAuthority& authority) {
    plane.struck = true;
    FlightModel& flight = bird.flight();
    glm::vec3 push(plane.forward.x, 0, plane.forward.z);
    if (glm::length(push) > 0.01f) push = glm::normalize(push);
    const glm::vec3 kept = flight.heading() * flight.speed() * kKeepSpeed;
    flight.setVelocity(kept + push * kPlaneKnockPush + glm::vec3(0, -kPlaneKnockDown, 0));
    bird.jolt(1.0f);
    stun_ = kPlaneStunSeconds;
    hitNotice_ = kHitNoticeSeconds;
    cooldown_ = kPlaneCooldown;
    impacts_.push_back(flight.position());

    GameEvent event;
    event.type = GameEventType::PlaneStruck;
    event.actor = localPlayer_;
    event.target = localPlayer_;
    event.value = plane.serial;
    event.position = flight.position();
    authority.request(event);
}

// Tonto, o passaro gira de um lado para o outro com o bico para baixo, cada vez menos.
void SkyPlaneEvent::tumble(PlayerBird& bird, float dt) {
    stun_ = std::max(0.0f, stun_ - dt);
    const float fade = stun_ / kPlaneStunSeconds;
    const float t = kPlaneStunSeconds - stun_;
    FlightModel& flight = bird.flight();
    flight.setAttitude(-0.6f * fade, 0.9f * std::sin(t * 10.0f) * fade);
    // O tranco nao some no planeio: o passaro continua despencando, cada vez menos, ate a tontura passar.
    flight.setVerticalSpeed(std::min(flight.verticalSpeed(), -kPlaneKnockDown * fade));
}

FlightControls SkyPlaneEvent::stunControls() const {
    FlightControls controls;
    controls.mode = FlightMode::Lost;   // Sem curva nem batida; a outros jogadores aparece planando.
    return controls;
}

bool SkyPlaneEvent::incoming() const {
    return std::any_of(planes_.begin(), planes_.end(), [&](const SkyPlane& p) {
        return p.target == localPlayer_ && p.phase == SkyPlanePhase::Approach;
    });
}

std::vector<glm::vec3> SkyPlaneEvent::takeImpacts() {
    std::vector<glm::vec3> impacts;
    impacts.swap(impacts_);
    return impacts;
}

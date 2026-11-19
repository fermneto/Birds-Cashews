#include "game/CashewCollector.h"
#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

namespace {
constexpr float kRequestTimeout = 3.0f;   // Permite pedir de novo se a resposta se perder na rede.
constexpr float kBeakRangeFactor = 2.0f;

void expire(std::map<CashewId, float>& pending, float dt) {
    for (auto it = pending.begin(); it != pending.end();) {
        it->second -= dt;
        it = it->second <= 0 ? pending.erase(it) : std::next(it);
    }
}
}

CashewCollector::CashewCollector(CashewField& field, GameAuthority& authority, PlayerId localPlayer)
    : field_(field), authority_(authority), localPlayer_(localPlayer) {}

void CashewCollector::handle(const GameEvent& event) {
    if (event.type == GameEventType::CashewCollected) applyCollected(event);
    if (event.type == GameEventType::CashewRespawned) {
        pendingRespawn_.erase(event.cashew);
        field_.markRespawned(event.cashew);
    }
}

// So o primeiro confirmado leva o caju: os seguintes o encontram indisponivel.
void CashewCollector::applyCollected(const GameEvent& event) {
    pendingCollect_.erase(event.cashew);
    if (!field_.markCollected(event.cashew)) return;
    if (event.actor == localPlayer_) carried_ = std::min(carried_ + 1, stats_.capacity);
}

void CashewCollector::update(float dt, glm::vec3 position) {
    field_.advance(dt);
    expire(pendingCollect_, dt);
    expire(pendingRespawn_, dt);
    requestNearby(position);
    requestRespawns();
}

// Pedidos em andamento reservam espaco, para a carga nunca passar do limite.
void CashewCollector::requestNearby(glm::vec3 position) {
    if (!collecting_) return;
    int free = stats_.capacity - carried_ - static_cast<int>(pendingCollect_.size());
    if (free <= 0) return;
    const float radius = stats_.collectRadius * stats_.collectRadius;
    std::vector<std::pair<float, CashewId>> inRange;
    for (size_t i = 0; i < field_.size(); i++) {
        const CashewId id = static_cast<CashewId>(i);
        if (!field_.isActive(id) || pendingCollect_.count(id)) continue;
        const glm::vec3 delta = field_.positionOf(id) - position;
        const float distance = glm::dot(delta, delta);
        if (distance <= radius) inRange.emplace_back(distance, id);
    }
    std::sort(inRange.begin(), inRange.end());
    for (size_t i = 0; i < inRange.size() && static_cast<int>(i) < free; i++) {
        pendingCollect_[inRange[i].second] = kRequestTimeout;
        send(GameEventType::CashewCollected, inRange[i].second);
    }
}

int CashewCollector::unload(int amount) {
    const int removed = std::clamp(amount, 0, carried_);
    carried_ -= removed;
    return removed;
}

void CashewCollector::requestRespawns() {
    for (CashewId id : field_.dueForRespawn()) {
        if (pendingRespawn_.count(id)) continue;
        pendingRespawn_[id] = kRequestTimeout;
        send(GameEventType::CashewRespawned, id);
    }
}

void CashewCollector::send(GameEventType type, CashewId id) {
    GameEvent event;
    event.type = type;
    event.actor = localPlayer_;
    event.cashew = id;
    event.value = 1;
    event.position = field_.positionOf(id);
    authority_.request(event);
}

float CashewCollector::beakTarget(glm::vec3 position) const {
    if (carried_ >= stats_.capacity) return 0.0f;
    return field_.nearest(position, stats_.collectRadius * kBeakRangeFactor) ? 1.0f : 0.0f;
}

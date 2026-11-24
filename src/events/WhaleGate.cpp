#include "events/WhaleGate.h"
#include "world/World.h"

namespace {
constexpr float kRequestSlack = 5.0f;    // O pedido sai assim que o passaro passa do limite.
constexpr float kStateSlack = 60.0f;     // O estado que a autoridade conhece chega uns 100 ms atrasado.
constexpr float kFarthest = World::kBoundaryRadius + 400.0f;   // Alem disso o pedido nao faz sentido.

float radiusOf(glm::vec3 position) { return glm::length(glm::vec2(position.x, position.z)); }
}

WhaleGate::WhaleGate() { armed_.fill(true); }

bool WhaleGate::accept(const GameEvent& event, const std::optional<glm::vec3>& known) {
    if (event.type != GameEventType::WhaleBite || event.target >= kMaxPlayers || event.actor != event.target) return false;
    if (!armed_[event.target]) return false;
    const float radius = radiusOf(event.position);
    if (radius < World::kBoundaryRadius - kRequestSlack || radius > kFarthest) return false;
    if (known && radiusOf(*known) < World::kBoundaryRadius - kStateSlack) return false;
    armed_[event.target] = false;
    return true;
}

void WhaleGate::observe(PlayerId id, glm::vec3 position) {
    if (id < kMaxPlayers && radiusOf(position) < World::kWarningRadius) armed_[id] = true;
}

void WhaleGate::forget(PlayerId id) {
    if (id < kMaxPlayers) armed_[id] = true;
}

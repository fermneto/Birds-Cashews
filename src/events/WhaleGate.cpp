#include "events/WhaleGate.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kRequestSlack = 5.0f;    // O pedido sai assim que o passaro passa do limite.
constexpr float kStateSlack = 60.0f;     // O estado que a autoridade conhece chega uns 100 ms atrasado.
constexpr float kFarthest = World::kBoundaryRadius + 400.0f;   // Alem disso o pedido nao faz sentido.

float radiusOf(glm::vec3 position) { return glm::length(glm::vec2(position.x, position.z)); }
}

// O salto e balistico: sai de 20 m de fundo e sobe ate o apice em arco, freando sob a gravidade, fecha a
// boca ali e cai de volta. Perto da agua o corpo bate primeiro e freia a cabeca ate a altura de mastigar.
WhaleTimeline whaleTimeline(float biteHeight) {
    constexpr float kDeep = -20.0f;       // Onde a boca comeca, debaixo da agua.
    constexpr float kChewHeight = 6.5f;   // Altura da boca enquanto mastiga.
    constexpr float kMouthAbove = 3.6f;   // O focinho fica acima do passaro preso entre as mandibulas.
    constexpr float kChewSeconds = 1.4f, kSpitBob = 0.35f, kSinkSeconds = 1.8f, kThrowSeconds = 2.5f;
    WhaleTimeline t;
    t.target = std::clamp(biteHeight, 14.0f, 480.0f);   // O voo tem teto em 420 m; acima disso o passaro so passa de leve.
    t.apex = t.target + kMouthAbove;
    const float rise = t.apex - kDeep;
    // Ate uns 75 m o salto rapido de sempre; nos mais altos a gravidade cai para o salto durar e ser visto.
    const float blend = std::clamp((rise - 95.0f) / (440.0f - 95.0f), 0.0f, 1.0f);
    t.gravity = 100.0f + (65.0f - 100.0f) * blend * blend * (3.0f - 2.0f * blend);
    t.bite = std::sqrt(2.0f * rise / t.gravity);
    t.breach = t.bite - std::sqrt(2.0f * t.apex / t.gravity);
    const float drop = t.apex - kChewHeight, braking = std::min(18.0f, 0.5f * drop);
    const float falling = std::sqrt(2.0f * (drop - braking) / t.gravity);
    t.brake = t.bite + falling;
    t.impactSpeed = t.gravity * falling;
    t.fallEnd = t.brake + 2.0f * braking / t.impactSpeed;   // Freada uniforme ate parar.
    t.spit = t.fallEnd + kChewSeconds;
    t.sinkStart = t.spit + kSpitBob;
    t.end = t.spit + kSinkSeconds;
    t.throwEnd = t.spit + kThrowSeconds;
    return t;
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

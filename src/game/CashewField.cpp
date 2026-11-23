#include "game/CashewField.h"
#include "entity/PlayerBird.h"
#include "world/Rng.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int kMinPerTree = 6, kMaxPerTree = 10, kAttemptsPerTree = 1500;
constexpr float kMinRespawn = 30.0f, kMaxRespawn = 60.0f;
constexpr float kMinSpacing = 16.0f;
constexpr float kFoliageBulge = 1.12f;     // Maior raio visivel de uma copa, por causa da rugosidade da malha.
constexpr float kStemDepth = 0.8f;         // Fracao do raio em que o talo ja esta escondido na copa.
constexpr float kHangGap = 7.5f;           // Folga entre a folhagem e o centro do caju.
constexpr float kFoliageMargin = 1.0f;
constexpr float kFruitTop = 3.75f, kFruitBottom = 5.6f, kFruitRadius = 3.0f;
constexpr float kProbeStep = 3.0f, kEscapeMargin = 25.0f;
constexpr int kMaxEscapeSteps = 200;

// Distancia no espaco em que a copa achatada vira uma esfera.
float canopyDistance(glm::vec3 point, const SphereCollider& sphere) {
    const glm::vec3 delta = point - sphere.center;
    return glm::length(glm::vec3(delta.x, delta.y / CashewTree::kCanopySquash, delta.z));
}

bool isFree(const World& world, glm::vec3 point, float radius) {
    return !world.resolve(point, radius).hit();
}

bool pointClearOfFoliage(const World& world, glm::vec3 point) {
    for (const CashewTree& tree : world.trees())
        for (const SphereCollider& sphere : tree.canopy())
            if (canopyDistance(point, sphere) < sphere.radius * kFoliageBulge + kFoliageMargin) return false;
    return true;
}

// O caju inteiro precisa aparecer, sem ficar enterrado em nenhuma copa.
bool fruitClearOfFoliage(const World& world, glm::vec3 center) {
    return pointClearOfFoliage(world, center) && pointClearOfFoliage(world, center + glm::vec3(0, kFruitTop, 0))
        && pointClearOfFoliage(world, center - glm::vec3(0, kFruitBottom, 0));
}

bool flightLineEscapes(const World& world, const CashewTree& tree, glm::vec3 start, glm::vec3 direction) {
    const glm::vec2 base(tree.base().x, tree.base().z);
    for (int i = 1; i <= kMaxEscapeSteps; i++) {
        const glm::vec3 point = start + direction * (kProbeStep * i);
        if (!isFree(world, point, PlayerBird::kRadius)) return false;
        if (glm::length(glm::vec2(point.x, point.z) - base) > tree.reach() + kEscapeMargin) return true;
    }
    return false;
}

// Um passaro vindo de fora da arvore precisa conseguir chegar voando em linha reta.
bool hasEscapeRoute(const World& world, const CashewTree& tree, glm::vec3 start) {
    const glm::vec3 offset(start.x - tree.base().x, 0, start.z - tree.base().z);
    if (glm::length(offset) < 1e-3f) return false;
    const glm::vec3 radial = glm::normalize(offset);
    for (float slope : {0.35f, 0.0f, -0.2f})
        if (flightLineEscapes(world, tree, start, glm::normalize(radial + glm::vec3(0, slope, 0)))) return true;
    return false;
}
}

CashewField::CashewField(const World& world, uint32_t seed) : world_(&world) {
    for (size_t i = 0; i < world.trees().size(); i++)
        growOnTree(world, i, (static_cast<uint64_t>(seed) << 20) ^ (i * 0x9E3779u + 0xCA5Eu));
    states_.resize(spots_.size());
}

CashewField::CashewField(std::vector<CashewSpot> spots) : spots_(std::move(spots)), states_(spots_.size()) {}

// Cada arvore tem a propria sequencia, entao uma arvore nao altera o sorteio das outras.
void CashewField::growOnTree(const World& world, size_t treeIndex, uint64_t treeSeed) {
    Rng rng(treeSeed);
    const CashewTree& tree = world.trees()[treeIndex];
    const std::vector<SphereCollider>& canopy = tree.canopy();
    const int wanted = rng.range(kMinPerTree, kMaxPerTree);
    int placed = 0;
    for (int attempt = 0; attempt < kAttemptsPerTree && placed < wanted; attempt++) {
        const SphereCollider& sphere = canopy[rng.range(0, static_cast<int>(canopy.size()) - 1)];
        // Da lateral para baixo, para o caju ficar pendurado e o talo subir ate a copa.
        const float angle = rng.range(0.0f, 6.2831853f), height = rng.range(-0.65f, 0.1f);
        const float ring = std::sqrt(1.0f - height * height);
        const glm::vec3 surface(ring * std::cos(angle), height * CashewTree::kCanopySquash, ring * std::sin(angle));
        const glm::vec3 position = sphere.center + surface * (sphere.radius * kFoliageBulge + kHangGap);

        const bool spaced = std::none_of(spots_.begin(), spots_.end(), [&](const CashewSpot& spot) {
            return glm::length(spot.position - position) < kMinSpacing;
        });
        if (!spaced || !fruitClearOfFoliage(world, position) || !isFree(world, position, kFruitRadius)
            || !hasEscapeRoute(world, tree, position)) continue;

        CashewSpot spot;
        spot.position = position;
        spot.yaw = rng.range(0.0f, 6.2831853f);
        spot.respawnDelay = rng.range(kMinRespawn, kMaxRespawn);
        spot.stemAnchor = sphere.center + surface * (sphere.radius * kStemDepth);
        spot.tree = treeIndex;
        spots_.push_back(spot);
        placed++;
    }
}

std::optional<CashewId> CashewField::nearest(glm::vec3 position, float radius) const {
    if (radius < 0) return std::nullopt;
    std::optional<CashewId> best;
    float bestDistance = radius * radius;
    const auto consider = [&](CashewId id) {
        const glm::vec3 delta = positionOf(id) - position;
        const float distance = glm::dot(delta, delta);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = id;
        }
    };
    for (size_t i = 0; i < spots_.size(); i++)
        if (states_[i].active) consider(static_cast<CashewId>(i));
    for (CashewId id : activeLoose()) consider(id);
    return best;
}

const LooseCashew* CashewField::loose(CashewId id) const {
    return isLooseCashew(id) ? &loose_[id - kLooseCashewBase] : nullptr;
}

bool CashewField::isActive(CashewId id) const {
    if (const LooseCashew* fallen = loose(id)) return fallen->active;
    return id < spots_.size() && states_[id].active;
}

// Queda livre ate o ponto de pouso, calculado uma vez na hora em que o caju sai.
glm::vec3 CashewField::positionOf(CashewId id) const {
    if (const LooseCashew* fallen = loose(id)) {
        const float t = static_cast<float>(clock_ - fallen->spilledAt);
        const float drop = fallen->from.y - fallen->rest.y;
        if (drop <= 0.0f) return fallen->rest;
        const float progress = std::min(1.0f, 0.5f * kFallSpeed * t * t / drop);
        return glm::mix(fallen->from, fallen->rest, progress);
    }
    return id < spots_.size() ? spots_[id].position : glm::vec3(0);
}

float CashewField::yawOf(CashewId id) const {
    if (const LooseCashew* fallen = loose(id)) return fallen->yaw;
    return id < spots_.size() ? spots_[id].yaw : 0.0f;
}

std::vector<CashewId> CashewField::activeLoose() const {
    std::vector<CashewId> ids;
    for (int i = 0; i < kLooseCashewSlots; i++)
        if (loose_[i].active) ids.push_back(static_cast<CashewId>(kLooseCashewBase + i));
    return ids;
}

void CashewField::advance(float dt) {
    clock_ += dt;
    for (LooseCashew& fallen : loose_)
        if (fallen.active && clock_ - fallen.spilledAt >= kLooseCashewSeconds) fallen.active = false;
}

// Desce de metro em metro ate encostar em chao, copa ou mar. A castanha fica apoiada, sem afundar.
glm::vec3 CashewField::restingPoint(glm::vec3 from) const {
    if (!world_) return from;
    constexpr float kStep = 1.0f, kLowest = -20.0f, kNutBelowCenter = kFruitBottom - kFruitRadius;
    for (glm::vec3 point = from; point.y > kLowest; point.y -= kStep) {
        glm::vec3 resolved = point;
        if (world_->resolve(resolved, kFruitRadius).hit()) return resolved + glm::vec3(0, kNutBelowCenter, 0);
    }
    return glm::vec3(from.x, kLowest, from.z);
}

// Um caju novo na mesma vaga substitui o antigo: a autoridade so reusa vagas de cajus que ja sumiram.
bool CashewField::collectableBy(CashewId id, PlayerId player) const {
    if (!isActive(id)) return false;
    const LooseCashew* fallen = loose(id);
    return !fallen || fallen->droppedBy != player || clock_ - fallen->spilledAt >= kRegrabSeconds;
}

bool CashewField::spill(CashewId id, glm::vec3 from, PlayerId droppedBy) {
    if (!isLooseCashew(id)) return false;
    LooseCashew& fallen = loose_[id - kLooseCashewBase];
    fallen.active = true;
    fallen.from = from;
    fallen.rest = restingPoint(from);
    fallen.yaw = static_cast<float>(id % 16) * 0.39f;
    fallen.spilledAt = clock_;
    fallen.droppedBy = droppedBy;
    return true;
}

bool CashewField::markCollected(CashewId id) {
    if (!isActive(id)) return false;
    if (isLooseCashew(id)) {
        loose_[id - kLooseCashewBase].active = false;   // Caju derrubado nao volta.
        return true;
    }
    states_[id].active = false;
    states_[id].respawnAt = clock_ + spots_[id].respawnDelay;
    return true;
}

// Um aviso atrasado de uma coleta anterior nao pode trazer o caju de volta antes da hora.
bool CashewField::markRespawned(CashewId id) {
    if (id >= spots_.size() || states_[id].active || clock_ < states_[id].respawnAt) return false;
    states_[id].active = true;
    return true;
}

// Em rede o relogio do host vale: o caju volta mesmo que o tempo local ainda nao tenha vencido.
bool CashewField::forceRespawn(CashewId id) {
    if (id >= spots_.size() || states_[id].active) return false;
    states_[id].active = true;
    return true;
}

std::vector<CashewId> CashewField::dueForRespawn() const {
    std::vector<CashewId> due;
    for (size_t i = 0; i < states_.size(); i++)
        if (!states_[i].active && clock_ >= states_[i].respawnAt) due.push_back(static_cast<CashewId>(i));
    return due;
}

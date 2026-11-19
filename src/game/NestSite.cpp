#include "game/NestSite.h"
#include "entity/PlayerBird.h"
#include "world/Rng.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace {
constexpr float kPerchHeight = 9.0f;        // Acima da borda do ninho em qualquer nivel de Tamanho.
constexpr float kFoliageBulge = 1.12f;      // Maior raio visivel de uma copa, por causa da rugosidade da malha.
constexpr float kProbeStep = 2.0f, kClearAbove = 130.0f, kEscapeMargin = 25.0f;
constexpr float kFullTurn = 6.2831853f;
constexpr int kAttemptsPerTree = 24, kHeadings = 12, kMaxPlayers = 4;

bool isFree(const World& world, glm::vec3 point) {
    return !world.resolve(point, PlayerBird::kRadius).hit();
}

// Altura visivel da folhagem neste ponto, ou infinito negativo se nao houver copa embaixo.
float foliageTop(const CashewTree& tree, float x, float z) {
    float top = -std::numeric_limits<float>::infinity();
    for (const SphereCollider& sphere : tree.canopy()) {
        const float radius = sphere.radius * kFoliageBulge;
        const float distance = glm::length(glm::vec2(x - sphere.center.x, z - sphere.center.z));
        if (distance >= radius) continue;
        const float rise = std::sqrt(radius * radius - distance * distance) * CashewTree::kCanopySquash;
        top = std::max(top, sphere.center.y + rise);
    }
    return top;
}

bool skyIsOpen(const World& world, glm::vec3 perch) {
    for (float rise = 0; rise <= kClearAbove; rise += kProbeStep)
        if (!isFree(world, perch + glm::vec3(0, rise, 0))) return false;
    return true;
}

// Um voo nivelado nessa direcao precisa sair da arvore sem bater em nada.
bool headingEscapes(const World& world, const CashewTree& tree, glm::vec3 perch, float yaw) {
    const glm::vec3 heading(std::sin(yaw), 0, -std::cos(yaw));
    const glm::vec2 base(tree.base().x, tree.base().z);
    for (float distance = 0;; distance += kProbeStep) {
        const glm::vec3 point = perch + heading * distance;
        if (!isFree(world, point)) return false;
        if (glm::length(glm::vec2(point.x, point.z) - base) > tree.reach() + kEscapeMargin) return true;
    }
}

bool findOpenHeading(const World& world, const CashewTree& tree, glm::vec3 perch, float start, float& yaw) {
    for (int i = 0; i < kHeadings; i++) {
        yaw = start + kFullTurn * i / kHeadings;
        if (headingEscapes(world, tree, perch, yaw)) return true;
    }
    return false;
}

// Copas mais altas primeiro: o ninho fica no ponto mais visivel da arvore.
std::vector<SphereCollider> canopyByHeight(const CashewTree& tree) {
    std::vector<SphereCollider> spheres = tree.canopy();
    std::stable_sort(spheres.begin(), spheres.end(), [](const SphereCollider& a, const SphereCollider& b) {
        return a.center.y + a.radius * CashewTree::kCanopySquash > b.center.y + b.radius * CashewTree::kCanopySquash;
    });
    return spheres;
}

bool placeOnTree(const World& world, size_t treeIndex, Rng& rng, NestSite& site) {
    const CashewTree& tree = world.trees()[treeIndex];
    const std::vector<SphereCollider> spheres = canopyByHeight(tree);
    for (int attempt = 0; attempt < kAttemptsPerTree; attempt++) {
        const SphereCollider& sphere = spheres[std::min<size_t>(attempt / 6, spheres.size() - 1)];
        const float angle = rng.range(0.0f, kFullTurn), spread = sphere.radius * rng.range(0.0f, 0.3f);
        const float x = sphere.center.x + std::cos(angle) * spread, z = sphere.center.z + std::sin(angle) * spread;
        const glm::vec3 base(x, foliageTop(tree, x, z), z);
        const glm::vec3 perch = base + glm::vec3(0, kPerchHeight, 0);
        float yaw = 0;
        if (!skyIsOpen(world, perch) || !findOpenHeading(world, tree, perch, rng.range(0.0f, kFullTurn), yaw)) continue;
        site = {treeIndex, base, perch, std::remainder(yaw, kFullTurn)};
        return true;
    }
    return false;
}

std::vector<size_t> shuffledTrees(size_t count, uint32_t seed) {
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), size_t{0});
    Rng rng(uint64_t{seed} ^ 0x4E5354u);
    for (size_t i = count; i > 1; i--) std::swap(order[i - 1], order[static_cast<size_t>(rng.next() % i)]);
    return order;
}
}

// A ordem embaralhada e a mesma para todos, entao cada jogador fica com uma arvore diferente.
NestSite chooseNestSite(const World& world, uint32_t seed, PlayerId player) {
    const size_t count = world.trees().size();
    const std::vector<size_t> order = shuffledTrees(count, seed);
    NestSite site;
    for (size_t step = 0; step < count; step++) {
        const size_t tree = order[(player + step * kMaxPlayers) % count];
        Rng rng((uint64_t{seed} << 16) ^ (uint64_t{player} << 8) ^ (tree * 0x9E3779u));
        if (placeOnTree(world, tree, rng, site)) return site;
    }
    // Ilha sem arvore utilizavel: o ninho fica no ar, no ponto de partida.
    site.tree = count;
    site.perch = world.spawnPoint();
    site.base = site.perch - glm::vec3(0, kPerchHeight, 0);
    return site;
}

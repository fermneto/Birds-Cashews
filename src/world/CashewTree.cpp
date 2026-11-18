#include "world/CashewTree.h"
#include "gfx/MeshBuilder.h"
#include "world/Rng.h"
#include <algorithm>
#include <cmath>

namespace {
const glm::vec3 kBark(0.40f, 0.29f, 0.21f);
const glm::vec3 kLeaves[] = {{0.20f, 0.50f, 0.20f}, {0.27f, 0.58f, 0.20f}, {0.16f, 0.44f, 0.22f}, {0.36f, 0.60f, 0.18f}};

glm::vec3 closestOnSegment(glm::vec3 p, glm::vec3 a, glm::vec3 b) {
    const glm::vec3 ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
    return a + ab * t;
}

// Empurra a posicao para fora de uma esfera e informa a direcao do empurrao.
bool pushOut(glm::vec3& position, glm::vec3 center, float minDistance, glm::vec3& normal) {
    const glm::vec3 delta = position - center;
    const float distance = glm::length(delta);
    if (distance >= minDistance) return false;
    normal = distance > 1e-4f ? delta / distance : glm::vec3(0, 1, 0);
    position = center + normal * minDistance;
    return true;
}

// Mesmo empurrao para uma copa achatada: calcula no espaco em que ela vira uma esfera.
bool pushOutOfCanopy(glm::vec3& position, const SphereCollider& sphere, float radius, glm::vec3& normal) {
    const float squash = CashewTree::kCanopySquash;
    const glm::vec3 delta = position - sphere.center;
    const glm::vec3 round(delta.x, delta.y / squash, delta.z);
    const float distance = glm::length(round), limit = sphere.radius * 0.94f + radius;
    if (distance >= limit) return false;
    const glm::vec3 direction = distance > 1e-4f ? round / distance : glm::vec3(0, 1, 0);
    position = sphere.center + glm::vec3(direction.x, direction.y * squash, direction.z) * limit;
    normal = glm::normalize(glm::vec3(direction.x, direction.y / squash, direction.z));
    return true;
}
}

CashewTree::CashewTree(glm::vec3 base, Rng& rng)
    : base_(base), height_(rng.range(120.0f, 190.0f)), trunkRadius_(height_ * rng.range(0.085f, 0.11f)) {
    growTrunk(rng);
    const int count = rng.range(6, 8);
    const float start = rng.range(0.0f, 6.28f);
    for (int i = 0; i < count; i++) growBranch(start + 6.2831853f * i / count + rng.range(-0.25f, 0.25f), rng);
    canopy_.push_back({base_ + glm::vec3(0, height_ * 0.8f, 0), height_ * 0.34f});
    reach_ += height_ * 0.05f;
}

void CashewTree::growTrunk(Rng& rng) {
    const glm::vec3 lean(rng.range(-0.08f, 0.08f), 0, rng.range(-0.08f, 0.08f));
    trunk_ = {base_ - glm::vec3(0, 6, 0)};
    for (float t : {0.14f, 0.28f, 0.44f}) trunk_.push_back(base_ + (glm::vec3(0, 1, 0) + lean * t) * (height_ * t));
    crown_ = trunk_.back();
    addCapsules(trunk_, trunkRadius_, trunkRadius_ * 0.62f);
}

void CashewTree::growBranch(float angle, Rng& rng) {
    const glm::vec3 direction(std::cos(angle), 0, std::sin(angle));
    const float length = height_ * rng.range(0.38f, 0.6f);
    const float rise = height_ * rng.range(0.58f, 0.76f);
    const glm::vec3 middle = base_ + direction * (length * 0.55f) + glm::vec3(0, height_ * 0.46f + (rise - height_ * 0.46f) * 0.75f, 0);
    const glm::vec3 tip = base_ + direction * length + glm::vec3(0, rise, 0);
    const std::vector<glm::vec3> points = {crown_, middle, tip};
    branches_.push_back(points);
    addCapsules(points, trunkRadius_ * 0.42f, trunkRadius_ * 0.16f);

    const float tipRadius = height_ * rng.range(0.2f, 0.27f);
    const glm::vec3 side(-direction.z, 0, direction.x);
    canopy_.push_back({tip, tipRadius});
    canopy_.push_back({middle + glm::vec3(0, height_ * 0.1f, 0), tipRadius * 0.95f});
    canopy_.push_back({glm::mix(middle, tip, 0.6f) + side * (tipRadius * rng.range(-0.9f, 0.9f)) + glm::vec3(0, height_ * 0.05f, 0),
                       tipRadius * rng.range(0.75f, 0.95f)});
    reach_ = std::max(reach_, length + tipRadius);
}

void CashewTree::addCapsules(const std::vector<glm::vec3>& points, float baseRadius, float tipRadius) {
    for (size_t i = 0; i + 1 < points.size(); i++) {
        const float t = static_cast<float>(i) / std::max<size_t>(1, points.size() - 2);
        capsules_.push_back({points[i], points[i + 1], baseRadius + (tipRadius - baseRadius) * t});
    }
}

bool CashewTree::resolve(glm::vec3& position, float radius, glm::vec3& normal) const {
    const glm::vec2 offset(position.x - base_.x, position.z - base_.z);
    if (glm::length(offset) > reach_ + radius) return false;
    bool hit = false;
    for (const auto& capsule : capsules_) {
        const glm::vec3 closest = closestOnSegment(position, capsule.a, capsule.b);
        hit = pushOut(position, closest, capsule.radius + radius, normal) || hit;
    }
    for (const auto& sphere : canopy_) hit = pushOutOfCanopy(position, sphere, radius, normal) || hit;
    return hit;
}

void CashewTree::appendMesh(MeshBuilder& mesh, Rng& rng) const {
    std::vector<float> radii;
    for (size_t i = 0; i < trunk_.size(); i++)
        radii.push_back(trunkRadius_ * (1.0f - 0.38f * i / (trunk_.size() - 1)) * (i == 0 ? 1.25f : 1.0f));
    mesh.tube(trunk_, radii, 9, kBark);
    for (const auto& branch : branches_)
        mesh.tube(branch, {trunkRadius_ * 0.42f, trunkRadius_ * 0.28f, trunkRadius_ * 0.14f}, 6, kBark * 0.92f);
    for (const auto& sphere : canopy_)
        mesh.blob(sphere.center, sphere.radius, kLeaves[rng.range(0, 3)], rng, 1, 0.12f, kCanopySquash);
}

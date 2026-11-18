#include "world/World.h"
#include "world/Rng.h"
#include <cmath>

World::World(uint32_t seed) : seed_(seed), island_(seed) { plantTrees(); }

// Sorteia posicoes em terra firme, mantendo distancia para sobrar espaco de voo.
void World::plantTrees() {
    Rng rng(seed_ ^ 0x7EE5u);
    constexpr int kWanted = 34;
    constexpr float kSpacing = 190.0f, kMaxRadius = 700.0f;
    for (int attempt = 0; attempt < 6000 && static_cast<int>(trees_.size()) < kWanted; attempt++) {
        const float angle = rng.range(0.0f, 6.2831853f), radius = kMaxRadius * std::sqrt(rng.unit());
        const glm::vec3 base(radius * std::cos(angle), 0, radius * std::sin(angle));
        const float ground = island_.heightAt(base.x, base.z);
        if (ground < 8.0f) continue;
        bool free = true;
        for (const auto& tree : trees_)
            free = free && glm::length(glm::vec2(tree.base().x - base.x, tree.base().z - base.z)) >= kSpacing;
        if (free) trees_.emplace_back(glm::vec3(base.x, ground, base.z), rng);
    }
}

Contact World::resolve(glm::vec3& position, float radius) const {
    Contact contact;
    glm::vec3 normal(0, 1, 0);
    for (const auto& tree : trees_) {
        if (tree.resolve(position, radius, normal)) {
            contact.surface = Surface::Solid;
            contact.normal = normal;
        }
    }
    const float ground = island_.heightAt(position.x, position.z);
    const float floor = std::max(ground, 0.0f) + radius;
    if (position.y < floor) {
        position.y = floor;
        contact.surface = ground > 0 ? Surface::Ground : Surface::Water;
        contact.normal = ground > 0 ? island_.normalAt(position.x, position.z) : glm::vec3(0, 1, 0);
    }
    return contact;
}

glm::vec3 World::spawnPoint() const {
    const float x = 0, z = 620;
    return {x, std::max(island_.heightAt(x, z), 0.0f) + 190.0f, z};
}

void World::appendScenery(MeshBuilder& mesh, Rng& rng) const {
    for (int i = 0; i < 260; i++) {
        const float x = rng.range(-850.0f, 850.0f), z = rng.range(-850.0f, 850.0f);
        const float ground = island_.heightAt(x, z);
        if (ground < 9.0f) continue;
        const bool rock = rng.unit() < 0.3f;
        const glm::vec3 color = rock ? glm::vec3(0.55f, 0.53f, 0.50f) : glm::vec3(0.24f, 0.50f, 0.22f);
        mesh.blob({x, ground + 1.5f, z}, rng.range(3.0f, 8.0f), color, rng, rock ? 0 : 1, 0.2f);
    }
}

MeshBuilder World::buildCloudMesh() const {
    MeshBuilder mesh;
    Rng rng(seed_ ^ 0xC10Du);
    for (int i = 0; i < 30; i++) {
        const glm::vec3 center(rng.range(-2000.0f, 2000.0f), rng.range(480.0f, 640.0f), rng.range(-2000.0f, 2000.0f));
        const int puffs = rng.range(5, 9);
        for (int p = 0; p < puffs; p++) {
            const glm::vec3 offset(rng.range(-110.0f, 110.0f), rng.range(-4.0f, 6.0f), rng.range(-55.0f, 55.0f));
            mesh.blob(center + offset, rng.range(30.0f, 62.0f), glm::vec3(1.0f), rng, 1, 0.12f, 0.38f);
        }
    }
    return mesh;
}

MeshBuilder World::buildMesh() const {
    MeshBuilder mesh;
    Rng rng(seed_ ^ 0x51CEu);
    island_.appendMesh(mesh, seed_);
    for (const auto& tree : trees_) tree.appendMesh(mesh, rng);
    appendScenery(mesh, rng);
    return mesh;
}

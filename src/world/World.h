#pragma once
#include "gfx/MeshBuilder.h"
#include "world/CashewTree.h"
#include "world/Island.h"
#include <cstdint>
#include <vector>

enum class Surface { None, Ground, Water, Solid };

struct Contact {
    Surface surface = Surface::None;
    glm::vec3 normal{0, 1, 0};
    bool hit() const { return surface != Surface::None; }
};

// Mundo estatico gerado pela semente: relevo, arvores e cenario.
class World {
public:
    static constexpr float kBoundaryRadius = 1150.0f;
    static constexpr float kWarningRadius = 1050.0f;

    explicit World(uint32_t seed);

    Contact resolve(glm::vec3& position, float radius) const;
    MeshBuilder buildMesh() const;
    MeshBuilder buildCloudMesh() const;
    glm::vec3 spawnPoint() const;

    const Island& island() const { return island_; }
    const std::vector<CashewTree>& trees() const { return trees_; }

private:
    void plantTrees();
    void appendScenery(MeshBuilder& mesh, Rng& rng) const;

    uint32_t seed_;
    Island island_;
    std::vector<CashewTree> trees_;
};

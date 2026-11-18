#pragma once
#include "world/Noise.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class MeshBuilder;

// Relevo da ilha em uma grade regular. A altura consultada coincide com a malha desenhada.
class Island {
public:
    static constexpr int kCells = 220;
    static constexpr float kCellSize = 10.0f;
    static constexpr float kHalfSize = kCells * kCellSize * 0.5f;

    explicit Island(uint32_t seed);
    float heightAt(float x, float z) const;
    glm::vec3 normalAt(float x, float z) const;
    float gridHeight(int i, int j) const { return heights_[index(i, j)]; }
    void appendMesh(MeshBuilder& mesh, uint32_t seed) const;

private:
    static int index(int i, int j) { return j * (kCells + 1) + i; }
    float generateHeight(float x, float z, const ValueNoise& noise) const;
    glm::vec3 pointAt(int i, int j) const;
    glm::vec3 surfaceColor(float height, float slope, float tint) const;

    std::vector<float> heights_;
};

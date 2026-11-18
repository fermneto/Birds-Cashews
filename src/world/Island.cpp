#include "world/Island.h"
#include "gfx/MeshBuilder.h"
#include "world/Rng.h"
#include <algorithm>
#include <cmath>

namespace {
float smooth(float low, float high, float x) {
    const float t = std::clamp((x - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
}

Island::Island(uint32_t seed) : heights_((kCells + 1) * (kCells + 1)) {
    const ValueNoise noise(seed);
    for (int j = 0; j <= kCells; j++)
        for (int i = 0; i <= kCells; i++)
            heights_[index(i, j)] = generateHeight(i * kCellSize - kHalfSize, j * kCellSize - kHalfSize, noise);
}

// O contorno varia com o angulo; a altura sobe do fundo do mar ate as colinas centrais.
float Island::generateHeight(float x, float z, const ValueNoise& noise) const {
    const float radius = std::sqrt(x * x + z * z);
    const float angle = std::atan2(z, x);
    const float coast = 820.0f + 170.0f * (noise.fractal(std::cos(angle) * 1.6f + 9, std::sin(angle) * 1.6f + 9, 3) - 0.5f);
    const float inland = (coast - radius) / 320.0f;
    const float hills = std::pow(noise.fractal(x / 380.0f + 40, z / 380.0f + 40, 4), 1.5f);
    const float land = 3.0f + smooth(0.0f, 1.0f, inland) * (6.0f + 110.0f * hills);
    return -28.0f + (28.0f + land) * smooth(-0.35f, 0.12f, inland);
}

float Island::heightAt(float x, float z) const {
    const float gx = std::clamp((x + kHalfSize) / kCellSize, 0.0f, kCells - 0.001f);
    const float gz = std::clamp((z + kHalfSize) / kCellSize, 0.0f, kCells - 0.001f);
    const int i = static_cast<int>(gx), j = static_cast<int>(gz);
    const float fx = gx - i, fz = gz - j;
    const float h00 = gridHeight(i, j), h10 = gridHeight(i + 1, j);
    const float h01 = gridHeight(i, j + 1), h11 = gridHeight(i + 1, j + 1);
    if (fx >= fz) return h00 + fx * (h10 - h00) + fz * (h11 - h10);
    return h00 + fz * (h01 - h00) + fx * (h11 - h01);
}

glm::vec3 Island::normalAt(float x, float z) const {
    const float step = kCellSize * 0.5f;
    return glm::normalize(glm::vec3(heightAt(x - step, z) - heightAt(x + step, z), 2 * step,
                                    heightAt(x, z - step) - heightAt(x, z + step)));
}

glm::vec3 Island::pointAt(int i, int j) const {
    return {i * kCellSize - kHalfSize, gridHeight(i, j), j * kCellSize - kHalfSize};
}

glm::vec3 Island::surfaceColor(float height, float slope, float tint) const {
    if (height < 7.0f) return glm::vec3(0.90f, 0.81f, 0.58f) * tint;
    if (slope < 0.8f) return glm::vec3(0.52f, 0.47f, 0.40f) * tint;
    const glm::vec3 low(0.47f, 0.66f, 0.27f), high(0.30f, 0.52f, 0.25f);
    return glm::mix(low, high, std::clamp((height - 7.0f) / 60.0f, 0.0f, 1.0f)) * tint;
}

void Island::appendMesh(MeshBuilder& mesh, uint32_t seed) const {
    Rng rng(seed ^ 0xA5A5u);
    const auto addFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        mesh.triangle(a, b, c, surfaceColor((a.y + b.y + c.y) / 3.0f, normal.y, rng.range(0.93f, 1.05f)));
    };
    for (int j = 0; j < kCells; j++) {
        for (int i = 0; i < kCells; i++) {
            const glm::vec3 p00 = pointAt(i, j), p10 = pointAt(i + 1, j);
            const glm::vec3 p01 = pointAt(i, j + 1), p11 = pointAt(i + 1, j + 1);
            addFace(p00, p11, p10);
            addFace(p00, p01, p11);
        }
    }
}

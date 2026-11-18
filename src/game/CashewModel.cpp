#include "game/CashewModel.h"
#include "world/Rng.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace {
constexpr float kBoundingRadius = 8.0f;

struct Ring {
    float y, radius;
    glm::vec3 color;
};

const glm::vec3 kYellow(0.98f, 0.78f, 0.16f), kOrange(0.97f, 0.46f, 0.10f), kRed(0.88f, 0.13f, 0.11f);
const glm::vec3 kDarkRed(0.68f, 0.09f, 0.10f), kNut(0.50f, 0.37f, 0.23f), kStem(0.33f, 0.25f, 0.15f);

// Perfil de cima para baixo. A cor de cada faixa vem do anel de cima.
template <size_t N>
void addProfile(MeshBuilder& mesh, const std::array<Ring, N>& rings, int sides) {
    for (size_t i = 0; i + 1 < rings.size(); i++) {
        const std::vector<glm::vec3> points = {{0, rings[i].y, 0}, {0, rings[i + 1].y, 0}};
        mesh.tube(points, {rings[i].radius, rings[i + 1].radius}, sides, rings[i].color);
    }
}
}

MeshBuilder buildCashewMesh() {
    const std::array<Ring, 9> rings = {{{3.75f, 0.02f, kYellow}, {3.6f, 0.6f, kYellow}, {2.6f, 1.4f, kYellow},
                                        {1.2f, 1.95f, kOrange}, {-0.2f, 2.4f, kRed}, {-1.4f, 2.5f, kRed},
                                        {-2.3f, 1.85f, kDarkRed}, {-2.8f, 0.75f, kDarkRed}, {-2.95f, 0.02f, kDarkRed}}};
    MeshBuilder mesh;
    addProfile(mesh, rings, 8);
    // Dois volumes sobrepostos formam o desenho de rim da castanha.
    Rng rng(0xCA5Eu);
    mesh.blob({-0.25f, -3.85f, 0}, 1.45f, kNut, rng, 1, 0.05f);
    mesh.blob({0.85f, -4.6f, 0}, 1.0f, kNut * 0.9f, rng, 1, 0.05f);
    return mesh;
}

// Mesma silhueta e mesmas cores com bem menos triangulos.
MeshBuilder buildCashewFarMesh() {
    const std::array<Ring, 5> rings = {{{3.75f, 0.02f, kYellow}, {2.4f, 1.5f, kYellow}, {0.6f, 2.3f, kOrange},
                                        {-1.4f, 2.5f, kRed}, {-2.95f, 0.02f, kRed}}};
    MeshBuilder mesh;
    addProfile(mesh, rings, 5);
    Rng rng(0xCA5Eu);
    mesh.blob({0.1f, -4.1f, 0}, 1.7f, kNut, rng, 0, 0.0f);
    return mesh;
}

MeshBuilder buildCashewStems(const CashewField& field) {
    MeshBuilder mesh;
    for (const CashewSpot& spot : field.spots())
        mesh.tube({spot.position + glm::vec3(0, 3.5f, 0), spot.stemAnchor}, {0.1f, 0.7f}, 5, kStem);   // Ponta fina, sem furo aparente.
    return mesh;
}

std::vector<glm::mat4> visibleCashews(const CashewField& field, const glm::mat4& viewProjection, glm::vec3 camera,
                                      float minDistance, float maxDistance) {
    const glm::mat4 rows = glm::transpose(viewProjection);
    const std::array<glm::vec4, 5> planes = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
                                             rows[3] - rows[1], rows[3] + rows[2]};
    const auto inFrustum = [&](glm::vec3 point) {
        for (const glm::vec4& plane : planes)
            if (glm::dot(glm::vec3(plane), point) + plane.w < -kBoundingRadius * glm::length(glm::vec3(plane))) return false;
        return true;
    };
    std::vector<glm::mat4> visible;
    for (size_t i = 0; i < field.size(); i++) {
        const CashewSpot& spot = field.spots()[i];
        const float distance = glm::length(spot.position - camera);
        if (!field.isActive(static_cast<CashewId>(i)) || distance < minDistance || distance >= maxDistance) continue;
        if (!inFrustum(spot.position)) continue;
        visible.push_back(glm::rotate(glm::translate(glm::mat4(1.0f), spot.position), spot.yaw, glm::vec3(0, 1, 0)));
    }
    return visible;
}

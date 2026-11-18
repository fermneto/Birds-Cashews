#include "procedural/BirdFactoryTests.h"
#include "core/TestRun.h"
#include "shared/BirdFactory.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
struct Bounds {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};
};

Bounds boundsOf(const std::vector<Vertex>& vertices) {
    Bounds bounds;
    for (const auto& vertex : vertices) {
        bounds.min = glm::min(bounds.min, vertex.position);
        bounds.max = glm::max(bounds.max, vertex.position);
    }
    return bounds;
}

bool validVertex(const Vertex& vertex) {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(vertex.position[axis]) || !std::isfinite(vertex.normal[axis])
            || !(vertex.color[axis] >= 0 && vertex.color[axis] <= 1)) return false;
    }
    return std::abs(glm::length(vertex.normal) - 1.0f) < 1e-4f;
}

bool sameVertex(const Vertex& a, const Vertex& b) {
    return a.position == b.position && a.normal == b.normal && a.color == b.color;
}
}

int runBirdFactorySelfTest() {
    TestRun run;
    constexpr std::array<uint32_t, 5> seeds = {0, 1, 17, 42, std::numeric_limits<uint32_t>::max()};
    bool repeatable = true, healthy = true, symmetric = true;
    for (uint32_t seed : seeds) {
        const auto mesh = buildBirdMesh(seed), repeat = buildBirdMesh(seed);
        const auto& vertices = mesh.vertices();
        repeatable &= std::equal(vertices.begin(), vertices.end(),
                                 repeat.vertices().begin(), repeat.vertices().end(), sameVertex);
        healthy &= vertices.size() >= 192 && vertices.size() % 3 == 0
                   && std::all_of(vertices.begin(), vertices.end(), validVertex);
        const auto bounds = boundsOf(vertices);
        symmetric &= std::abs(bounds.min.x + bounds.max.x) < 1e-5f
                     && bounds.max.x > 0.5f && bounds.min.z < -0.5f && bounds.max.z > 0.5f;
    }
    const auto first = buildBirdMesh(17), second = buildBirdMesh(42);
    const auto a = boundsOf(first.vertices()), b = boundsOf(second.vertices());
    run.check("passaro procedural: malha e cores repetem com a mesma semente", repeatable);
    run.check("passaro procedural: triangulos validos e normais unitarias", healthy);
    run.check("passaro procedural: envergadura simetrica e corpo no eixo Z", symmetric);
    run.check("passaro procedural: sementes variadas mudam a silhueta e a paleta",
              glm::length((a.max - a.min) - (b.max - b.min)) > 0.15f
              && glm::length(first.vertices().front().color - second.vertices().front().color) > 0.15f);
    return run.failures;
}

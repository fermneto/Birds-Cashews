#include "procedural/BirdFactoryTests.h"
#include "core/TestRun.h"
#include "shared/BirdFactory.h"
#include "world/Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

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

bool sameMesh(const MeshBuilder& a, const MeshBuilder& b) {
    return std::equal(a.vertices().begin(), a.vertices().end(), b.vertices().begin(), b.vertices().end(), sameVertex);
}

float saturation(glm::vec3 color) {
    const float high = std::max({color.r, color.g, color.b}), low = std::min({color.r, color.g, color.b});
    return high > 0 ? (high - low) / high : 0.0f;
}

// Coeficiente de variacao: desvio padrao dividido pela media.
float spreadOf(const std::vector<float>& values) {
    float mean = 0, variance = 0;
    for (float value : values) mean += value;
    mean /= static_cast<float>(values.size());
    for (float value : values) variance += (value - mean) * (value - mean);
    return std::sqrt(variance / static_cast<float>(values.size())) / mean;
}

std::string withValue(const char* name, float value) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s (%.3f)", name, value);
    return text;
}

// Assinatura do genoma em inteiros: se mudar, outro computador ja desenharia outro passaro.
uint64_t genomeSignature(uint32_t seed) {
    const BirdTraits t = describeBird(seed);
    uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](float value) {
        hash ^= static_cast<uint64_t>(static_cast<int64_t>(std::lround(value * 10000.0f)));
        hash *= 1099511628211ULL;
    };
    for (float value : {t.length, t.width, t.height, t.head, t.beak, t.beakDepth, t.crest, t.tail,
                        t.tailSpread, t.span, t.chord}) mix(value);
    for (const glm::vec3& color : t.colors) { mix(color.r); mix(color.g); mix(color.b); }
    return hash;
}
}

int runBirdFactorySelfTest() {
    TestRun run;
    constexpr std::array<uint32_t, 5> seeds = {0, 1, 17, 42, std::numeric_limits<uint32_t>::max()};
    bool repeatable = true, healthy = true, symmetric = true;
    for (uint32_t seed : seeds) {
        const auto mesh = buildBirdMesh(seed);
        const auto parts = buildBirdFlightParts(seed), partsAgain = buildBirdFlightParts(seed);
        const auto& vertices = mesh.vertices();
        repeatable &= sameMesh(mesh, buildBirdMesh(seed)) && sameMesh(parts.body, partsAgain.body)
                      && sameMesh(parts.leftWing, partsAgain.leftWing) && sameMesh(parts.rightWing, partsAgain.rightWing);
        healthy &= vertices.size() >= 192 && vertices.size() % 3 == 0
                   && std::all_of(vertices.begin(), vertices.end(), validVertex)
                   && !parts.leftWing.vertices().empty() && parts.wingspan > 1.0f;
        const auto bounds = boundsOf(vertices);
        symmetric &= std::abs(bounds.min.x + bounds.max.x) < 1e-4f
                     && bounds.max.x > 0.5f && bounds.min.z < -0.4f && bounds.max.z > 0.4f;
    }
    run.check("passaro procedural: malha, asas e cores repetem com a mesma semente", repeatable);
    run.check("passaro procedural: triangulos validos e normais unitarias", healthy);
    run.check("passaro procedural: envergadura simetrica e corpo no eixo Z", symmetric);

    // O genoma so usa o Rng inteiro e contas basicas: estes valores sao os mesmos em qualquer maquina.
    bool stable = true;
    for (uint32_t seed : seeds) stable &= genomeSignature(seed) == genomeSignature(seed);
    run.check("passaro procedural: genoma igual ao de referencia (mesma semente, mesmo passaro em qualquer PC)",
              stable && genomeSignature(42) == 13778815140951925231ULL);

    // Diversidade medida em 300 sementes, como chegariam dos jogadores e dos NPCs.
    constexpr int kSamples = 300;
    std::vector<BirdTraits> birds;
    Rng rng(0xD1CEu);
    for (int i = 0; i < kSamples; ++i) birds.push_back(describeBird(static_cast<uint32_t>(rng.next() >> 32)));
    float paletteDistance = 0, closest = 0;
    int pairs = 0, twins = 0, contrasted = 0, saturated = 0;
    for (int i = 0; i < kSamples; ++i) {
        float nearest = std::numeric_limits<float>::max();
        for (int j = 0; j < kSamples; ++j) {
            if (i == j) continue;
            nearest = std::min(nearest, birdLookDistance(birds[i], birds[j]));
            if (j > i) {
                float colors = 0;
                for (size_t k = 0; k < birds[i].colors.size(); ++k) colors += glm::length(birds[i].colors[k] - birds[j].colors[k]);
                paletteDistance += colors / static_cast<float>(birds[i].colors.size());
                ++pairs;
            }
        }
        closest += nearest;
        if (nearest < 0.15f) ++twins;
        float strongest = 0;
        for (const auto& a : birds[i].colors)
            for (const auto& b : birds[i].colors) strongest = std::max(strongest, glm::length(a - b));
        if (strongest > 0.55f) ++contrasted;
        const float vivid = std::max({saturation(birds[i].colors[0]), saturation(birds[i].colors[1]), saturation(birds[i].colors[3])});
        if (vivid > 0.6f) ++saturated;
    }
    paletteDistance /= static_cast<float>(pairs);
    run.check(withValue("passaro procedural: paletas distantes entre si (media RGB por zona)", paletteDistance),
              paletteDistance > 0.45f);
    run.check(withValue("passaro procedural: vizinho mais parecido ainda e diferente (media)", closest / kSamples),
              closest / kSamples > 0.35f);
    run.check(withValue("passaro procedural: quase gemeos sao raros (fracao)", static_cast<float>(twins) / kSamples),
              twins <= kSamples / 50);
    run.check(withValue("passaro procedural: contraste forte entre zonas do mesmo passaro (fracao)",
                        static_cast<float>(contrasted) / kSamples), contrasted >= kSamples * 95 / 100);
    run.check(withValue("passaro procedural: cores saturadas (fracao)", static_cast<float>(saturated) / kSamples),
              saturated >= kSamples * 95 / 100);

    std::vector<float> length, girth, head, beak, tail, span, chord;
    for (const auto& bird : birds) {
        length.push_back(bird.length);
        girth.push_back(bird.width / bird.length);
        head.push_back(bird.head);
        beak.push_back(bird.beak);
        tail.push_back(bird.tail);
        span.push_back(bird.span / bird.length);
        chord.push_back(bird.chord / bird.length);
    }
    const float smallest = std::min({spreadOf(length), spreadOf(girth), spreadOf(head), spreadOf(beak),
                                     spreadOf(tail), spreadOf(span), spreadOf(chord)});
    run.check(withValue("passaro procedural: corpo, cabeca, bico, cauda e asas variam (menor coef. de variacao)", smallest),
              smallest > 0.10f && spreadOf(beak) > 0.4f && spreadOf(tail) > 0.3f);

    const auto first = buildBirdMesh(17), second = buildBirdMesh(42);
    const auto a = boundsOf(first.vertices()), b = boundsOf(second.vertices());
    run.check("passaro procedural: sementes variadas mudam a silhueta e a paleta",
              glm::length((a.max - a.min) - (b.max - b.min)) > 0.15f
              && birdLookDistance(describeBird(17), describeBird(42)) > 0.3f);
    return run.failures;
}

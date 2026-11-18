#include "shared/BirdFactory.h"
#include "world/Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace {
enum class Body { Streamlined, Compact, Full };
enum class Beak { Short, Long, Hooked };
enum class Crest { Low, Upright, Swept };
enum class Tail { Fan, Forked, Narrow };
enum class Wing { Broad, Pointed, Rounded };

struct Genome {
    float length, width, height, span, hue;
    Beak beak;
    Crest crest;
    Tail tail;
    Wing wing;
    Body body;
};

Genome makeGenome(uint32_t seed) {
    Rng rng(seed);
    const float length = rng.range(1.5f, 2.6f);
    return {length, length * rng.range(0.17f, 0.26f), length * rng.range(0.20f, 0.29f),
            length * rng.range(0.85f, 1.45f), rng.unit(),
            static_cast<Beak>(rng.range(0, 2)), static_cast<Crest>(rng.range(0, 2)),
            static_cast<Tail>(rng.range(0, 2)), static_cast<Wing>(rng.range(0, 2)),
            static_cast<Body>(rng.range(0, 2))};
}

glm::vec3 hsv(float hue, float saturation, float value) {
    const float sector = (hue - std::floor(hue)) * 6.0f;
    const glm::vec3 channels{0, 4, 2};
    glm::vec3 color;
    for (int i = 0; i < 3; ++i) {
        const float distance = std::abs(std::fmod(sector + channels[i], 6.0f) - 3.0f);
        color[i] = value * (1.0f - saturation * (1.0f - std::clamp(distance - 1.0f, 0.0f, 1.0f)));
    }
    return color;
}

struct Palette {
    glm::vec3 body, belly, feathers, accent, beak;
};

Palette makePalette(float hue) {
    return {hsv(hue, 0.72f, 0.69f), hsv(hue + 0.06f, 0.29f, 0.94f),
            hsv(hue - 0.05f, 0.65f, 0.42f), hsv(hue + 0.44f, 0.72f, 0.93f),
            hsv(hue + 0.48f, 0.61f, 0.70f)};
}

void outwardFace(MeshBuilder& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                 glm::vec3 inside, glm::vec3 color) {
    const glm::vec3 center = (a + b + c) / 3.0f;
    if (glm::dot(glm::cross(b - a, c - a), center - inside) < 0) std::swap(b, c);
    mesh.triangle(a, b, c, color);
}

struct Section {
    glm::vec3 center;
    float halfWidth, halfHeight;
};

// O contorno octogonal fica no plano XY, que e perpendicular ao eixo do voo (-Z).
using Ring = std::array<glm::vec3, 8>;
Ring makeRing(const Section& section) {
    constexpr float d = 0.70710678f;
    const std::array<glm::vec2, 8> octagon = {{{1, 0}, {d, d}, {0, 1}, {-d, d},
                                              {-1, 0}, {-d, -d}, {0, -1}, {d, -d}}};
    Ring points;
    for (std::size_t i = 0; i < points.size(); ++i) {
        points[i] = section.center + glm::vec3(octagon[i].x * section.halfWidth,
                                              octagon[i].y * section.halfHeight, 0);
    }
    return points;
}

void appendBody(MeshBuilder& mesh, const Genome& bird, const Palette& palette) {
    const float l = bird.length, w = bird.width, h = bird.height;
    float neckHeight = 0.69f, rumpWidth = 0.82f, rumpHeight = 0.76f;
    switch (bird.body) {
        case Body::Streamlined: neckHeight = 0.58f; rumpWidth = 0.66f; rumpHeight = 0.59f; break;
        case Body::Compact: neckHeight = 0.83f; rumpWidth = 0.72f; rumpHeight = 0.62f; break;
        case Body::Full: rumpWidth = 0.94f; rumpHeight = 0.89f; break;
    }
    const std::array<Section, 6> sections = {{
        {{0, 0.32f * h, -0.65f * l}, 0, 0},
        {{0, 0.38f * h, -0.53f * l}, 0.50f * w, 0.54f * h},
        {{0, 0.21f * h, -0.31f * l}, 0.68f * w, neckHeight * h},
        {{0, 0, -0.13f * l}, w, h},
        {{0, 0.03f * h, 0.24f * l}, rumpWidth * w, rumpHeight * h},
        {{0, 0.09f * h, 0.53f * l}, 0, 0}
    }};
    Ring front = makeRing(sections.front());
    for (std::size_t i = 1; i < sections.size(); ++i) {
        const Ring back = makeRing(sections[i]);
        const glm::vec3 inside = (sections[i - 1].center + sections[i].center) * 0.5f;
        for (std::size_t j = 0; j < front.size(); ++j) {
            const auto next = (j + 1) % front.size();
            const auto color = j < 4 ? palette.body : palette.belly;
            outwardFace(mesh, front[j], back[j], back[next], inside, color);
            outwardFace(mesh, front[j], back[next], front[next], inside, color);
        }
        front = back;
    }
}

// Forma fina em duas faces, com espessura central para iluminar as facetas.
void appendPlume(MeshBuilder& mesh, std::initializer_list<glm::vec3> rim,
                 glm::vec3 ridge, glm::vec3 underside, const glm::vec3 color, const glm::vec3 shade) {
    const glm::vec3 inside = (ridge + underside) * 0.5f;
    const glm::vec3* edge = rim.begin();
    for (std::size_t i = 0; i < rim.size(); ++i) {
        const glm::vec3 a = edge[i], b = edge[(i + 1) % rim.size()];
        outwardFace(mesh, a, b, ridge, inside, color);
        outwardFace(mesh, b, a, underside, inside, shade);
    }
}

void appendBeak(MeshBuilder& mesh, const Genome& bird, const Palette& color) {
    const glm::vec3 base(0, 0.30f * bird.height, -0.58f * bird.length);
    float reach = 0.22f, height = 0.28f, drop = 0.03f;
    switch (bird.beak) {
        case Beak::Short: reach = 0.15f; height = 0.40f; break;
        case Beak::Long: reach = 0.34f; height = 0.18f; break;
        case Beak::Hooked: drop = 0.65f; break;
    }
    const glm::vec3 left = base + glm::vec3(-0.39f * bird.width, 0, 0);
    const glm::vec3 right = base + glm::vec3(0.39f * bird.width, 0, 0);
    const glm::vec3 tip = base + glm::vec3(0, -drop * bird.height, -reach * bird.length);
    appendPlume(mesh, {left, tip, right, base + glm::vec3(0, 0, 0.12f * bird.length)},
                base + glm::vec3(0, height * bird.height, -0.04f * bird.length),
                base + glm::vec3(0, -0.19f * bird.height, -0.04f * bird.length),
                color.beak, color.beak * 0.73f);
}

void appendCrest(MeshBuilder& mesh, const Genome& bird, const Palette& color) {
    const glm::vec3 base(0, 0.79f * bird.height, -0.46f * bird.length);
    float height = 0.45f, sweep = 0.06f;
    switch (bird.crest) {
        case Crest::Low: height = 0.25f; break;
        case Crest::Upright: height = 1.02f; break;
        case Crest::Swept: height = 0.74f; sweep = 0.32f; break;
    }
    const glm::vec3 tip = base + glm::vec3(0, height * bird.height, sweep * bird.length);
    appendPlume(mesh, {base, tip, base + glm::vec3(0, -0.13f * bird.height, 0.34f * bird.length)},
                base + glm::vec3(0.18f * bird.width, 0.02f * bird.height, 0.10f * bird.length),
                base + glm::vec3(-0.18f * bird.width, 0.02f * bird.height, 0.10f * bird.length),
                color.accent, color.accent * 0.72f);
}

void appendTail(MeshBuilder& mesh, const Genome& bird, const Palette& color) {
    const glm::vec3 base(0, 0.05f * bird.height, 0.40f * bird.length);
    float spread = 1.1f, middle = 0.60f, outer = 0.55f;
    switch (bird.tail) {
        case Tail::Fan: spread = 1.45f; break;
        case Tail::Forked: middle = 0.30f; outer = 0.78f; break;
        case Tail::Narrow: spread = 0.64f; middle = 0.86f; break;
    }
    const auto point = [&](float x, float y, float z) {
        return base + glm::vec3(x * bird.width, y * bird.height, z * bird.length);
    };
    appendPlume(mesh, {point(-0.28f, 0, 0), point(-spread, 0.18f, outer),
                       point(0, 0.18f, middle), point(spread, 0.18f, outer), point(0.28f, 0, 0)},
                point(0, 0.19f, 0.18f), point(0, -0.13f, 0.18f),
                color.feathers, color.accent);
}

void appendWing(MeshBuilder& mesh, const Genome& bird, const Palette& color, float side) {
    const glm::vec3 root(side * 0.72f * bird.width, 0.17f * bird.height, -0.13f * bird.length);
    float sweep = 0.53f, chord = 0.39f, shoulder = 0.76f;
    switch (bird.wing) {
        case Wing::Broad: chord = 0.56f; shoulder = 0.86f; break;
        case Wing::Pointed: sweep = 0.79f; chord = 0.32f; shoulder = 0.64f; break;
        case Wing::Rounded: break;
    }
    const auto point = [&](float x, float y, float z) {
        return root + glm::vec3(side * x * bird.span, y * bird.height, z * bird.length);
    };
    appendPlume(mesh, {point(0, 0, -0.22f), point(0.38f, 0.15f, -0.24f),
                       point(shoulder, 0.23f, sweep * 0.37f), point(1, 0.12f, sweep),
                       point(0.68f, 0.08f, sweep + 0.12f), point(0.51f, 0, chord + 0.17f),
                       point(0.25f, -0.08f, chord), point(0, 0, 0.22f)},
                point(0.35f, 0.38f, 0.16f), point(0.35f, -0.17f, 0.16f),
                color.feathers, color.belly);
}

void appendLeg(MeshBuilder& mesh, const Genome& bird, const Palette& color, float side) {
    const glm::vec3 hip(side * 0.44f * bird.width, -0.63f * bird.height, 0.06f * bird.length);
    const glm::vec3 ankle = hip + glm::vec3(0, -0.62f * bird.height, 0.10f * bird.length);
    const float radius = 0.09f * bird.width;
    mesh.tube({hip, ankle}, {radius * 1.4f, radius}, 5, color.beak);
    for (float toe : {-1.0f, 0.0f, 1.0f}) {
        const glm::vec3 tip = ankle + glm::vec3(toe * 0.20f * bird.width, 0, -0.14f * bird.length);
        mesh.tube({ankle, tip}, {radius * 0.82f, radius * 0.12f}, 5, color.beak);
    }
}

void appendEye(MeshBuilder& mesh, const Genome& bird, Rng& rng, float side) {
    const glm::vec3 center(side * 0.50f * bird.width, 0.50f * bird.height, -0.50f * bird.length);
    mesh.blob(center, 0.17f * bird.width, {0.05f, 0.05f, 0.05f}, rng, 0, 0);
    const glm::vec3 glint = center + glm::vec3(side * 0.13f, 0.06f, -0.02f) * bird.width;
    mesh.blob(glint, 0.043f * bird.width, {0.90f, 0.90f, 0.90f}, rng, 0, 0);
}
}

MeshBuilder buildBirdMesh(uint32_t seed) {
    const Genome bird = makeGenome(seed);
    const Palette palette = makePalette(bird.hue);
    MeshBuilder mesh;
    appendBody(mesh, bird, palette);
    appendBeak(mesh, bird, palette);
    appendCrest(mesh, bird, palette);
    appendTail(mesh, bird, palette);
    for (float side : {-1.0f, 1.0f}) {
        appendWing(mesh, bird, palette, side);
        appendLeg(mesh, bird, palette, side);
        Rng eyeRng(seed);
        appendEye(mesh, bird, eyeRng, side);
    }
    return mesh;
}

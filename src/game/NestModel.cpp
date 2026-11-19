#include "game/NestModel.h"
#include "game/NestEconomy.h"
#include "world/Rng.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr float kFullTurn = 6.2831853f;
constexpr float kBowlRadius = 12.0f, kRimHeight = 3.6f, kFloorHeight = 1.0f;
constexpr float kMastHeight = 34.0f, kBeaconGap = 9.0f;
constexpr float kBeaconHideDistance = 45.0f, kBeaconFullDistance = 90.0f, kBeaconGrowth = 170.0f, kBeaconMaxScale = 6.0f;
constexpr int kRingSides = 14, kBaseTwigLayers = 3, kTwigsPerLayer = 16, kStruts = 6;

const glm::vec3 kOuterStraw(0.47f, 0.32f, 0.17f), kInnerStraw(0.70f, 0.55f, 0.28f), kFloorStraw(0.58f, 0.43f, 0.22f);
const glm::vec3 kTwigDark(0.33f, 0.22f, 0.13f), kTwigLight(0.62f, 0.46f, 0.25f);
const glm::vec3 kMastWhite(0.95f, 0.93f, 0.86f), kFlagRed(0.95f, 0.18f, 0.12f), kFlagYellow(0.99f, 0.76f, 0.14f);
const glm::vec3 kBeaconTop(1.0f, 0.82f, 0.18f), kBeaconBottom(1.0f, 0.42f, 0.10f);

glm::vec3 around(glm::vec3 center, float angle, float radius, float height) {
    return center + glm::vec3(std::cos(angle) * radius, height, std::sin(angle) * radius);
}

// Faixa conica entre dois aneis horizontais.
void addBand(MeshBuilder& mesh, glm::vec3 center, float lowRadius, float lowY, float highRadius, float highY, glm::vec3 color) {
    for (int i = 0; i < kRingSides; i++) {
        const float a = kFullTurn * i / kRingSides, b = kFullTurn * (i + 1) / kRingSides;
        const float shade = i % 2 == 0 ? 1.0f : 0.9f;
        mesh.quad(around(center, a, lowRadius, lowY), around(center, a, highRadius, highY),
                  around(center, b, highRadius, highY), around(center, b, lowRadius, lowY), color * shade);
    }
}

void addBowl(MeshBuilder& mesh, glm::vec3 base, float radius, float rim) {
    addBand(mesh, base, radius * 0.3f, -7.0f, radius * 0.86f, kFloorHeight, kTwigDark);
    addBand(mesh, base, radius * 0.86f, kFloorHeight, radius, rim, kOuterStraw);
    addBand(mesh, base, radius * 0.82f, rim, radius, rim, kTwigLight);
    addBand(mesh, base, radius * 0.56f, kFloorHeight, radius * 0.82f, rim, kInnerStraw);
    addBand(mesh, base, 0.01f, kFloorHeight, radius * 0.56f, kFloorHeight, kFloorStraw);
}

// Cada nivel de Tamanho acrescenta uma camada de gravetos na borda.
void addTwigs(MeshBuilder& mesh, glm::vec3 base, float radius, float rim, int layers, float scale, Rng& rng) {
    for (int layer = 0; layer < layers; layer++) {
        const float height = rim * (0.35f + 0.65f * layer / std::max(layers - 1, 1));
        for (int i = 0; i < kTwigsPerLayer; i++) {
            const float angle = kFullTurn * (i + rng.range(-0.3f, 0.3f)) / kTwigsPerLayer;
            const float span = rng.range(0.25f, 0.42f), reach = radius * rng.range(0.93f, 1.07f);
            const glm::vec3 from = around(base, angle - span, reach, height + rng.range(-0.5f, 0.5f));
            const glm::vec3 to = around(base, angle + span, reach, height + rng.range(-0.5f, 0.5f));
            const float thickness = rng.range(0.35f, 0.6f) * scale;
            mesh.tube({from, to}, {thickness, thickness * 0.7f}, 4, i % 3 == 0 ? kTwigLight : kTwigDark);
        }
    }
}

// Escoras escondem o vao entre o ninho e a folhagem irregular.
void addStruts(MeshBuilder& mesh, glm::vec3 base, float radius, Rng& rng) {
    for (int i = 0; i < kStruts; i++) {
        const float angle = kFullTurn * (i + rng.range(-0.2f, 0.2f)) / kStruts;
        mesh.tube({around(base, angle, radius * 0.75f, 1.0f), around(base, angle, radius * 0.3f, -16.0f)}, {1.1f, 0.6f}, 5, kTwigDark);
    }
}

// Mastro atras do poleiro, com faixas e bandeira, fora da visao de quem esta pousado.
void addMast(MeshBuilder& mesh, glm::vec3 foot, float height, glm::vec3 side, float scale) {
    constexpr int kBands = 5;
    for (int i = 0; i < kBands; i++) {
        const glm::vec3 low = foot + glm::vec3(0, height * i / kBands, 0), high = foot + glm::vec3(0, height * (i + 1) / kBands, 0);
        mesh.tube({low, high}, {0.75f * scale, 0.6f * scale}, 6, i % 2 == 0 ? kFlagRed : kMastWhite);
    }
    const glm::vec3 top = foot + glm::vec3(0, height, 0), drop(0, -9.0f * scale, 0);
    const glm::vec3 tip = top + side * (16.0f * scale) + drop * 0.5f;
    mesh.triangle(top, top + drop, tip, kFlagRed);
    mesh.triangle(top + drop * 0.3f, top + drop * 0.7f, top + side * (9.0f * scale) + drop * 0.5f, kFlagYellow);
}

glm::vec3 behindPerch(const NestSite& site, float radius) {
    return site.base + glm::vec3(-std::sin(site.yaw), 0, std::cos(site.yaw)) * (radius * 0.9f);
}
}

MeshBuilder buildNestMesh(const NestSite& site, int sizeLevel) {
    const float scale = nestScale(sizeLevel), radius = kBowlRadius * scale, rim = kRimHeight * scale;
    MeshBuilder mesh;
    Rng rng(0x4E1570u);
    addStruts(mesh, site.base, radius, rng);
    addBowl(mesh, site.base, radius, rim);
    addTwigs(mesh, site.base, radius, rim, kBaseTwigLayers + sizeLevel, scale, rng);
    const glm::vec3 side(std::cos(site.yaw), 0, std::sin(site.yaw));
    addMast(mesh, behindPerch(site, radius) + glm::vec3(0, rim * 0.5f, 0), kMastHeight * scale + rim * 0.5f, side, scale);
    return mesh;
}

MeshBuilder buildNestBeaconMesh() {
    const glm::vec3 top(0, 7.5f, 0), bottom(0, -7.5f, 0);
    MeshBuilder mesh;
    for (int i = 0; i < 6; i++) {
        const float a = kFullTurn * i / 6, b = kFullTurn * (i + 1) / 6;
        const glm::vec3 p(std::cos(a) * 5.0f, 0, std::sin(a) * 5.0f), q(std::cos(b) * 5.0f, 0, std::sin(b) * 5.0f);
        mesh.triangle(top, q, p, kBeaconTop);
        mesh.triangle(bottom, p, q, kBeaconBottom);
    }
    return mesh;
}

glm::mat4 nestBeaconTransform(const NestSite& site, int sizeLevel, glm::vec3 camera, float time) {
    const float nest = nestScale(sizeLevel);
    const float height = (kRimHeight + kMastHeight) * nest + kBeaconGap;
    const glm::vec3 center = behindPerch(site, kBowlRadius * nest) + glm::vec3(0, height, 0);
    const float distance = glm::length(center - camera);
    if (distance < kBeaconHideDistance) return glm::mat4(0.0f);
    const float appear = std::clamp((distance - kBeaconHideDistance) / (kBeaconFullDistance - kBeaconHideDistance), 0.0f, 1.0f);
    const float scale = appear * std::clamp(distance / kBeaconGrowth, 1.0f, kBeaconMaxScale);
    const glm::mat4 placed = glm::translate(glm::mat4(1.0f), center + glm::vec3(0, std::sin(time * 1.6f) * 1.5f, 0));
    return glm::scale(glm::rotate(placed, time * 1.2f, glm::vec3(0, 1, 0)), glm::vec3(scale));
}

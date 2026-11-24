#include "events/SkyPlaneModel.h"
#include "gfx/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
const glm::vec3 kWhite(0.93f, 0.93f, 0.90f), kRed(0.82f, 0.17f, 0.13f), kGlass(0.20f, 0.33f, 0.52f);
const glm::vec3 kDark(0.16f, 0.16f, 0.18f), kGrey(0.55f, 0.56f, 0.58f);
constexpr float kPropellerZ = -7.15f;
constexpr float kPropellerTurns = 38.0f;   // Radianos por segundo: borrao de helice.
constexpr float kPlaneScale = 1.5f;        // Um pouco maior que um aviao de turismo, para ser visto de longe.

// Bloco de 8 cantos: os 4 primeiros formam a face de baixo e os 4 ultimos a de cima, na mesma ordem.
void slab(MeshBuilder& mesh, const std::array<glm::vec3, 8>& c, glm::vec3 color) {
    mesh.quad(c[0], c[1], c[2], c[3], color);
    mesh.quad(c[4], c[7], c[6], c[5], color);
    for (int i = 0; i < 4; i++) {
        const int n = (i + 1) % 4;
        mesh.quad(c[i], c[i + 4], c[n + 4], c[n], color);
    }
}

// Superficie fina e afilada, como asa ou leme: corda na raiz e na ponta, de z0 a z1, com espessura.
// A raiz fica em "root" e a ponta em "tip"; os dois pontos marcam o bordo de ataque (menor z).
void surface(MeshBuilder& mesh, glm::vec3 root, float rootChord, glm::vec3 tip, float tipChord, glm::vec3 thickness,
             glm::vec3 color) {
    const glm::vec3 back(0, 0, 1);
    const glm::vec3 h = thickness * 0.5f;
    slab(mesh, {root - h, root + back * rootChord - h, tip + back * tipChord - h, tip - h,
                root + h, root + back * rootChord + h, tip + back * tipChord + h, tip + h}, color);
}

// Asa com diedro e ponta vermelha, espelhada para os dois lados.
void addWings(MeshBuilder& mesh) {
    for (float side : {-1.0f, 1.0f}) {
        const glm::vec3 root(side * 0.9f, -0.35f, -3.5f), stripe(side * 7.0f, 0.05f, -3.15f), tip(side * 8.5f, 0.15f, -3.0f);
        surface(mesh, root, 2.5f, stripe, 1.55f, {0, 0.26f, 0}, kWhite);
        surface(mesh, stripe, 1.55f, tip, 1.3f, {0, 0.2f, 0}, kRed);
        // Profundor na cauda.
        surface(mesh, {side * 0.4f, 0.45f, 4.8f}, 1.6f, {side * 3.2f, 0.55f, 5.4f}, 0.9f, {0, 0.14f, 0}, kWhite);
    }
}

void addTail(MeshBuilder& mesh) {
    // Leme vertical: base larga junto a fuselagem, topo estreito e recuado.
    const float t = 0.08f;
    slab(mesh, {glm::vec3(-t, 0.8f, 4.1f), glm::vec3(-t, 0.8f, 6.6f), glm::vec3(-t, 3.1f, 6.7f), glm::vec3(-t, 3.1f, 5.7f),
                glm::vec3(t, 0.8f, 4.1f), glm::vec3(t, 0.8f, 6.6f), glm::vec3(t, 3.1f, 6.7f), glm::vec3(t, 3.1f, 5.7f)},
         kRed);
}

void addCockpit(MeshBuilder& mesh) {
    const float base = 0.95f, top = 1.55f;
    slab(mesh, {glm::vec3(-0.75f, base, -3.9f), glm::vec3(0.75f, base, -3.9f), glm::vec3(0.75f, base, -1.2f),
                glm::vec3(-0.75f, base, -1.2f), glm::vec3(-0.45f, top, -3.0f), glm::vec3(0.45f, top, -3.0f),
                glm::vec3(0.45f, top, -1.5f), glm::vec3(-0.45f, top, -1.5f)},
         kGlass);
}

// Rodas fixas sob a asa, como num aviao pequeno de turismo.
void addGear(MeshBuilder& mesh) {
    for (float side : {-1.0f, 1.0f}) {
        mesh.tube({{side * 1.3f, -0.6f, -3.0f}, {side * 1.6f, -1.7f, -3.0f}}, {0.1f, 0.1f}, 4, kGrey);
        mesh.tube({{side * 1.75f, -1.9f, -3.0f}, {side * 1.45f, -1.9f, -3.0f}}, {0.38f, 0.38f}, 6, kDark);
    }
}

float pitchOf(glm::vec3 forward) { return std::asin(std::clamp(forward.y, -1.0f, 1.0f)); }
}

MeshBuilder buildSkyPlaneMesh() {
    MeshBuilder mesh;
    // Fuselagem: mais alta no meio, afinando ate a cauda levantada.
    mesh.tube({{0, 0.75f, 6.8f}, {0, 0.45f, 4.2f}, {0, 0.15f, 1.0f}, {0, 0.1f, -2.0f}, {0, 0.05f, -4.8f}, {0, 0.05f, -5.9f}},
              {0.3f, 0.75f, 1.1f, 1.2f, 1.05f, 0.8f}, 7, kWhite);
    // Faixa vermelha ao longo da fuselagem e capo do motor.
    mesh.tube({{0, 0.05f, -5.9f}, {0, 0.05f, -6.7f}}, {0.8f, 0.55f}, 7, kRed);
    mesh.tube({{0, 0.05f, -6.7f}, {0, 0.05f, -7.3f}}, {0.42f, 0.04f}, 7, kGrey);
    mesh.tube({{0, -0.55f, 3.6f}, {0, -0.25f, -4.6f}}, {0.62f, 0.95f}, 7, kRed);
    addWings(mesh);
    addTail(mesh);
    addCockpit(mesh);
    addGear(mesh);
    return mesh;
}

MeshBuilder buildSkyPlanePropeller() {
    MeshBuilder mesh;
    for (float side : {-1.0f, 1.0f}) {
        const glm::vec3 r(side * 2.1f, 0, 0);
        slab(mesh, {glm::vec3(0, -0.12f, -0.04f), r + glm::vec3(0, -0.22f, -0.04f), r + glm::vec3(0, 0.08f, -0.04f),
                    glm::vec3(0, 0.12f, -0.04f), glm::vec3(0, -0.12f, 0.04f), r + glm::vec3(0, -0.22f, 0.04f),
                    r + glm::vec3(0, 0.08f, 0.04f), glm::vec3(0, 0.12f, 0.04f)},
             kDark);
    }
    return mesh;
}

glm::mat4 skyPlaneTransform(const SkyPlane& plane) {
    const float yaw = std::atan2(plane.forward.x, -plane.forward.z);
    auto model = glm::translate(glm::mat4(1.0f), plane.position);
    model = glm::rotate(model, -yaw, glm::vec3(0, 1, 0));
    model = glm::rotate(model, pitchOf(plane.forward), glm::vec3(1, 0, 0));
    model = glm::rotate(model, -plane.roll, glm::vec3(0, 0, 1));
    return glm::scale(model, glm::vec3(kPlaneScale));
}

glm::mat4 skyPlanePropellerTransform(const SkyPlane& plane, float time) {
    const glm::mat4 nose = glm::translate(skyPlaneTransform(plane), glm::vec3(0, 0.05f, kPropellerZ));
    return glm::rotate(nose, time * kPropellerTurns + plane.serial, glm::vec3(0, 0, 1));
}

void showSkyPlanes(Renderer& renderer, const std::vector<SkyPlane>& planes, float time) {
    std::vector<glm::mat4> bodies, propellers;
    for (const SkyPlane& plane : planes) {
        bodies.push_back(skyPlaneTransform(plane));
        propellers.push_back(skyPlanePropellerTransform(plane, time));
    }
    renderer.setSkyPlanes(std::move(bodies), std::move(propellers));
}

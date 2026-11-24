#include "gfx/WhaleView.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>

namespace {
constexpr float kPi = 3.14159265f;
const glm::vec3 kBack(0.22f, 0.30f, 0.40f), kBelly(0.84f, 0.86f, 0.84f), kMouth(0.78f, 0.36f, 0.42f);
const glm::vec3 kTongue(0.66f, 0.24f, 0.30f), kTeeth(0.97f, 0.95f, 0.88f), kEye(0.06f, 0.06f, 0.08f);
const glm::vec3 kThroat(0.22f, 0.04f, 0.07f);
constexpr int kRingSides = 10, kHalfSides = 5;

// Secao do corpo: elipse com metade de cima e de baixo de alturas diferentes.
struct Section {
    float z, width, top, bottom, center;
};

glm::vec3 ringPoint(const Section& s, float angle) {
    const float sine = std::sin(angle);
    return {s.width * std::cos(angle), s.center + sine * (sine >= 0 ? s.top : s.bottom), s.z};
}

// Liga duas secoes por faixas. A cor sai da altura de cada faixa: dorso escuro, barriga clara.
void loft(MeshBuilder& mesh, const Section& a, const Section& b, float from, float to, int sides) {
    for (int i = 0; i < sides; i++) {
        const float a0 = from + (to - from) * i / sides, a1 = from + (to - from) * (i + 1) / sides;
        const glm::vec3 color = std::sin((a0 + a1) * 0.5f) < -0.35f ? kBelly : kBack;
        mesh.quad(ringPoint(a, a0), ringPoint(b, a0), ringPoint(b, a1), ringPoint(a, a1), color);
    }
}

void fan(MeshBuilder& mesh, glm::vec3 apex, const std::vector<glm::vec3>& rim, glm::vec3 color) {
    for (size_t i = 0; i + 1 < rim.size(); i++) mesh.triangle(apex, rim[i], rim[i + 1], color);
}

void strip(MeshBuilder& mesh, const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b, glm::vec3 color) {
    for (size_t i = 0; i + 1 < a.size(); i++) mesh.quad(a[i], b[i], b[i + 1], a[i + 1], color);
}

// Ceu da boca ou lingua: curva de uma borda a outra, funda no meio. depth positivo sobe.
std::vector<glm::vec3> mouthCurve(float z, float width, float depth) {
    return {{-width, 0, z}, {-width * 0.5f, depth * 0.6f, z}, {0, depth, z}, {width * 0.5f, depth * 0.6f, z}, {width, 0, z}};
}

std::vector<glm::vec3> halfRing(const Section& s, float from) {
    std::vector<glm::vec3> points;
    for (int i = 0; i <= kHalfSides; i++) points.push_back(ringPoint(s, from + kPi * i / kHalfSides));
    return points;
}

void octahedron(MeshBuilder& mesh, glm::vec3 c, float r, glm::vec3 color) {
    const glm::vec3 x(r, 0, 0), y(0, r, 0), z(0, 0, r);
    for (const glm::vec3& v : {y, -y})
        for (int i = 0; i < 4; i++) {
            const glm::vec3 around[] = {x, z, -x, -z};
            mesh.triangle(c + v, c + around[i], c + around[(i + 1) % 4], color);
        }
}

// Dente em cruz, visto de qualquer lado. tip e a ponta (para baixo em cima, para cima embaixo).
void tooth(MeshBuilder& mesh, glm::vec3 root, float tip) {
    const glm::vec3 point = root + glm::vec3(0, tip, 0);
    mesh.triangle(root + glm::vec3(-0.35f, 0, 0), root + glm::vec3(0.35f, 0, 0), point, kTeeth);
    mesh.triangle(root + glm::vec3(0, 0, -0.35f), root + glm::vec3(0, 0, 0.35f), point, kTeeth);
}

void teeth(MeshBuilder& mesh, const Section* sections, int count, float tip) {
    for (float z = -1.2f; z > WhaleView::kHingeZ + 2.0f; z -= 1.5f) {
        int i = 0;
        while (i + 2 < count && sections[i + 1].z > z) i++;
        const float u = (z - sections[i].z) / (sections[i + 1].z - sections[i].z);
        const float width = glm::mix(sections[i].width, sections[i + 1].width, u) * 0.86f;
        for (float side : {-1.0f, 1.0f}) tooth(mesh, {side * width, 0, z}, tip);
    }
}

// Cabeca de cima, da ponta do focinho a dobradica, do mais a frente para tras.
const Section kHead[] = {{0.0f, 2.2f, 1.6f, 0, 0}, {-3.0f, 3.6f, 3.4f, 0, 0}, {-8.0f, 4.6f, 5.0f, 0, 0}, {-13.0f, 5.0f, 6.0f, 0, 0}};
const Section kJaw[] = {{-0.4f, 2.0f, 0, 1.0f, 0}, {-3.0f, 3.4f, 0, 2.3f, 0}, {-8.0f, 4.4f, 0, 3.8f, 0}, {-13.0f, 5.0f, 0, 5.0f, 0}};
const Section kBody[] = {{-13.0f, 5.0f, 6.0f, 5.0f, 0.0f}, {-20.0f, 5.6f, 6.2f, 5.6f, 0.2f}, {-28.0f, 5.0f, 5.4f, 4.8f, 0.5f},
                         {-36.0f, 3.4f, 3.6f, 3.0f, 0.9f}, {-42.0f, 1.8f, 1.8f, 1.4f, 1.1f}, {-46.0f, 0.7f, 0.7f, 0.6f, 1.2f}};
constexpr float kPalate = 0.7f, kFloor = 0.65f;
}

MeshBuilder WhaleView::buildBody() {
    MeshBuilder mesh;
    constexpr int bodyCount = sizeof(kBody) / sizeof(kBody[0]);
    for (int i = 0; i + 1 < bodyCount; i++) loft(mesh, kBody[i], kBody[i + 1], 0, 2 * kPi, kRingSides);
    std::vector<glm::vec3> tail;
    for (int i = 0; i <= kRingSides; i++) tail.push_back(ringPoint(kBody[bodyCount - 1], 2 * kPi * i / kRingSides));
    fan(mesh, {0, 1.2f, -46.6f}, tail, kBack);

    // Cabeca: dorso, ceu da boca e o focinho fechando na frente.
    constexpr int headCount = sizeof(kHead) / sizeof(kHead[0]);
    for (int i = 0; i + 1 < headCount; i++) {
        loft(mesh, kHead[i], kHead[i + 1], 0, kPi, kHalfSides);
        strip(mesh, mouthCurve(kHead[i].z, kHead[i].width, kPalate * kHead[i].top),
              mouthCurve(kHead[i + 1].z, kHead[i + 1].width, kPalate * kHead[i + 1].top), kMouth);
    }
    const glm::vec3 snout(0, 0.8f, 0.6f);
    fan(mesh, snout, halfRing(kHead[0], 0), kBack);
    fan(mesh, snout, mouthCurve(0, kHead[0].width, kPalate * kHead[0].top), kMouth);
    teeth(mesh, kHead, headCount, -0.9f);

    // Garganta escura no fundo da boca, entre o ceu da boca e a lingua.
    std::vector<glm::vec3> throat = mouthCurve(kHingeZ, 5.0f, kPalate * 6.0f);
    const std::vector<glm::vec3> floor = mouthCurve(kHingeZ, 5.0f, -kFloor * 5.0f);
    throat.insert(throat.end(), floor.rbegin(), floor.rend());
    throat.push_back(throat.front());
    fan(mesh, {0, 0, kHingeZ - 0.4f}, throat, kThroat);

    for (float side : {-1.0f, 1.0f}) {
        octahedron(mesh, {side * 4.85f, 2.0f, -10.0f}, 0.55f, kEye);
        // Nadadeira peitoral, longa e caida para tras.
        mesh.quad({side * 5.2f, -2.0f, -15.5f}, {side * 13.5f, -6.0f, -21.0f}, {side * 12.5f, -6.6f, -23.0f},
                  {side * 5.2f, -2.6f, -19.5f}, kBack);
        // Metade da cauda.
        mesh.quad({0, 1.2f, -44.0f}, {side * 6.5f, 1.1f, -47.0f}, {side * 10.0f, 1.0f, -50.5f}, {0, 1.2f, -46.6f}, kBack);
    }
    mesh.triangle({0, 4.9f, -28.0f}, {0, 7.2f, -33.5f}, {0, 4.0f, -34.5f}, kBack);   // Barbatana do dorso.
    return mesh;
}

MeshBuilder WhaleView::buildJaw() {
    MeshBuilder mesh;
    constexpr int count = sizeof(kJaw) / sizeof(kJaw[0]);
    for (int i = 0; i + 1 < count; i++) {
        loft(mesh, kJaw[i], kJaw[i + 1], kPi, 2 * kPi, kHalfSides);
        strip(mesh, mouthCurve(kJaw[i].z, kJaw[i].width, -kFloor * kJaw[i].bottom),
              mouthCurve(kJaw[i + 1].z, kJaw[i + 1].width, -kFloor * kJaw[i + 1].bottom), kTongue);
    }
    const glm::vec3 chin(0, -0.4f, 0.0f);
    fan(mesh, chin, halfRing(kJaw[0], kPi), kBelly);
    fan(mesh, chin, mouthCurve(kJaw[0].z, kJaw[0].width, -kFloor * kJaw[0].bottom), kTongue);
    teeth(mesh, kJaw, count, 0.8f);
    return mesh;
}

MeshBuilder WhaleView::buildMark(SeaMarkKind kind) {
    MeshBuilder mesh;
    const auto disk = [&](int sides, float jag, glm::vec3 color) {
        for (int i = 0; i < sides; i++) {
            const float a0 = 2 * kPi * i / sides, a1 = 2 * kPi * (i + 1) / sides;
            const float r0 = i % 2 ? jag : 1.0f, r1 = (i + 1) % 2 ? jag : 1.0f;
            mesh.triangle({0, 0, 0}, {r0 * std::cos(a0), 0, r0 * std::sin(a0)}, {r1 * std::cos(a1), 0, r1 * std::sin(a1)}, color);
        }
    };
    switch (kind) {
        case SeaMarkKind::Shadow: disk(14, 1.0f, {0.03f, 0.22f, 0.30f}); break;
        case SeaMarkKind::Foam: disk(12, 0.78f, {0.92f, 0.96f, 0.97f}); break;
        case SeaMarkKind::Bubble: octahedron(mesh, {0, 0, 0}, 1.0f, {0.90f, 0.96f, 1.0f}); break;
        case SeaMarkKind::Spray: {
            // Coluna que afina para cima, com um tufo no alto.
            constexpr int sides = 6;
            for (int i = 0; i < sides; i++) {
                const float a0 = 2 * kPi * i / sides, a1 = 2 * kPi * (i + 1) / sides;
                const glm::vec3 b0(std::cos(a0), 0, std::sin(a0)), b1(std::cos(a1), 0, std::sin(a1));
                mesh.quad(b0, b1, b1 * 0.45f + glm::vec3(0, 0.85f, 0), b0 * 0.45f + glm::vec3(0, 0.85f, 0), {0.86f, 0.93f, 0.97f});
                mesh.triangle(b0 * 0.45f + glm::vec3(0, 0.85f, 0), b1 * 0.45f + glm::vec3(0, 0.85f, 0), {0, 1.0f, 0}, {0.95f, 0.98f, 1.0f});
            }
            break;
        }
        case SeaMarkKind::Count: break;
    }
    return mesh;
}

glm::mat4 WhaleView::jawTransform(float jaw) {
    const glm::vec3 hinge(0, 0, kHingeZ);
    return glm::translate(glm::rotate(glm::translate(glm::mat4(1.0f), hinge), jaw, glm::vec3(1, 0, 0)), -hinge);
}

WhaleView::WhaleView()
    : body_(std::make_unique<Mesh>(buildBody().vertices())), jaw_(std::make_unique<Mesh>(buildJaw().vertices())) {
    for (size_t i = 0; i < marks_.size(); i++) marks_[i] = std::make_unique<Mesh>(buildMark(static_cast<SeaMarkKind>(i)).vertices());
}

// Desenhada antes do mar: a parte submersa fica escondida pela agua, como a sombra que passa por baixo.
void WhaleView::draw(const Shader& shader, const WhaleScene& scene) const {
    if (scene.whales.empty() && scene.marks.empty()) return;
    glDisable(GL_CULL_FACE);
    for (const WhaleInstance& whale : scene.whales) {
        shader.set("uModel", whale.model);
        body_->draw();
        shader.set("uModel", whale.model * jawTransform(whale.jaw));
        jaw_->draw();
    }
    for (const SeaMark& mark : scene.marks) {
        shader.set("uGlow", mark.kind == SeaMarkKind::Shadow ? 0.0f : 0.45f);
        shader.set("uModel", mark.model);
        marks_[static_cast<size_t>(mark.kind)]->draw();
    }
    shader.set("uGlow", 0.0f);
    shader.set("uModel", glm::mat4(1.0f));
    glEnable(GL_CULL_FACE);
}

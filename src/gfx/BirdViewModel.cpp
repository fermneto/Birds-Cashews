#include "gfx/BirdViewModel.h"
#include <glm/gtc/matrix_transform.hpp>

MeshBuilder BirdViewModel::buildWing() {
    MeshBuilder mesh;
    const glm::vec3 covert(0.20f, 0.42f, 0.55f), feather(0.11f, 0.27f, 0.42f), tip(0.93f, 0.78f, 0.32f);
    constexpr int kFeathers = 7;
    constexpr float kLength = 2.1f;
    for (int i = 0; i < kFeathers; i++) {
        const float x0 = kLength * i / kFeathers, x1 = kLength * (i + 1) / kFeathers;
        const float chord0 = 0.62f - 0.3f * x0 / kLength, chord1 = 0.62f - 0.3f * x1 / kLength;
        const float back = 0.30f + 0.16f * (i % 2);
        mesh.quad({x0, 0, -chord0 * 0.5f}, {x0, 0, 0.05f}, {x1, 0, 0.05f}, {x1, 0, -chord1 * 0.5f}, covert);
        mesh.quad({x0, -0.01f, 0.05f}, {x0 + 0.02f, -0.03f, back}, {x1 - 0.02f, -0.03f, back}, {x1, -0.01f, 0.05f},
                  i >= kFeathers - 2 ? tip : feather);
    }
    return mesh;
}

MeshBuilder BirdViewModel::buildBeak(bool upper) {
    MeshBuilder mesh;
    const glm::vec3 color = upper ? glm::vec3(0.96f, 0.62f, 0.16f) : glm::vec3(0.82f, 0.48f, 0.12f);
    const float height = upper ? 0.1f : -0.06f;
    const glm::vec3 tip(0, 0, -0.42f), left(-0.13f, 0, 0), right(0.13f, 0, 0), top(0, height, 0);
    mesh.triangle(left, tip, top, color);
    mesh.triangle(right, top, tip, color * 0.88f);
    mesh.triangle(left, right, tip, color * 0.75f);
    return mesh;
}

BirdViewModel::BirdViewModel()
    : wing_(std::make_unique<Mesh>(buildWing().vertices())),
      beakUpper_(std::make_unique<Mesh>(buildBeak(true).vertices())),
      beakLower_(std::make_unique<Mesh>(buildBeak(false).vertices())) {}

void BirdViewModel::drawWing(const Shader& shader, const glm::mat4& cameraToWorld, const BirdAnimation& animation, float side) const {
    // As asas avancam um pouco para que a ponta continue visivel na tela.
    glm::mat4 model = glm::translate(cameraToWorld, glm::vec3(side * 0.34f, -0.56f, -1.0f));
    model = glm::scale(model, glm::vec3(side, 1, 1));
    model = glm::rotate(model, glm::radians(side < 0 ? animation.leftWingAngle : animation.rightWingAngle), glm::vec3(0, 0, 1));
    model = glm::rotate(model, glm::radians(26.0f - animation.wingSweep), glm::vec3(0, 1, 0));
    shader.set("uModel", model);
    wing_->draw();
}

void BirdViewModel::draw(const Shader& shader, const glm::mat4& cameraToWorld, const BirdAnimation& animation) const {
    drawWing(shader, cameraToWorld, animation, 1.0f);
    drawWing(shader, cameraToWorld, animation, -1.0f);

    const glm::mat4 beakBase = glm::translate(cameraToWorld, glm::vec3(0, -0.47f, -0.8f));
    const float opening = glm::radians(animation.beakOpen * 24.0f);
    shader.set("uModel", glm::rotate(beakBase, opening, glm::vec3(1, 0, 0)));
    beakUpper_->draw();
    shader.set("uModel", glm::rotate(beakBase, -opening, glm::vec3(1, 0, 0)));
    beakLower_->draw();
}

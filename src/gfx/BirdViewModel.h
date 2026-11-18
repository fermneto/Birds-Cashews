#pragma once
#include "entity/PlayerBird.h"
#include "gfx/Mesh.h"
#include "gfx/Shader.h"
#include <memory>

// Asas e bico do proprio passaro, desenhados na frente da camera.
class BirdViewModel {
public:
    BirdViewModel();
    void draw(const Shader& shader, const glm::mat4& cameraToWorld, const BirdAnimation& animation) const;

private:
    static MeshBuilder buildWing();
    static MeshBuilder buildBeak(bool upper);
    void drawWing(const Shader& shader, const glm::mat4& cameraToWorld, const BirdAnimation& animation, float side) const;

    std::unique_ptr<Mesh> wing_, beakUpper_, beakLower_;
};

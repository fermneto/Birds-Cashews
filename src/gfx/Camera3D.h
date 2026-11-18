#pragma once
#include <glm/glm.hpp>

// Camera em primeira pessoa com guinada, arfagem e rolagem.
struct Camera3D {
    glm::vec3 position{0};
    float yaw = 0, pitch = 0, roll = 0;
    float fovDegrees = 72.0f, aspect = 16.0f / 9.0f;

    glm::mat4 view() const;
    glm::mat4 projection() const;
    glm::mat4 viewProjection() const { return projection() * view(); }
};

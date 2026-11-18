#include "gfx/Camera3D.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

glm::mat4 Camera3D::view() const {
    const glm::vec3 forward(std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, forward);
    // Rolar para a direita abaixa a asa direita, entao o "cima" da camera pende para a direita.
    const glm::vec3 rolledUp = up * std::cos(roll) + right * std::sin(roll);
    return glm::lookAt(position, position + forward, rolledUp);
}

glm::mat4 Camera3D::projection() const {
    return glm::perspective(glm::radians(fovDegrees), aspect, 0.3f, 9000.0f);
}

#pragma once
#include "gfx/MeshBuilder.h"
#include <cstdint>

// A mesma semente gera o mesmo passaro em qualquer computador. Implementada na tarefa D2.
MeshBuilder buildBirdMesh(uint32_t seed);

struct BirdFlightParts {
    MeshBuilder body, leftWing, rightWing;
    glm::vec3 rightShoulder;
    float wingspan;
};

BirdFlightParts buildBirdFlightParts(uint32_t seed);

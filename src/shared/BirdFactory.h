#pragma once
#include "gfx/MeshBuilder.h"
#include <array>
#include <cstdint>

// A mesma semente gera o mesmo passaro em qualquer computador. Implementada na tarefa D2.
// O genoma usa so o Rng inteiro e aritmetica basica, sem trigonometria, para nao depender da libm.
MeshBuilder buildBirdMesh(uint32_t seed);

struct BirdFlightParts {
    MeshBuilder body, leftWing, rightWing;
    glm::vec3 rightShoulder;
    float wingspan;
};

BirdFlightParts buildBirdFlightParts(uint32_t seed);

// Escala com que todo passaro procedural aparece no mundo.
inline constexpr float kBirdScale = 2.2f;

// Resumo do genoma: proporcoes em unidades do modelo e as cores das zonas principais.
struct BirdTraits {
    float length, width, height;   // Corpo: comprimento, meia largura e meia altura.
    float head, beak, beakDepth, crest, tail, tailSpread, span, chord;
    // Costas, barriga, cabeca, asa, ponta da asa e destaque (crista, faixas).
    std::array<glm::vec3, 6> colors;
};

BirdTraits describeBird(uint32_t seed);

// Distancia visual entre dois passaros: cores das zonas e proporcoes, perto de 0 quando iguais.
float birdLookDistance(const BirdTraits& a, const BirdTraits& b);

// Raio de colisao em metros no mundo: cobre o corpo e boa parte das asas abertas.
float birdCollisionRadius(const BirdTraits& bird);

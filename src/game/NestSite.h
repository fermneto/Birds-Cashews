#pragma once
#include "shared/GameTypes.h"
#include "world/World.h"
#include <cstddef>

// Lugar do ninho de um jogador no topo de um cajueiro.
struct NestSite {
    size_t tree = 0;        // Indice em World::trees.
    glm::vec3 base{0};      // Fundo do ninho, apoiado na folhagem.
    glm::vec3 perch{0};     // Onde o passaro fica pousado.
    float yaw = 0;          // Direcao livre para decolar.
};

// A mesma semente e o mesmo jogador sempre geram o mesmo lugar, em qualquer computador.
NestSite chooseNestSite(const World& world, uint32_t seed, PlayerId player);

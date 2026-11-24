#pragma once
#include <glm/glm.hpp>
#include <vector>

// O que desenhar da baleia do limite do mapa neste quadro. So dados: quem monta e o events/BoundaryWhale.
struct WhaleInstance {
    glm::mat4 model{1.0f};   // Ponta do focinho na origem, +Z para a frente, +Y para cima.
    float jaw = 0;           // Abertura da mandibula, em radianos.
};

// Marcas no mar: sombra da baleia por baixo da agua, espuma, borrifo do espiraculo e bolhas.
enum class SeaMarkKind { Shadow, Foam, Spray, Bubble, Count };

struct SeaMark {
    SeaMarkKind kind = SeaMarkKind::Shadow;
    glm::mat4 model{1.0f};   // Cada malha tem tamanho 1; a escala vem aqui.
};

struct WhaleScene {
    std::vector<WhaleInstance> whales;
    std::vector<SeaMark> marks;
};

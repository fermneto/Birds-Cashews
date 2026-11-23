#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// Camadas do som de ambiente, tocadas em loop o tempo todo e misturadas por volume.
enum class AmbientLayer { Wind, Sea, Birds, Count };
constexpr int kAmbientLayers = static_cast<int>(AmbientLayer::Count);

// Volume de cada camada, de 0 a 1.
struct AmbientMix {
    float wind = 0, sea = 0, birds = 0;
    float& operator[](AmbientLayer layer);
    float operator[](AmbientLayer layer) const;
};

// Onde o passaro esta e quao rapido voa, para escolher o que se ouve.
struct AmbientListener {
    glm::vec3 position{0};
    float speed = 0;        // m/s.
    float groundHeight = 0; // Altura do relevo logo abaixo, em metros.
    bool playing = false;   // Fora da partida, so um fundo calmo.
};

// Vento cresce com a velocidade; o mar, perto da costa e voando baixo; os passaros, perto das copas.
AmbientMix ambientMixFor(const AmbientListener& listener);

// Loop sem emenda audivel: o fim se funde com o comeco. Mono, na taxa do SoundManager.
std::vector<int16_t> synthesizeAmbientLoop(AmbientLayer layer, int sampleRate);

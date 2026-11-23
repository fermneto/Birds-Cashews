#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// Camadas continuas do som de voo e do ambiente, tocadas em loop e misturadas por volume.
// Os piados dos passaros nao sao camada: saem do BirdChorus, so quando ha passaros por perto.
enum class AmbientLayer { Wind, Rush, Climb, Dive, Sea, Count };
constexpr int kAmbientLayers = static_cast<int>(AmbientLayer::Count);

// Volume de cada camada, de 0 a 1.
struct AmbientMix {
    float wind = 0;    // Vento de fundo, sempre presente.
    float rush = 0;    // Zunido do ar passando, cresce com a velocidade.
    float climb = 0;   // Tom que sobe sem parar enquanto o passaro ganha altura.
    float dive = 0;    // Assobio do vento no mergulho.
    float sea = 0;     // Ondas, perto da costa.
    float& operator[](AmbientLayer layer);
    float operator[](AmbientLayer layer) const;
};

// Onde o passaro esta e como voa, para escolher o que se ouve.
struct AmbientListener {
    glm::vec3 position{0};
    float speed = 0;          // m/s, velocidade total.
    float verticalSpeed = 0;  // m/s, positivo subindo.
    float groundHeight = 0;   // Altura do relevo logo abaixo, em metros.
    bool playing = false;     // Fora da partida, so um fundo calmo.
};

// Tudo depende de valores continuos do voo, entao o som muda junto com o movimento, sem saltos.
AmbientMix ambientMixFor(const AmbientListener& listener);

// Loop sem emenda audivel: o fim se funde com o comeco. Mono, na taxa do SoundManager.
std::vector<int16_t> synthesizeAmbientLoop(AmbientLayer layer, int sampleRate);

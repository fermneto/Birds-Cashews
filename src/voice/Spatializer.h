#pragma once
#include "shared/GameTypes.h"
#include <glm/glm.hpp>

// Quem ouve: posicao, velocidade e direcao (yaw, na mesma convencao do FlightModel: frente = (sin, 0, -cos)).
struct VoiceListener {
    glm::vec3 position{0};
    glm::vec3 velocity{0};
    float yaw = 0;

    static VoiceListener from(const PlayerPublicState& state) { return {state.position, state.velocity, state.yaw}; }
};

struct SpatialSettings {
    float fullDistance = 15.0f;        // Volume cheio ate aqui.
    float silentDistance = 80.0f;      // Silencio a partir daqui.
    float speedOfSound = 343.0f;
    float dopplerExaggeration = 3.0f;  // Os passaros voam a 20-60 m/s: sem exagero o efeito mal se ouve.
    float minDoppler = 0.6f, maxDoppler = 1.6f;
    bool doppler = true;
};

// Como tocar a voz de um falante para quem ouve.
struct SpatialParams {
    float gain = 1.0f;                     // Volume pela distancia.
    float pan = 0.0f;                      // -1 esquerda, +1 direita.
    float left = 0.70710678f, right = 0.70710678f;   // Ganhos de potencia constante do pan.
    float doppler = 1.0f;                  // Fator de frequencia (e de leitura) do Doppler.
    float distance = 0.0f;
};

float distanceGain(float distance, const SpatialSettings& settings);
void panGains(float pan, float& left, float& right);
float dopplerFactor(const VoiceListener& listener, const glm::vec3& sourcePosition, const glm::vec3& sourceVelocity,
                    const SpatialSettings& settings);
SpatialParams spatialize(const VoiceListener& listener, const PlayerPublicState& speaker, const SpatialSettings& settings);

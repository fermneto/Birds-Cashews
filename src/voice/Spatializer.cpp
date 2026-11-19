#include "voice/Spatializer.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kQuarterPi = 0.78539816f;
constexpr float kNearDistance = 0.5f;   // Mais perto que isso a direcao nao tem sentido: centro e sem Doppler.
}

// Cheio ate fullDistance e silencio em silentDistance, com curva suave (smoothstep) entre os dois,
// sem degrau nem quina na derivada.
float distanceGain(float distance, const SpatialSettings& settings) {
    const float span = std::max(settings.silentDistance - settings.fullDistance, 1e-3f);
    const float t = std::clamp((distance - settings.fullDistance) / span, 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

// Pan de potencia constante: left^2 + right^2 = 1 em qualquer posicao.
void panGains(float pan, float& left, float& right) {
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * kQuarterPi;
    left = std::cos(angle);
    right = std::sin(angle);
}

// f = (c + v_ouvinte) / (c - v_fonte), com as velocidades projetadas na linha entre os dois,
// positivas quando um vai em direcao ao outro. O exagero multiplica as velocidades.
float dopplerFactor(const VoiceListener& listener, const glm::vec3& sourcePosition, const glm::vec3& sourceVelocity,
                    const SpatialSettings& settings) {
    if (!settings.doppler) return 1.0f;
    const glm::vec3 offset = sourcePosition - listener.position;
    const float distance = glm::length(offset);
    if (distance < kNearDistance) return 1.0f;
    const glm::vec3 toSource = offset / distance;
    const float c = settings.speedOfSound;
    const float listenerTowards = glm::dot(listener.velocity, toSource) * settings.dopplerExaggeration;
    const float sourceTowards = glm::dot(sourceVelocity, -toSource) * settings.dopplerExaggeration;
    const float denominator = std::max(c - sourceTowards, 1e-3f);
    const float factor = std::max(c + listenerTowards, 0.0f) / denominator;
    return std::clamp(factor, settings.minDoppler, settings.maxDoppler);
}

SpatialParams spatialize(const VoiceListener& listener, const PlayerPublicState& speaker, const SpatialSettings& settings) {
    SpatialParams params;
    const glm::vec3 offset = speaker.position - listener.position;
    params.distance = glm::length(offset);
    params.gain = distanceGain(params.distance, settings);
    // Angulo no plano horizontal em relacao a frente de quem ouve. O seno do angulo da o lado:
    // na frente e atras fica no centro, a 90 graus fica todo de um lado.
    const glm::vec3 forward(std::sin(listener.yaw), 0, -std::cos(listener.yaw));
    const glm::vec3 right(std::cos(listener.yaw), 0, std::sin(listener.yaw));
    const float side = glm::dot(offset, right), ahead = glm::dot(offset, forward);
    const float horizontal = std::sqrt(side * side + ahead * ahead);
    params.pan = horizontal > kNearDistance ? side / horizontal : 0.0f;
    panGains(params.pan, params.left, params.right);
    params.doppler = dopplerFactor(listener, speaker.position, speaker.velocity, settings);
    return params;
}

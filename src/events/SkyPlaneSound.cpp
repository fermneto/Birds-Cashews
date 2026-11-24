#include "events/SkyPlaneSound.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr float kTwoPi = 6.2831853f;
constexpr float kSoundSpeed = 343.0f;
constexpr float kEngineRange = 1500.0f;    // Alem disso o motor nao se ouve.
constexpr float kImpactRange = 900.0f;
constexpr float kEngineLoopSeconds = 2.0f;
constexpr float kEngineSeam = 0.25f;
constexpr float kEngineHz = 46.0f;         // Multiplo de 0,5 Hz: o loop de 2 s fecha sem salto.
constexpr float kBladeHz = 23.0f;          // Batida das pas da helice.
constexpr float kEnginePeak = 12000.0f;    // Como as camadas do ambiente.

float engineVolume(float distance) {
    const float near = std::clamp(1.0f - distance / kEngineRange, 0.0f, 1.0f);
    return 0.9f * near * near;
}
}

PlaneEngineMix planeEngineFor(glm::vec3 listener, glm::vec3 listenerVelocity, const std::vector<SkyPlane>& planes) {
    PlaneEngineMix mix;
    for (const SkyPlane& plane : planes) {
        const glm::vec3 toPlane = plane.position - listener;
        const float distance = glm::length(toPlane);
        const float volume = engineVolume(distance);
        if (volume <= mix.volume) continue;
        const glm::vec3 direction = distance > 0.01f ? toPlane / distance : glm::vec3(0, 0, -1);
        // Quem se aproxima ouve mais agudo: f' = f (c + v_ouvinte . u) / (c + v_fonte . u), u do ouvinte a fonte.
        const float heard = kSoundSpeed + glm::dot(listenerVelocity, direction);
        const float emitted = kSoundSpeed + glm::dot(plane.velocity(), direction);
        mix.volume = volume;
        mix.rate = std::clamp(heard / std::max(emitted, 1.0f), 0.5f, 2.0f);
    }
    return mix;
}

float planeImpactVolume(glm::vec3 listener, glm::vec3 impact) {
    const float near = std::clamp(1.0f - glm::length(impact - listener) / kImpactRange, 0.0f, 1.0f);
    return near * near;
}

// Harmonicos de um motor a pistao, pulsando no ritmo das pas, com um zumbido de ruido por cima.
// O ruido nao e periodico, entao o fim do loop e fundido com o comeco.
std::vector<int16_t> synthesizePlaneEngineLoop(int sampleRate) {
    const int length = static_cast<int>(kEngineLoopSeconds * static_cast<float>(sampleRate));
    const int seam = static_cast<int>(kEngineSeam * static_cast<float>(sampleRate));
    std::mt19937 random(0xA1E0u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    std::vector<float> raw(static_cast<size_t>(length + seam));
    float hiss = 0, buzz = 0;
    for (size_t i = 0; i < raw.size(); i++) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float tone = 0;
        for (int k = 1; k <= 12; k++) tone += std::sin(kTwoPi * kEngineHz * k * t + 0.7f * k) / std::pow(static_cast<float>(k), 0.8f);
        const float blades = 0.72f + 0.28f * std::sin(kTwoPi * kBladeHz * t);
        hiss += (white(random) - hiss) * 0.35f;
        buzz += (hiss - buzz) * 0.5f;
        raw[i] = tone * blades * 0.22f + buzz * 0.5f;
    }
    std::vector<float> loop(raw.begin(), raw.begin() + length);
    for (int i = 0; i < seam; i++) {
        const float a = static_cast<float>(i) / static_cast<float>(seam);
        loop[static_cast<size_t>(i)] = raw[static_cast<size_t>(length + i)] * (1.0f - a) + raw[static_cast<size_t>(i)] * a;
    }
    float peak = 1e-6f;
    for (float value : loop) peak = std::max(peak, std::abs(value));
    std::vector<int16_t> samples(loop.size());
    std::transform(loop.begin(), loop.end(), samples.begin(),
                   [&](float value) { return static_cast<int16_t>(value / peak * kEnginePeak); });
    return samples;
}

std::vector<float> synthesizePlaneImpact(int sampleRate) {
    constexpr float kSeconds = 0.9f;
    std::mt19937 random(0xC4A5u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    std::vector<float> mix(static_cast<size_t>(kSeconds * static_cast<float>(sampleRate)));
    float thumpPhase = 0, crunch = 0, crackle = 0;
    for (size_t i = 0; i < mix.size(); i++) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float attack = std::min(t / 0.004f, 1.0f);
        const float release = std::min((kSeconds - t) / 0.05f, 1.0f);
        // Pancada grave que cai de 90 para 35 Hz.
        thumpPhase += (35.0f + 55.0f * std::exp(-7.0f * t)) / static_cast<float>(sampleRate);
        const float thump = std::sin(kTwoPi * thumpPhase) * std::exp(-5.0f * t);
        // Lataria amassando: ruido grave que some rapido, com estalos soltos.
        crunch += (white(random) - crunch) * 0.18f;
        if (white(random) > 0.9985f) crackle = white(random);
        crackle *= 0.995f;
        const float noise = (crunch * 1.6f + crackle * 0.5f) * std::exp(-7.0f * t);
        // Zunido metalico de parciais desafinadas.
        const float clang = (std::sin(kTwoPi * 620.0f * t) + 0.7f * std::sin(kTwoPi * 1130.0f * t) +
                             0.45f * std::sin(kTwoPi * 1870.0f * t)) * 0.22f * std::exp(-6.0f * t);
        mix[i] = (thump * 0.95f + noise + clang) * attack * release;
    }
    return mix;
}

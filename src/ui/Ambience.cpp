#include "ui/Ambience.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr float kTwoPi = 6.2831853f;
constexpr float kLoopSeconds = 8.0f;
constexpr float kSeamSeconds = 0.75f;   // Trecho do fim que se funde com o comeco.

// Volumes de cada camada no loop gerado, antes do volume do AmbientMix.
constexpr float kWindPeak = 0.30f, kSeaPeak = 0.28f, kBirdsPeak = 0.22f;

// Distancias da ilha: a costa fica por volta de 820 m do centro.
constexpr float kSeaFrom = 620.0f, kSeaFull = 900.0f;
constexpr float kBirdsInland = 780.0f, kBirdsCoast = 950.0f;

float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Gera "total" amostras com a funcao e funde o excesso do fim no comeco, para o loop nao estalar.
template <class Generate>
std::vector<float> seamless(int sampleRate, Generate generate) {
    const int length = static_cast<int>(kLoopSeconds * sampleRate);
    const int seam = static_cast<int>(kSeamSeconds * sampleRate);
    std::vector<float> raw(static_cast<size_t>(length + seam));
    for (size_t i = 0; i < raw.size(); i++) raw[i] = generate(static_cast<int>(i));
    std::vector<float> loop(raw.begin(), raw.begin() + length);
    for (int i = 0; i < seam; i++) {
        const float t = static_cast<float>(i) / static_cast<float>(seam);
        loop[static_cast<size_t>(i)] = raw[static_cast<size_t>(length + i)] * (1.0f - t) + raw[static_cast<size_t>(i)] * t;
    }
    return loop;
}

// Ruido marrom suave, com rajadas lentas que repetem em periodos inteiros do loop.
std::vector<float> wind(int sampleRate) {
    std::mt19937 random(0xB1D5u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    float low = 0, lower = 0;
    const float length = kLoopSeconds * sampleRate;
    return seamless(sampleRate, [&](int i) {
        low += (white(random) - low) * 0.035f;
        lower += (low - lower) * 0.08f;
        const float phase = static_cast<float>(i) / length;
        const float gust = 0.6f + 0.25f * std::sin(kTwoPi * phase) + 0.15f * std::sin(kTwoPi * 3.0f * phase + 1.3f);
        return lower * 9.0f * gust;
    });
}

// Ondas: ruido grave que sobe e desce duas vezes por loop, como a arrebentacao.
std::vector<float> sea(int sampleRate) {
    std::mt19937 random(0x5EA5u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    float low = 0;
    const float length = kLoopSeconds * sampleRate;
    return seamless(sampleRate, [&](int i) {
        low += (white(random) - low) * 0.06f;
        const float swell = std::fmod(2.0f * static_cast<float>(i) / length, 1.0f);   // Duas ondas por loop.
        const float wave = std::pow(std::sin(3.14159265f * swell), 3.0f);
        return low * 6.0f * (0.25f + 0.75f * wave);
    });
}

// Passaros distantes: piados curtos, sorteados uma vez e sempre iguais.
std::vector<float> birds(int sampleRate) {
    struct Chirp { float start, seconds, fromHz, toHz; };
    std::mt19937 random(0xC41Cu);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<Chirp> chirps;
    for (float t = 0.3f; t < kLoopSeconds - 0.4f; t += 0.5f + unit(random) * 1.3f) {
        const int notes = 1 + static_cast<int>(unit(random) * 3.0f);
        const float base = 2200.0f + unit(random) * 1800.0f;
        for (int n = 0; n < notes; n++)
            chirps.push_back({t + n * 0.11f, 0.07f + unit(random) * 0.05f, base * (1.0f + 0.1f * n), base * (1.25f + 0.1f * n)});
    }
    return seamless(sampleRate, [&](int i) {
        const float t = std::fmod(static_cast<float>(i) / sampleRate, kLoopSeconds);
        float value = 0;
        for (const Chirp& chirp : chirps) {
            const float local = t - chirp.start;
            if (local < 0 || local > chirp.seconds) continue;
            const float progress = local / chirp.seconds;
            const float hz = chirp.fromHz + (chirp.toHz - chirp.fromHz) * progress;
            const float envelope = std::sin(3.14159265f * progress);
            value += std::sin(kTwoPi * hz * local) * envelope * envelope * (0.8f + 0.2f * std::sin(kTwoPi * 38.0f * local));
        }
        return value;
    });
}

// Normaliza pelo pico, para cada camada ter o volume escolhido independentemente do ruido sorteado.
std::vector<int16_t> toSamples(const std::vector<float>& loop, float peak) {
    float highest = 1e-6f;
    for (float value : loop) highest = std::max(highest, std::abs(value));
    std::vector<int16_t> samples(loop.size());
    std::transform(loop.begin(), loop.end(), samples.begin(), [&](float value) {
        return static_cast<int16_t>(std::clamp(value / highest * peak, -1.0f, 1.0f) * 32767.0f);
    });
    return samples;
}
}

float& AmbientMix::operator[](AmbientLayer layer) {
    switch (layer) {
        case AmbientLayer::Sea: return sea;
        case AmbientLayer::Birds: return birds;
        default: return wind;
    }
}

float AmbientMix::operator[](AmbientLayer layer) const { return const_cast<AmbientMix&>(*this)[layer]; }

AmbientMix ambientMixFor(const AmbientListener& listener) {
    AmbientMix mix;
    if (!listener.playing) {
        mix.wind = 0.15f;
        mix.sea = 0.25f;
        mix.birds = 0.35f;
        return mix;
    }
    const float radius = glm::length(glm::vec2(listener.position.x, listener.position.z));
    const float altitude = std::max(0.0f, listener.position.y - std::max(listener.groundHeight, 0.0f));
    const float low = 1.0f - smooth(60.0f, 260.0f, altitude);
    mix.wind = 0.12f + 0.88f * std::clamp((listener.speed - 12.0f) / 45.0f, 0.0f, 1.0f);
    mix.sea = smooth(kSeaFrom, kSeaFull, radius) * (0.35f + 0.65f * low);
    mix.birds = 0.8f * (1.0f - smooth(30.0f, 160.0f, altitude)) * (1.0f - smooth(kBirdsInland, kBirdsCoast, radius));
    return mix;
}

std::vector<int16_t> synthesizeAmbientLoop(AmbientLayer layer, int sampleRate) {
    switch (layer) {
        case AmbientLayer::Wind: return toSamples(wind(sampleRate), kWindPeak);
        case AmbientLayer::Sea: return toSamples(sea(sampleRate), kSeaPeak);
        case AmbientLayer::Birds: return toSamples(birds(sampleRate), kBirdsPeak);
        case AmbientLayer::Count: break;
    }
    return {};
}

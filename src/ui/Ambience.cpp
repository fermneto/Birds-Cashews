#include "ui/Ambience.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace {
constexpr float kTwoPi = 6.2831853f;
constexpr float kLoopSeconds = 8.0f;
constexpr float kSeamSeconds = 0.75f;   // Trecho do fim que se funde com o comeco.

// Pico de cada camada no loop gerado, antes do volume do AmbientMix.
constexpr float kWindPeak = 0.36f, kRushPeak = 0.14f, kClimbPeak = 0.24f, kDivePeak = 0.26f, kSeaPeak = 0.30f;

// Distancias da ilha: a costa fica por volta de 820 m do centro.
constexpr float kSeaFrom = 620.0f, kSeaFull = 900.0f;

float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Gera o loop e mais um trecho, e funde esse trecho no comeco, para o loop nao estalar.
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

// Filtro passa-faixa de dois polos: da cor a um ruido branco.
class Resonator {
public:
    Resonator(float hz, float q, int sampleRate) { tune(hz, q, sampleRate); }
    void tune(float hz, float q, int sampleRate) {
        const float w = kTwoPi * hz / static_cast<float>(sampleRate), alpha = std::sin(w) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0_ = alpha / a0;
        a1_ = -2.0f * std::cos(w) / a0;
        a2_ = (1.0f - alpha) / a0;
    }
    float operator()(float in) {
        const float out = b0_ * (in - x2_) - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = in;
        y2_ = y1_;
        y1_ = out;
        return out;
    }

private:
    float b0_ = 0, a1_ = 0, a2_ = 0, x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

// Vento de fundo: sopro arejado, sem ronco grave. As rajadas sobem e descem devagar, e o tom do sopro
// acompanha a rajada, como vento passando pelas folhas. Todas as ondas repetem em periodos inteiros do loop.
std::vector<float> wind(int sampleRate) {
    std::mt19937 random(0xB1D5u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    Resonator band(500.0f, 1.1f, sampleRate);
    float soft = 0;
    const float length = kLoopSeconds * sampleRate;
    return seamless(sampleRate, [&](int i) {
        const float phase = static_cast<float>(i) / length;
        const float gust = std::clamp(0.45f + 0.30f * std::sin(kTwoPi * phase + 0.4f) + 0.15f * std::sin(kTwoPi * 3.0f * phase + 1.7f)
                                          + 0.10f * std::sin(kTwoPi * 5.0f * phase + 2.9f), 0.0f, 1.0f);
        if (i % 32 == 0) band.tune(380.0f + 420.0f * gust, 1.1f, sampleRate);
        soft += (band(white(random)) - soft) * 0.25f;   // Tira o chiado mais agudo.
        return soft * (0.25f + 0.75f * gust * gust);
    });
}

// Chiado leve do ar em alta velocidade: so as frequencias altas do ruido, sem faixa fixa de motor.
std::vector<float> rush(int sampleRate) {
    std::mt19937 random(0x2A5Bu);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    float low = 0, smooth = 0;
    const float length = kLoopSeconds * sampleRate;
    return seamless(sampleRate, [&](int i) {
        const float noise = white(random);
        low += (noise - low) * 0.08f;
        smooth += ((noise - low) - smooth) * 0.35f;   // Agudo, mas sem aspereza.
        const float phase = static_cast<float>(i) / length;
        return smooth * (0.8f + 0.2f * std::sin(kTwoPi * 2.0f * phase + 0.9f));
    });
}

// Subida: sopro grave e quente, sem nota definida. Em cada uma das quatro ondas do loop a faixa do ruido
// sobe de leve, dando a sensacao de ganhar altura sem nenhum tom agudo.
std::vector<float> climb(int sampleRate) {
    std::mt19937 random(0xC11Bu);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    Resonator band(260.0f, 0.9f, sampleRate);
    float soft = 0;
    const float length = kLoopSeconds * sampleRate;
    return seamless(sampleRate, [&](int i) {
        const float swell = std::fmod(4.0f * static_cast<float>(i) / length, 1.0f);   // Quatro ondas por loop.
        const float rise = std::sin(3.14159265f * swell);
        if (i % 32 == 0) band.tune(220.0f + 220.0f * swell, 0.9f, sampleRate);
        soft += (band(white(random)) - soft) * 0.2f;
        return soft * (0.35f + 0.65f * rise);
    });
}

// Assobio do mergulho: faixa estreita de ruido agudo que oscila de leve, como vento numa fresta.
std::vector<float> dive(int sampleRate) {
    std::mt19937 random(0xD1BEu);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    Resonator whistle(1250.0f, 9.0f, sampleRate), body(500.0f, 0.7f, sampleRate);
    const float length = kLoopSeconds * sampleRate;
    return seamless(sampleRate, [&](int i) {
        const float phase = static_cast<float>(i) / length;
        whistle.tune(1250.0f + 120.0f * std::sin(kTwoPi * 3.0f * phase), 9.0f, sampleRate);
        const float noise = white(random);
        return whistle(noise) * 3.0f + body(noise) * 0.5f;
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
        const float swell = std::fmod(2.0f * static_cast<float>(i) / length, 1.0f);
        const float wave = std::pow(std::sin(3.14159265f * swell), 3.0f);
        return low * (0.25f + 0.75f * wave);
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
        case AmbientLayer::Rush: return rush;
        case AmbientLayer::Climb: return climb;
        case AmbientLayer::Dive: return dive;
        case AmbientLayer::Sea: return sea;
        default: return wind;
    }
}

float AmbientMix::operator[](AmbientLayer layer) const { return const_cast<AmbientMix&>(*this)[layer]; }

AmbientMix ambientMixFor(const AmbientListener& listener) {
    AmbientMix mix;
    if (!listener.playing) {
        mix.wind = 0.25f;
        mix.sea = 0.2f;
        return mix;
    }
    const float radius = glm::length(glm::vec2(listener.position.x, listener.position.z));
    const float altitude = std::max(0.0f, listener.position.y - std::max(listener.groundHeight, 0.0f));
    const float fast = smooth(18.0f, 70.0f, listener.speed);
    mix.wind = 0.45f + 0.30f * smooth(80.0f, 400.0f, altitude) + 0.25f * fast;   // Mais aberto no alto e mais forte rapido.
    mix.rush = 0.6f * fast;
    mix.climb = smooth(1.0f, 9.0f, listener.verticalSpeed) * 0.9f;
    mix.dive = smooth(4.0f, 28.0f, -listener.verticalSpeed) * (0.4f + 0.6f * fast);
    mix.sea = smooth(kSeaFrom, kSeaFull, radius) * (0.35f + 0.65f * (1.0f - smooth(60.0f, 260.0f, altitude)));
    return mix;
}

std::vector<int16_t> synthesizeAmbientLoop(AmbientLayer layer, int sampleRate) {
    switch (layer) {
        case AmbientLayer::Wind: return toSamples(wind(sampleRate), kWindPeak);
        case AmbientLayer::Rush: return toSamples(rush(sampleRate), kRushPeak);
        case AmbientLayer::Climb: return toSamples(climb(sampleRate), kClimbPeak);
        case AmbientLayer::Dive: return toSamples(dive(sampleRate), kDivePeak);
        case AmbientLayer::Sea: return toSamples(sea(sampleRate), kSeaPeak);
        case AmbientLayer::Count: break;
    }
    return {};
}

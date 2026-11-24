#include "ui/WhaleSounds.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr float kTwoPi = 6.2831853f;

// Monta um efeito somando camadas; cada camada recebe o tempo em segundos e devolve a amostra.
class Sketch {
public:
    Sketch(float seconds, int rate, uint32_t seed) : samples_(static_cast<size_t>(seconds * rate)), rate_(rate), random_(seed) {}

    float white() { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(random_); }
    int rate() const { return rate_; }
    size_t size() const { return samples_.size(); }
    float time(size_t i) const { return static_cast<float>(i) / rate_; }
    void add(size_t i, float value) { samples_[i] += value; }

    // Nota com glissando exponencial e queda propria. harmonic soma um pouco da oitava, para soar mais cheia.
    void tone(float start, float seconds, float fromHz, float toHz, float volume, float decay, float harmonic = 0) {
        float phase = 0;
        const size_t first = static_cast<size_t>(start * rate_);
        for (size_t i = first; i < size() && time(i) < start + seconds; i++) {
            const float t = time(i) - start, u = t / seconds;
            phase += fromHz * std::pow(toHz / fromHz, u) / rate_;
            const float shape = std::min(t / 0.004f, 1.0f) * std::min((seconds - t) / 0.02f, 1.0f) * std::exp(-decay * t);
            add(i, (std::sin(kTwoPi * phase) + harmonic * std::sin(2.0f * kTwoPi * phase)) * volume * shape);
        }
    }

    // Normaliza para o pico pedido e esfuma as pontas, para nao estalar.
    std::vector<float> finish(float peak) {
        float high = 0;
        for (float s : samples_) high = std::max(high, std::abs(s));
        const float gain = high > 0 ? peak / high : 0.0f;
        const size_t fade = static_cast<size_t>(0.01f * rate_);
        for (size_t i = 0; i < size(); i++) {
            const float edge = std::min({1.0f, static_cast<float>(i) / fade, static_cast<float>(size() - 1 - i) / fade});
            samples_[i] *= gain * edge;
        }
        return samples_;
    }

private:
    std::vector<float> samples_;
    int rate_;
    std::mt19937 random_;
};

// Espirro do espiraculo: um "pfffsss" de ar com agua, que abre rapido e se desfaz.
std::vector<float> spout(int rate) {
    Sketch s(0.9f, rate, 0x5B0u);
    float low = 0, band = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        const float noise = s.white();
        low += (noise - low) * 0.08f;
        band += ((noise - low) - band) * 0.45f;   // Tira o grave e o agudo demais.
        const float swell = std::min(t / 0.05f, 1.0f) * std::exp(-3.2f * std::max(0.0f, t - 0.05f));
        s.add(i, band * swell + low * 0.6f * swell);
    }
    return s.finish(1.1f);
}

// Mergulho pesado: pancada grave e agua espirrando.
std::vector<float> splash(int rate) {
    Sketch s(0.85f, rate, 0x5A1u);
    s.tone(0, 0.32f, 85, 35, 1.0f, 7.0f);
    float low = 0, bright = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        const float noise = s.white();
        low += (noise - low) * 0.22f;
        bright += (noise - bright) * 0.7f;
        const float body = std::min(t / 0.015f, 1.0f) * std::exp(-4.5f * t);
        const float drops = (s.white() > 0.985f ? 1.0f : 0.0f) * std::exp(-3.0f * t);   // Gotas caindo.
        s.add(i, low * 1.4f * body + (bright - low) * 0.5f * body * std::exp(-6.0f * t) + drops * 0.6f);
    }
    return s.finish(1.3f);
}

// Mordida: estalo dos dentes, baque grave e um esmagar molhado logo depois.
std::vector<float> bite(int rate) {
    Sketch s(0.5f, rate, 0xB17u);
    s.tone(0, 0.22f, 140, 55, 1.0f, 9.0f, 0.5f);
    float low = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        const float noise = s.white();
        low += (noise - low) * 0.18f;
        const float click = t < 0.025f ? noise * (1.0f - t / 0.025f) : 0.0f;
        const float grain = std::sin(kTwoPi * 38.0f * t) > 0.2f ? 1.0f : 0.3f;   // Esmagar em pedacos.
        const float crunch = t > 0.04f ? low * grain * std::exp(-6.0f * (t - 0.04f)) : 0.0f;
        s.add(i, click * 1.2f + crunch * 1.1f);
    }
    return s.finish(1.5f);
}

// Mastigar: cada fechada da boca e um esmagar abafado com um baque curto.
std::vector<float> chew(int rate) {
    Sketch s(0.3f, rate, 0xC4Eu);
    s.tone(0, 0.14f, 90, 60, 0.7f, 14.0f);
    float low = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        low += (s.white() - low) * 0.12f;
        const float grain = std::sin(kTwoPi * 55.0f * t + 1.3f * std::sin(kTwoPi * 9.0f * t)) > 0.0f ? 1.0f : 0.25f;
        s.add(i, low * 2.2f * grain * std::min(t / 0.01f, 1.0f) * std::exp(-9.0f * t));
    }
    return s.finish(1.1f);
}

// Cuspida: um "ptu" estalado seguido do sopro do passaro saindo voando.
std::vector<float> spit(int rate) {
    Sketch s(0.6f, rate, 0x5917u);
    s.tone(0, 0.07f, 190, 90, 0.9f, 20.0f);
    s.tone(0.03f, 0.26f, 300, 760, 0.35f, 5.0f);
    float low = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        const float cutoff = 0.04f + 0.4f * std::min(t / 0.25f, 1.0f);   // O sopro fica mais agudo.
        low += (s.white() - low) * cutoff;
        const float swell = std::min(t / 0.08f, 1.0f) * std::exp(-5.0f * std::max(0.0f, t - 0.08f));
        s.add(i, low * 0.9f * swell);
    }
    return s.finish(1.3f);
}

// Ronco grave da baleia por perto, com vibrato lento. Sobe e desce devagar para emendar com o proximo.
std::vector<float> rumble(int rate) {
    Sketch s(0.95f, rate, 0x2B1u);
    float phase = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i), u = t / 0.95f;
        const float hz = 72.0f - 16.0f * u + 3.0f * std::sin(kTwoPi * 4.5f * t);
        phase += hz / rate;
        const float body = std::sin(kTwoPi * phase) + 0.55f * std::sin(2.0f * kTwoPi * phase) + 0.3f * std::sin(3.0f * kTwoPi * phase);
        s.add(i, body * std::sin(3.14159265f * u));
    }
    return s.finish(1.0f);
}
}

std::vector<float> synthesizeWhaleSound(WhaleSound sound, int sampleRate) {
    switch (sound) {
        case WhaleSound::Spout: return spout(sampleRate);
        case WhaleSound::Splash: return splash(sampleRate);
        case WhaleSound::Bite: return bite(sampleRate);
        case WhaleSound::Chew: return chew(sampleRate);
        case WhaleSound::Spit: return spit(sampleRate);
        case WhaleSound::Rumble: return rumble(sampleRate);
    }
    return {};
}

#include "ui/WhaleSounds.h"
#include <algorithm>
#include <cmath>
#include <random>

// Cada som e feito de camadas: ruido filtrado (agua, ar, carne), tons com glissando (voz e pancadas) e um
// sub-grave por baixo, para ter corpo. Um pouco de saturacao junta as camadas e um eco curto da o espaco do mar.
namespace {
constexpr float kPi = 3.14159265f, kTwoPi = 6.2831853f;

float ease(float a, float b, float x) {
    const float u = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

// Filtro de estado variavel: da passa-baixa, passa-banda e passa-alta de uma vez. damping baixo ressoa mais.
class Filter {
public:
    explicit Filter(int rate) : rate_(static_cast<float>(rate)) {}
    void run(float input, float hz, float damping) {
        const float f = std::min(2.0f * std::sin(kPi * std::min(hz, rate_ * 0.2f) / rate_), 1.0f);
        low += f * band;
        high = input - low - damping * band;
        band += f * high;
    }
    float low = 0, band = 0, high = 0;

private:
    float rate_;
};

// Monta um efeito somando camadas; cada camada recebe o tempo em segundos e devolve a amostra.
class Sketch {
public:
    Sketch(float seconds, int rate, uint32_t seed) : samples_(static_cast<size_t>(seconds * rate)), rate_(rate), random_(seed) {}

    float white() { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(random_); }
    float chance() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(random_); }
    int rate() const { return rate_; }
    size_t size() const { return samples_.size(); }
    float seconds() const { return static_cast<float>(size()) / rate_; }
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

    // Gotas caindo na agua: "ploc" curtos que sobem de tom, cada vez mais raros e baixos.
    void droplets(float from, float to, int count, float volume) {
        for (int i = 0; i < count; i++) {
            const float u = chance(), at = from + (to - from) * u * u;
            const float hz = 700.0f + 1500.0f * chance();
            tone(at, 0.07f, hz, hz * 1.7f, volume * (0.4f + 0.6f * chance()) * (1.0f - 0.7f * u), 40.0f);
        }
    }

    // Eco do mar: repeticoes cada vez mais baixas, que preenchem a cauda do som.
    void echo(float delay, float feedback) {
        const size_t gap = static_cast<size_t>(delay * rate_);
        for (size_t i = gap; i < size(); i++) samples_[i] += feedback * samples_[i - gap];
    }

    // Satura de leve (drive), normaliza para o pico pedido e esfuma as pontas, para nao estalar.
    std::vector<float> finish(float peak, float drive = 1.0f) {
        if (drive > 1.0f) {
            float high = 0;
            for (float s : samples_) high = std::max(high, std::abs(s));
            for (float& s : samples_) s = high > 0 ? std::tanh(drive * s / high) : 0.0f;
        }
        float high = 0;
        for (float s : samples_) high = std::max(high, std::abs(s));
        const float gain = high > 0 ? peak / high : 0.0f;
        const size_t fade = static_cast<size_t>(0.01f * rate_), tail = static_cast<size_t>(0.25f * rate_);
        for (size_t i = 0; i < size(); i++) {
            const float end = static_cast<float>(size() - 1 - i);
            const float edge = std::min({1.0f, static_cast<float>(i) / fade, end / tail});
            samples_[i] *= gain * edge;
        }
        return samples_;
    }

private:
    std::vector<float> samples_;
    int rate_;
    std::mt19937 random_;
};

// Espirro do espiraculo: um "PFFFSSSH" forte de ar com agua, com um sopro grave por baixo e a garoa caindo depois.
std::vector<float> spout(int rate) {
    Sketch s(1.8f, rate, 0x5B0u);
    s.tone(0, 0.45f, 95, 42, 0.9f, 5.0f, 0.3f);
    Filter blast(rate), gust(rate), hiss(rate);
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        const float swell = std::min(t / 0.04f, 1.0f) * std::exp(-2.0f * std::max(0.0f, t - 0.04f));
        blast.run(s.white(), 1100.0f - 500.0f * ease(0, 1.2f, t), 0.8f);
        gust.run(s.white(), 220.0f, 0.9f);
        hiss.run(s.white(), 3500.0f, 1.2f);
        s.add(i, (blast.band * 1.5f + gust.low * 1.4f + hiss.high * 0.18f) * swell);
    }
    s.droplets(0.45f, 1.7f, 30, 0.12f);
    s.echo(0.19f, 0.25f);
    return s.finish(1.7f, 1.6f);
}

// Splash pesado: baque sub-grave do corpo na agua, estouro da onda e uma cauda longa de agua revolta e gotas.
std::vector<float> splash(int rate) {
    Sketch s(2.4f, rate, 0x5A1u);
    s.tone(0, 0.8f, 72, 24, 1.6f, 4.0f);            // Sub-grave do corpo batendo.
    s.tone(0, 0.3f, 160, 55, 0.7f, 10.0f, 0.5f);
    Filter slam(rate), crash(rate), wash(rate);
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        const float hit = std::min(t / 0.006f, 1.0f) * std::exp(-5.0f * t);
        slam.run(s.white(), 320.0f, 0.7f);
        crash.run(s.white(), 1300.0f, 0.6f);
        // Agua revolta: ruido grave que borbulha e vai fechando o filtro enquanto se acalma.
        wash.run(s.white(), 900.0f - 550.0f * ease(0.1f, 2.2f, t), 0.5f);
        const float gurgle = 0.55f + 0.45f * std::sin(kTwoPi * (6.0f + 3.0f * std::sin(kTwoPi * 0.6f * t)) * t);
        const float tail = ease(0.03f, 0.25f, t) * std::exp(-1.5f * std::max(0.0f, t - 0.25f));
        s.add(i, slam.low * 2.4f * hit + crash.band * 1.3f * std::exp(-7.0f * t) * std::min(t / 0.004f, 1.0f) +
                     wash.low * 1.1f * gurgle * tail);
    }
    s.droplets(0.15f, 2.2f, 55, 0.22f);
    s.echo(0.23f, 0.3f);
    return s.finish(1.95f, 1.8f);
}

// Mordida: estalo forte dos dentes (duas batidas), baque grave da mandibula e um esmagar molhado depois.
std::vector<float> bite(int rate) {
    Sketch s(1.0f, rate, 0xB17u);
    s.tone(0, 0.45f, 120, 36, 1.5f, 6.5f, 0.45f);
    s.tone(0, 0.05f, 1100, 380, 0.7f, 45.0f);       // "Clac" dos dentes.
    s.tone(0.018f, 0.05f, 900, 320, 0.6f, 45.0f);
    Filter crack(rate), crunch(rate), squelch(rate);
    float grain = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        const float noise = s.white();
        crack.run(noise, 2800.0f, 1.0f);
        const float snap = (t < 0.008f ? 1.0f - t / 0.008f : 0.0f) + (t > 0.018f && t < 0.028f ? 1.0f - (t - 0.018f) / 0.01f : 0.0f);
        // Esmagar em pedacos: rajadas curtas e irregulares.
        if (s.chance() < 0.0016f) grain = 1.0f;
        grain *= 0.9993f;
        crunch.run(noise, 950.0f, 0.7f);
        squelch.run(s.white(), 520.0f - 330.0f * ease(0.06f, 0.6f, t), 0.22f);
        const float after = t > 0.04f ? std::exp(-4.0f * (t - 0.04f)) : 0.0f;
        const float wet = t > 0.07f ? std::min((t - 0.07f) / 0.03f, 1.0f) * std::exp(-5.0f * (t - 0.07f)) : 0.0f;
        s.add(i, crack.high * 3.0f * snap + crunch.band * 1.4f * grain * after + squelch.band * 0.9f * wet);
    }
    s.echo(0.17f, 0.2f);
    return s.finish(1.95f, 2.0f);
}

// Mastigar: cada fechada da boca e um esmagar carnudo e molhado, com baque grave.
std::vector<float> chew(int rate) {
    Sketch s(0.6f, rate, 0xC4Eu);
    s.tone(0, 0.25f, 85, 40, 1.2f, 11.0f, 0.3f);
    Filter squish(rate), meat(rate), smack(rate);
    float grain = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        squish.run(s.white(), 420.0f - 250.0f * ease(0, 0.4f, t), 0.18f);   // "Nhoc" ressonante que desce.
        meat.run(s.white(), 700.0f, 0.8f);
        smack.run(s.white(), 2200.0f, 1.0f);
        if (s.chance() < 0.0012f) grain = 1.0f;
        grain *= 0.9992f;
        const float body = std::min(t / 0.015f, 1.0f) * std::exp(-6.0f * t);
        const float lip = t < 0.012f ? 1.0f - t / 0.012f : 0.0f;
        s.add(i, squish.band * 1.3f * body + meat.low * 0.9f * grain * body + smack.high * 0.8f * lip);
    }
    return s.finish(1.75f, 1.8f);
}

// Cuspida: um "PTUH" estourado e o sopro forte de ar e agua jogando o passaro para longe.
std::vector<float> spit(int rate) {
    Sketch s(1.3f, rate, 0x5917u);
    s.tone(0, 0.1f, 190, 55, 1.4f, 22.0f);
    s.tone(0.02f, 0.45f, 125, 68, 0.55f, 5.0f, 0.6f);   // A garganta soando junto.
    Filter pop(rate), blast(rate), gust(rate), spray(rate);
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        pop.run(s.white(), 600.0f, 0.9f);
        const float burst = t < 0.03f ? 1.0f - t / 0.03f : 0.0f;
        // O sopro abre (fica mais agudo) enquanto o ar sai e fecha de novo no fim.
        blast.run(s.white(), 260.0f + 1400.0f * ease(0, 0.3f, t) - 700.0f * ease(0.3f, 1.1f, t), 0.5f);
        gust.run(s.white(), 150.0f, 0.8f);
        spray.run(s.white(), 3200.0f, 1.2f);
        const float swell = std::min(t / 0.03f, 1.0f) * std::exp(-2.6f * std::max(0.0f, t - 0.05f));
        s.add(i, pop.low * 2.0f * burst + blast.band * 2.0f * swell + gust.low * 1.8f * swell +
                     spray.high * 0.22f * ease(0.04f, 0.15f, t) * std::exp(-4.0f * t));
    }
    s.droplets(0.2f, 1.2f, 18, 0.1f);
    s.echo(0.19f, 0.22f);
    return s.finish(1.9f, 1.7f);
}

// Ronco da baleia por perto: rosnado bem grave e aspero, com a respiracao do mar por baixo. Longo, sobe e
// desce devagar para emendar com o proximo.
std::vector<float> rumble(int rate) {
    Sketch s(2.4f, rate, 0x2B1u);
    Filter throat(rate), sea(rate);
    float phase = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i), u = t / s.seconds();
        const float hz = 56.0f - 14.0f * u + 2.5f * std::sin(kTwoPi * 0.8f * t) + 1.5f * std::sin(kTwoPi * 5.0f * t);
        phase += hz / rate;
        float voice = 0;
        for (int n = 1; n <= 7; n++) voice += std::sin(kTwoPi * n * phase) / std::pow(static_cast<float>(n), 0.9f);
        const float rough = 0.7f + 0.3f * std::sin(kTwoPi * 11.0f * t);   // Aspereza de rosnado.
        throat.run(voice * rough, 380.0f + 120.0f * std::sin(kPi * u), 0.6f);
        sea.run(s.white(), 90.0f, 0.8f);
        const float shape = std::pow(std::sin(kPi * u), 0.7f);
        s.add(i, (throat.low * 0.9f + std::sin(kTwoPi * 0.5f * phase) * 0.6f + sea.low * 2.2f) * shape);
    }
    s.echo(0.31f, 0.25f);
    return s.finish(1.9f, 1.5f);
}

// Canto da baleia no salto: um gemido grave que sobe, se sustenta com vibrato e desce longo ("uuuoooOOOuuu"),
// com um segundo grito mais agudo no meio, sub-grave por baixo e o eco do mar.
std::vector<float> song(int rate) {
    Sketch s(3.5f, rate, 0x50A6u);
    Filter vowel(rate), second(rate), breath(rate), cry(rate);
    float phase = 0, cryPhase = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const float t = s.time(i);
        // Contorno da voz: sobe ate o pico e cai devagar, com vibrato que so aparece depois do ataque.
        const float rise = ease(0, 0.9f, t), fall = ease(1.3f, 3.1f, t);
        const float vibrato = 1.0f + 0.028f * std::sin(kTwoPi * 5.2f * t) * ease(0.4f, 1.0f, t);
        const float hz = 78.0f * std::pow(155.0f / 78.0f, rise) * std::pow(62.0f / 155.0f, fall) * vibrato;
        phase += hz / rate;
        float voice = 0;
        for (int n = 1; n <= 10; n++) voice += std::sin(kTwoPi * n * phase) / std::pow(static_cast<float>(n), 1.1f);
        // A "vogal" abre e fecha com a voz: as formantes seguem o mesmo contorno.
        const float formant = 260.0f + 420.0f * rise - 470.0f * fall;
        vowel.run(voice, formant, 0.35f);
        second.run(voice, formant * 2.4f, 0.5f);
        breath.run(s.white(), formant, 0.6f);
        const float shape = ease(0, 0.45f, t) * (1.0f - ease(2.5f, 3.4f, t));
        float sample = (vowel.band * 1.2f + second.band * 0.35f + breath.band * 0.08f + std::sin(kTwoPi * 0.5f * phase) * 0.55f) * shape;
        // Grito do meio: um "uuip" que sobe e cai de novo.
        if (t > 1.35f && t < 2.75f) {
            const float u = (t - 1.35f) / 1.4f;
            const float cryHz = 190.0f * std::pow(340.0f / 190.0f, ease(0, 0.45f, u)) * std::pow(230.0f / 340.0f, ease(0.45f, 1.0f, u));
            cryPhase += cryHz * (1.0f + 0.02f * std::sin(kTwoPi * 6.0f * t)) / rate;
            const float tri = 2.0f * std::abs(2.0f * (cryPhase - std::floor(cryPhase + 0.5f))) - 1.0f;
            cry.run(tri, 900.0f, 0.5f);
            sample += cry.band * 0.5f * std::pow(std::sin(kPi * u), 1.5f);
        }
        s.add(i, sample);
    }
    s.echo(0.29f, 0.32f);
    return s.finish(1.9f, 1.4f);
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
        case WhaleSound::Song: return song(sampleRate);
    }
    return {};
}

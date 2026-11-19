#include "voice/VoiceDsp.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kFrameSeconds = static_cast<float>(kVoiceFrame) / kVoiceRate;
constexpr int kRingSize = 8192;   // Potencia de 2: o indice vira uma mascara.
constexpr int64_t kRingMask = kRingSize - 1;
constexpr float kPi = 3.14159265f;
constexpr int kAlignWindow = 240;   // 5 ms comparados para alinhar os graos.
constexpr int kAlignSearch = 576;   // Ate 12 ms para tras: um periodo inteiro de uma voz de 83 Hz.

float frameDb(const float* frame, int count) {
    double sum = 0;
    for (int i = 0; i < count; i++) sum += static_cast<double>(frame[i]) * frame[i];
    const double mean = count > 0 ? sum / count : 0;
    return static_cast<float>(10.0 * std::log10(mean + 1e-10));
}
}

// ---------------------------------------------------------------- SpeechDetector

SpeechDetector::SpeechDetector(SpeechSettings settings) : settings_(settings) {}

void SpeechDetector::reset() {
    levelDb_ = floorDb_ = -100.0f;
    hangover_ = 0;
    started_ = speaking_ = false;
}

float SpeechDetector::thresholdDb() const { return std::max(floorDb_ + settings_.marginDb, settings_.minimumDb); }

float SpeechDetector::level() const {
    if (!speaking_) return 0.0f;
    return std::clamp((levelDb_ - thresholdDb()) / 20.0f, 0.0f, 1.0f);
}

bool SpeechDetector::process(const float* frame, int count) {
    levelDb_ = frameDb(frame, count);
    if (!started_) {
        // O primeiro quadro calibra o ruido de fundo: comecar com ruido nao conta como fala.
        floorDb_ = levelDb_;
        started_ = true;
    } else if (levelDb_ < floorDb_) {
        floorDb_ = levelDb_;
    } else {
        floorDb_ = std::min(levelDb_, floorDb_ + settings_.floorRiseDbPerSecond * kFrameSeconds);
    }
    const int hangoverFrames = static_cast<int>(std::lround(settings_.hangoverSeconds / kFrameSeconds));
    if (levelDb_ > thresholdDb()) {
        speaking_ = true;
        hangover_ = hangoverFrames;
    } else if (hangover_ > 0) {
        hangover_--;
    } else {
        speaking_ = false;
    }
    return speaking_;
}

// ---------------------------------------------------------------- PitchShifter

PitchShifter::PitchShifter(float semitones, int grainSamples)
    : history_(kRingSize, 0.0f), window_(static_cast<size_t>(grainSamples)), grain_(grainSamples), hop_(grainSamples / 2) {
    // Hann periodica: w(n) + w(n + N/2) = 1, entao dois graos sobrepostos somam ganho 1.
    for (int i = 0; i < grain_; i++) window_[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * kPi * i / grain_);
    setSemitones(semitones);
}

void PitchShifter::setSemitones(float semitones) {
    const float ratio = std::pow(2.0f, std::clamp(semitones, -12.0f, 12.0f) / 12.0f);
    if (ratio == ratio_ && delay_ > 0) return;
    ratio_ = ratio;
    // Um grao que le mais rapido que o tempo real precisa de amostras do "futuro": atrasamos a leitura
    // o suficiente para que o fim do grao ainda caia em amostras ja recebidas, e para que a comparacao
    // de forma de onda no inicio de cada grao so use amostras que ja chegaram.
    const float faster = std::max(ratio_ - 1.0f, 0.0f);
    delay_ = std::max(static_cast<int>(std::ceil((grain_ - 1) * faster)) + 3,
                      kAlignWindow + 3 + static_cast<int>(std::ceil(hop_ * faster)));
    reset();
}

void PitchShifter::reset() {
    std::fill(history_.begin(), history_.end(), 0.0f);
    time_ = 0;
    starts_[0] = starts_[1] = 0;
}

// Escolhe o inicio do grao novo entre o ponto nominal e ate kAlignSearch amostras antes, onde o trecho de
// entrada mais se parece (correlacao normalizada) com o trecho que o grao anterior vai ler neste momento.
int64_t PitchShifter::alignedStart(int64_t grain) const {
    const int64_t nominal = grain * hop_ - delay_;
    if (grain == 0) return nominal;
    const int64_t previous = starts_[(grain - 1) & 1];
    const int64_t playing = previous + static_cast<int64_t>(std::lround(hop_ * ratio_));
    int64_t best = nominal;
    float bestScore = -1e30f;
    for (int64_t shift = 0; shift <= kAlignSearch; shift++) {
        const int64_t candidate = nominal - shift;
        float cross = 0, energy = 1e-9f;
        for (int i = 0; i < kAlignWindow; i += 2) {
            const float a = history_[static_cast<size_t>((playing + i) & kRingMask)];
            const float b = history_[static_cast<size_t>((candidate + i) & kRingMask)];
            cross += a * b;
            energy += b * b;
        }
        const float score = cross / std::sqrt(energy);
        if (score > bestScore) { bestScore = score; best = candidate; }
    }
    return best;
}

void PitchShifter::process(const float* in, float* out, int count) {
    for (int n = 0; n < count; n++) {
        const int64_t t = time_++;
        history_[static_cast<size_t>(t & kRingMask)] = in[n];
        if (ratio_ == 1.0f) {
            out[n] = in[n];
            continue;
        }
        const int64_t current = t / hop_;
        if (t % hop_ == 0) starts_[current & 1] = alignedStart(current);
        float sum = 0;
        for (int64_t k = current - 1; k <= current; k++) {
            if (k < 0) continue;
            const int64_t offset = t - k * hop_;   // Posicao dentro do grao, de 0 a grain-1.
            const double position = static_cast<double>(starts_[k & 1]) + static_cast<double>(offset) * ratio_;
            const double base = std::floor(position);
            const int64_t i = static_cast<int64_t>(base);
            if (i - 1 < 0) continue;
            const float frac = static_cast<float>(position - base);
            const float sample = hermite(history_[static_cast<size_t>((i - 1) & kRingMask)], history_[static_cast<size_t>(i & kRingMask)],
                                         history_[static_cast<size_t>((i + 1) & kRingMask)], history_[static_cast<size_t>((i + 2) & kRingMask)], frac);
            sum += window_[static_cast<size_t>(offset)] * sample;
        }
        out[n] = sum;
    }
}

// ---------------------------------------------------------------- DopplerResampler

namespace {
constexpr int kSpliceMin = 240, kSpliceMax = 720;   // Saltos de 5 a 15 ms: cobre o periodo de qualquer voz.
constexpr float kFactorSmoothing = 0.00052f;        // Constante de tempo de cerca de 40 ms.
}

DopplerResampler::DopplerResampler() : ring_(kRingSize, 0.0f) { reset(); }

void DopplerResampler::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    write_ = kRingSize;   // Comeca adiante para que todo o historico seja silencio valido.
    read_ = static_cast<double>(write_ - kMinLag - 8);
    fadeLeft_ = 0;
    factor_ = 1.0f;
}

void DopplerResampler::push(const float* in, int count) {
    for (int i = 0; i < count; i++) ring_[static_cast<size_t>(write_++ & kRingMask)] = in[i];
    // Uma rajada maior que o anel nao pode virar atraso: volta para o maior atraso permitido.
    if (static_cast<double>(write_) - read_ > kRingSize - 2 * kSpliceMax) {
        read_ = static_cast<double>(write_ - kMaxLag);
        fadeLeft_ = 0;
    }
}

float DopplerResampler::at(double position) const {
    const double base = std::floor(position);
    const int64_t i = static_cast<int64_t>(base);
    const float t = static_cast<float>(position - base);
    return hermite(ring_[static_cast<size_t>((i - 1) & kRingMask)], ring_[static_cast<size_t>(i & kRingMask)],
                   ring_[static_cast<size_t>((i + 1) & kRingMask)], ring_[static_cast<size_t>((i + 2) & kRingMask)], t);
}

// Procura o salto cujo trecho de destino mais se parece com o que ia tocar, como no WSOLA.
// Em voz isso cai num multiplo do periodo do tom e o salto quase nao se ouve.
int DopplerResampler::bestSplice(bool backward) const {
    const int64_t start = static_cast<int64_t>(read_);
    int best = kSpliceMin;
    float bestScore = -1e30f;
    for (int jump = kSpliceMin; jump <= kSpliceMax; jump += 2) {
        const int64_t other = backward ? start - jump : start + jump;
        float cross = 0, energy = 1e-9f;
        for (int i = 0; i < kFade; i += 2) {
            const float a = ring_[static_cast<size_t>((start + i) & kRingMask)];
            const float b = ring_[static_cast<size_t>((other + i) & kRingMask)];
            cross += a * b;
            energy += b * b;
        }
        const float score = cross / std::sqrt(energy);
        if (score > bestScore) { bestScore = score; best = jump; }
    }
    return best;
}

void DopplerResampler::render(float* out, int count, float factor) {
    const float target = std::isfinite(factor) ? std::clamp(factor, 0.25f, 4.0f) : 1.0f;
    for (int n = 0; n < count; n++) {
        factor_ += (target - factor_) * kFactorSmoothing;
        if (fadeLeft_ == 0) {
            const double lag = static_cast<double>(write_) - read_;
            if (lag < kMinLag || lag > kMaxLag) {
                const bool backward = lag < kMinLag;
                const int jump = bestSplice(backward);
                fadeRead_ = read_;
                read_ += backward ? -jump : jump;
                fadeLeft_ = kFade;
                splices_++;
            }
        }
        // Protecao: a leitura nunca passa da escrita, mesmo com fatores fora do previsto.
        const double limit = static_cast<double>(write_ - 3);
        read_ = std::min(read_, limit);
        float sample = at(read_);
        if (fadeLeft_ > 0) {
            fadeRead_ = std::min(fadeRead_, limit);
            const float old = static_cast<float>(fadeLeft_) / kFade;
            sample = sample * (1.0f - old) + at(fadeRead_) * old;
            fadeRead_ += factor_;
            fadeLeft_--;
        }
        out[n] = sample;
        read_ += factor_;
    }
}

// ---------------------------------------------------------------- Limiter

Limiter::Limiter(float threshold, float releaseSeconds)
    : threshold_(threshold), release_(std::exp(-1.0f / (releaseSeconds * kVoiceRate))) {}

void Limiter::process(float* stereo, int frames) {
    for (int i = 0; i < frames; i++) {
        float& left = stereo[2 * i];
        float& right = stereo[2 * i + 1];
        const float peak = std::max(std::abs(left), std::abs(right));
        envelope_ = std::max(peak, envelope_ * release_);
        gain_ = envelope_ > threshold_ ? threshold_ / envelope_ : 1.0f;
        left *= gain_;
        right *= gain_;
    }
}

namespace {
constexpr float kGainRiseDbPerFrame = 15.0f * kVoiceFrame / kVoiceRate;   // 15 dB por segundo.
constexpr float kGainFallShare = 0.5f;                                     // Metade do excesso por quadro.
constexpr float kSoftKnee = 0.85f;

float softClip(float x) {
    const float magnitude = std::abs(x);
    if (magnitude <= kSoftKnee) return x;
    const float over = (magnitude - kSoftKnee) / (1.0f - kSoftKnee);
    return std::copysign(kSoftKnee + (1.0f - kSoftKnee) * std::tanh(over), x);
}
}

AutoGain::AutoGain(float targetDb, float maxGainDb, float startGainDb)
    : targetDb_(targetDb), maxGainDb_(maxGainDb), gainDb_(startGainDb), appliedLinear_(std::pow(10.0f, startGainDb / 20.0f)) {}

void AutoGain::process(float* frame, int count, bool voiced) {
    if (voiced && count > 0) {
        double energy = 0;
        for (int i = 0; i < count; i++) energy += static_cast<double>(frame[i]) * frame[i];
        const float levelDb = 10.0f * std::log10(static_cast<float>(energy / count) + 1e-12f);
        const float wanted = std::clamp(targetDb_ - levelDb, 0.0f, maxGainDb_);
        if (wanted < gainDb_) gainDb_ -= (gainDb_ - wanted) * kGainFallShare;
        else gainDb_ = std::min(wanted, gainDb_ + kGainRiseDbPerFrame);
    }
    const float target = std::pow(10.0f, gainDb_ / 20.0f);
    const float step = (target - appliedLinear_) / std::max(count, 1);
    for (int i = 0; i < count; i++) {
        appliedLinear_ += step;
        frame[i] = softClip(frame[i] * appliedLinear_);
    }
}

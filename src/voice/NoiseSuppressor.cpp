#include "voice/NoiseSuppressor.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kHopSeconds = static_cast<float>(NoiseSuppressor::kHop) / kVoiceRate;
constexpr int kLearnHops = 20;                 // 200 ms iniciais so aprendem o ruido.
constexpr float kSmoothing = 0.7f;             // Suavizacao da potencia por frequencia.
constexpr int kHopsBeforeRise = 150;           // 1,5 s sem voz antes de aceitar um ruido mais forte.
constexpr float kOverSubtraction = 1.6f;       // Margem acima da media do ruido.
constexpr float kFastDown = 0.3f;              // Queda rapida quando o som fica mais baixo.
constexpr float kNoiseLearning = 0.03f;        // Cerca de 330 ms para acompanhar um ruido que mudou.
constexpr float kSignalGuard = 4.0f;           // 6 dB acima do ruido ja pode ser voz: nao entra na media.
constexpr float kRiseDbPerSecond = 3.0f;       // Ruido mais forte que o guarda sobe a 3 dB/s.
constexpr float kDecisionDirected = 0.96f;
constexpr float kGainFloor = 0.1f;             // -20 dB: o fundo fica bem baixo, mas sem buracos.
constexpr float kBandLowHz = 300.0f, kBandHighHz = 3400.0f;
constexpr int kMinLag = kVoiceRate / 480, kMaxLag = kVoiceRate / 80;
constexpr float kOnsetSnrDb = 7.0f, kOnsetHarmonicity = 0.45f;

float riseFactor(float dbPerSecond) { return std::pow(10.0f, dbPerSecond * kHopSeconds / 10.0f); }
}

HighPass::HighPass(float cutoffHz) {
    const float w = 2.0f * kPi * cutoffHz / kVoiceRate, alpha = std::sin(w) / (2.0f * 0.7071f), c = std::cos(w);
    const float a0 = 1.0f + alpha;
    b0_ = (1.0f + c) / 2.0f / a0;
    b1_ = -(1.0f + c) / a0;
    b2_ = b0_;
    a1_ = -2.0f * c / a0;
    a2_ = (1.0f - alpha) / a0;
}

void HighPass::process(float* samples, int count) {
    for (int i = 0; i < count; i++) {
        const float x = samples[i];
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        samples[i] = y;
    }
}

NoiseSuppressor::NoiseSuppressor()
    : window_(kWindow), history_(kWindow, 0.0f), overlap_(kHop, 0.0f), smoothed_(kFft / 2 + 1, 0.0f),
      noise_(kFft / 2 + 1, 0.0f), previousGain_(kFft / 2 + 1, 1.0f), previousSnr_(kFft / 2 + 1, 1.0f),
      spectrum_(1, kFft, CV_32FC2), time_(1, kFft, CV_32F), correlation_(1, 2 * kFft, CV_32F) {
    // Raiz de Hann periodica: analise e sintese juntas somam 1 com 50% de sobreposicao.
    for (int i = 0; i < kWindow; i++) window_[i] = std::sqrt(0.5f - 0.5f * std::cos(2.0f * kPi * i / kWindow));
}

void NoiseSuppressor::updateNoise(int bin, float power) {
    float& noise = noise_[static_cast<size_t>(bin)];
    if (hops_ < kLearnHops) {
        noise = hops_ == 0 ? power : noise + 0.2f * (power - noise);
        return;
    }
    const bool quiet = !speaking_ && !voiceLikely_;
    if (power < noise) {
        // Ficou mais silencioso: o ruido verdadeiro e no maximo isso. Desce rapido, sempre.
        noise += kFastDown * (power - noise);
        return;
    }
    if (quiet && power < noise * kSignalGuard) {
        // Sem voz e sem pico nesta frequencia: e ruido. A estimativa segue a media, para cima e para baixo.
        noise += kNoiseLearning * (power - noise);
        return;
    }
    // Pico ou voz: so sobe se faz tempo que ninguem fala (um ruido novo e mais forte),
    // para a fala e as pausas entre silabas nunca virarem "ruido".
    if (quiet && hopsSinceVoice_ > kHopsBeforeRise) noise *= riseFactor(kRiseDbPerSecond);
}

void NoiseSuppressor::processHop(const float* in, float* out) {
    std::move(history_.begin() + kHop, history_.end(), history_.begin());
    std::copy(in, in + kHop, history_.end() - kHop);

    float* t = time_.ptr<float>();
    for (int i = 0; i < kFft; i++) t[i] = i < kWindow ? history_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)] : 0.0f;
    cv::dft(time_, spectrum_, cv::DFT_COMPLEX_OUTPUT);

    cv::Vec2f* bins = spectrum_.ptr<cv::Vec2f>();
    const float binHz = static_cast<float>(kVoiceRate) / kFft;
    bandSignal_ = bandNoise_ = 0;
    for (int k = 0; k <= kFft / 2; k++) {
        const float power = bins[k][0] * bins[k][0] + bins[k][1] * bins[k][1];
        float& smooth = smoothed_[static_cast<size_t>(k)];
        smooth = hops_ == 0 ? power : kSmoothing * smooth + (1.0f - kSmoothing) * power;
        updateNoise(k, smooth);
        const float noise = std::max(noise_[static_cast<size_t>(k)] * kOverSubtraction, 1e-12f);
        // A potencia suavizada oscila menos que a instantanea: o ruido residual nao "pisca".
        const float posterior = (0.5f * power + 0.5f * smooth) / noise;
        const float prior = kDecisionDirected * previousGain_[static_cast<size_t>(k)] * previousGain_[static_cast<size_t>(k)] *
                                previousSnr_[static_cast<size_t>(k)] +
                            (1.0f - kDecisionDirected) * std::max(posterior - 1.0f, 0.0f);
        const float gain = hops_ < kLearnHops ? kGainFloor : std::max(prior / (1.0f + prior), kGainFloor);
        previousGain_[static_cast<size_t>(k)] = gain;
        previousSnr_[static_cast<size_t>(k)] = posterior;
        bins[k] *= gain;
        if (k > 0 && k < kFft / 2) bins[kFft - k] *= gain;
        const float hz = k * binHz;
        if (hz >= kBandLowHz && hz <= kBandHighHz) {
            bandSignal_ += smooth;
            bandNoise_ += noise_[static_cast<size_t>(k)];
        }
    }
    cv::dft(spectrum_, time_, cv::DFT_INVERSE | cv::DFT_SCALE | cv::DFT_REAL_OUTPUT);
    for (int i = 0; i < kHop; i++) out[i] = overlap_[static_cast<size_t>(i)] + t[i] * window_[static_cast<size_t>(i)];
    for (int i = 0; i < kHop; i++) overlap_[static_cast<size_t>(i)] = t[i + kHop] * window_[static_cast<size_t>(i + kHop)];
    hops_ = std::min(hops_ + 1, 1 << 20);
}

// Autocorrelacao normalizada do quadro limpo, por FFT com espaco suficiente para nao dar a volta.
void NoiseSuppressor::measureVoice(const float* frame, int count) {
    cv::Mat padded = cv::Mat::zeros(1, 2 * kFft, CV_32F), freq;
    std::copy(frame, frame + count, padded.ptr<float>());
    cv::dft(padded, freq, cv::DFT_COMPLEX_OUTPUT);
    cv::Vec2f* f = freq.ptr<cv::Vec2f>();
    for (int k = 0; k < 2 * kFft; k++) f[k] = cv::Vec2f(f[k][0] * f[k][0] + f[k][1] * f[k][1], 0.0f);
    cv::dft(freq, correlation_, cv::DFT_INVERSE | cv::DFT_SCALE | cv::DFT_REAL_OUTPUT);
    const float* r = correlation_.ptr<float>();
    float best = 0;
    if (r[0] > 1e-9f)
        for (int lag = kMinLag; lag <= kMaxLag && lag < count; lag++)
            best = std::max(best, r[lag] / r[0] * count / static_cast<float>(count - lag));
    harmonicity_ = std::min(best, 1.0f);
    bandSnrDb_ = 10.0f * std::log10((bandSignal_ + 1e-12f) / (bandNoise_ * kOverSubtraction + 1e-12f));
    voiceLikely_ = hops_ >= kLearnHops && bandSnrDb_ > kOnsetSnrDb && harmonicity_ > kOnsetHarmonicity;
}

void NoiseSuppressor::process(const float* in, float* out, int count) {
    for (int start = 0; start + kHop <= count; start += kHop) processHop(in + start, out + start);
    measureVoice(out, count);
    hopsSinceVoice_ = voiceLikely_ || speaking_ ? 0 : hopsSinceVoice_ + count / kHop;
}

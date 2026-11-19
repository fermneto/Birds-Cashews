#pragma once
#include "voice/VoiceDsp.h"
#include <opencv2/core.hpp>
#include <vector>

// Filtro passa-alta de 2a ordem (Butterworth, RBJ): remove zumbido e batidas graves abaixo da voz.
class HighPass {
public:
    explicit HighPass(float cutoffHz = 100.0f);
    void process(float* samples, int count);

private:
    float b0_, b1_, b2_, a1_, a2_;
    float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

// Reducao de ruido espectral e reconhecimento de voz humana.
//
// 1. STFT: janelas de 20 ms (raiz de Hann) a cada 10 ms, FFT de 1024 pontos.
// 2. Ruido por frequencia: nos quadros sem voz, segue a media da potencia; com voz, so pode descer
//    e quase nao sobe, para a propria fala nao virar "ruido".
// 3. Ganho de Wiener com SNR a priori "decision-directed" (Ephraim-Malah), com piso de -20 dB:
//    o ruido estacionario some sem criar o chiado musical da subtracao simples.
// 4. Reconstrucao por sobreposicao e soma. Atraso de 10 ms.
//
// Para saber se e voz, mede no quadro limpo: a relacao sinal-ruido na faixa da fala (300 a 3400 Hz)
// e a periodicidade (autocorrelacao com pico entre 80 e 480 Hz), que existe na voz por causa das
// cordas vocais e quase nunca em ventilador, teclado, cliques ou barulho de rua.
class NoiseSuppressor {
public:
    NoiseSuppressor();

    void process(const float* in, float* out, int count);   // count = kVoiceFrame.
    void setSpeaking(bool speaking) { speaking_ = speaking; }

    bool voiceLikely() const { return voiceLikely_; }
    float bandSnrDb() const { return bandSnrDb_; }
    float harmonicity() const { return harmonicity_; }
    static constexpr int latency() { return kHop; }

    static constexpr int kHop = kVoiceFrame / 2;
    static constexpr int kWindow = kVoiceFrame;
    static constexpr int kFft = 1024;

private:
    void processHop(const float* in, float* out);
    void updateNoise(int bin, float power);
    void measureVoice(const float* frame, int count);

    std::vector<float> window_, history_, overlap_;
    std::vector<float> smoothed_, noise_, previousGain_, previousSnr_;
    cv::Mat spectrum_, time_, correlation_;
    int hops_ = 0, hopsSinceVoice_ = 0;
    bool speaking_ = false, voiceLikely_ = false;
    float bandSnrDb_ = 0, harmonicity_ = 0, bandSignal_ = 0, bandNoise_ = 0;
};

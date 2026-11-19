#pragma once
#include "shared/VoiceLink.h"
#include "voice/NoiseSuppressor.h"
#include "voice/OpusCodec.h"
#include "voice/VoiceDsp.h"
#include <deque>
#include <vector>

// Caminho da voz local: passa-alta -> reducao de ruido -> deteccao de voz -> ganho -> voz de passaro -> Opus.
// So comeca a transmitir quando o som tem cara de voz humana; ruidos de fora ficam de fora.
// Codifica todos os quadros (o Opus precisa do historico), mas so devolve os que tem fala.
// Na abertura da fala tambem devolve os ultimos quadros (60 ms), para nao perder o comeco da palavra.
class VoiceSender {
public:
    VoiceSender(const SpeechSettings& speech, bool birdVoice, float semitones, int bitrate);

    // Um quadro de kVoiceFrame amostras do microfone. Devolve 0, 1 ou 2 quadros prontos para enviar.
    std::vector<VoiceFrame> process(const float* frame, PlayerId speaker, bool muted);
    void setBirdVoice(bool enabled, float semitones);
    void setNoiseSuppression(bool enabled) { denoise_ = enabled; }
    const NoiseSuppressor& suppressor() const { return suppressor_; }

    bool speaking() const { return speaking_; }
    float level() const { return speaking_ ? detector_.level() : 0.0f; }
    float gainDb() const { return gain_.gainDb(); }
    const SpeechDetector& detector() const { return detector_; }
    bool ok() const { return encoder_.ok(); }
    int latency() const { return (birdVoice_ ? shifter_.latency() : 0) + (denoise_ ? NoiseSuppressor::latency() : 0); }

private:
    HighPass highPass_;
    NoiseSuppressor suppressor_;
    bool denoise_ = true;
    SpeechDetector detector_;
    AutoGain gain_;
    PitchShifter shifter_;
    VoiceEncoder encoder_;
    bool birdVoice_;
    bool speaking_ = false;
    uint16_t sequence_ = 0;
    std::deque<std::vector<uint8_t>> previous_;
    std::vector<float> shifted_, boosted_, cleaned_;
};

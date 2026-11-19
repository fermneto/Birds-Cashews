#pragma once
#include "shared/VoiceLink.h"
#include "voice/OpusCodec.h"
#include "voice/VoiceDsp.h"
#include <vector>

// Caminho da voz local: deteccao de fala -> voz de passaro -> Opus.
// Codifica todos os quadros (o Opus precisa do historico), mas so devolve os que tem fala.
// Na abertura da fala tambem devolve o quadro anterior, para nao perder o comeco da palavra.
class VoiceSender {
public:
    VoiceSender(const SpeechSettings& speech, bool birdVoice, float semitones, int bitrate);

    // Um quadro de kVoiceFrame amostras do microfone. Devolve 0, 1 ou 2 quadros prontos para enviar.
    std::vector<VoiceFrame> process(const float* frame, PlayerId speaker, bool muted);
    void setBirdVoice(bool enabled, float semitones);

    bool speaking() const { return speaking_; }
    float level() const { return speaking_ ? detector_.level() : 0.0f; }
    const SpeechDetector& detector() const { return detector_; }
    bool ok() const { return encoder_.ok(); }
    int latency() const { return birdVoice_ ? shifter_.latency() : 0; }

private:
    SpeechDetector detector_;
    PitchShifter shifter_;
    VoiceEncoder encoder_;
    bool birdVoice_;
    bool speaking_ = false;
    uint16_t sequence_ = 0;
    std::vector<uint8_t> previous_;
    std::vector<float> shifted_;
};

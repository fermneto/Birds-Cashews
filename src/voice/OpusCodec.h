#pragma once
#include "voice/VoiceDsp.h"
#include <cstdint>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

// Codificador Opus para voz: 48 kHz mono, quadros de 20 ms, cerca de 24 kbps,
// com correcao de erro embutida (FEC) para recuperar um quadro perdido a partir do seguinte.
class VoiceEncoder {
public:
    explicit VoiceEncoder(int bitrate = 24000, int complexity = 5);
    ~VoiceEncoder();
    VoiceEncoder(const VoiceEncoder&) = delete;
    VoiceEncoder& operator=(const VoiceEncoder&) = delete;

    bool ok() const { return encoder_ != nullptr; }
    // Codifica kVoiceFrame amostras. Vazio em caso de erro. Nunca passa de kMaxVoicePayload bytes.
    std::vector<uint8_t> encode(const float* frame);

private:
    OpusEncoder* encoder_ = nullptr;
};

// Decodificador Opus com ocultacao de perdas.
class VoiceDecoder {
public:
    VoiceDecoder();
    ~VoiceDecoder();
    VoiceDecoder(const VoiceDecoder&) = delete;
    VoiceDecoder& operator=(const VoiceDecoder&) = delete;

    bool ok() const { return decoder_ != nullptr; }
    // Cada chamada escreve exatamente kVoiceFrame amostras (silencio se der erro).
    bool decode(const std::vector<uint8_t>& payload, float* out);
    // Quadro perdido: usa o FEC do quadro seguinte se houver, senao a ocultacao do Opus.
    void conceal(const std::vector<uint8_t>* next, float* out);
    void reset();

private:
    OpusDecoder* decoder_ = nullptr;
};

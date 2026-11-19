#pragma once
#include "shared/GameTypes.h"
#include <cstdint>
#include <vector>

// Um quadro de voz ja comprimido, com quem falou e a ordem de envio.
struct VoiceFrame {
    PlayerId speaker = kNoPlayer;
    uint16_t sequence = 0;
    std::vector<uint8_t> payload;   // Opus, no maximo kMaxVoicePayload bytes.
};

constexpr size_t kMaxVoicePayload = 400;

// Transporte da voz. A rede repassa os quadros pelo host; os testes usam uma ligacao em memoria.
class VoiceLink {
public:
    virtual ~VoiceLink() = default;
    virtual void sendVoice(const VoiceFrame& frame) = 0;        // Voz do jogador local.
    virtual std::vector<VoiceFrame> receiveVoice() = 0;         // Voz dos outros, cada quadro uma unica vez.
};

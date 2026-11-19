#pragma once
#include "shared/GameTypes.h"
#include "shared/VoiceLink.h"
#include "voice/Spatializer.h"
#include "voice/VoiceDevices.h"
#include "voice/VoiceDsp.h"
#include "voice/VoiceMixer.h"
#include "voice/VoiceSender.h"
#include <vector>

struct VoiceSettings {
    bool useDevices = true;      // false: sem SDL, o audio entra por feedMicrophone e sai por renderOutput (testes).
    bool birdVoice = true;
    float pitchSemitones = 5.0f;
    int bitrate = 24000;
    int jitterFrames = 3;        // 60 ms.
    int outputQueueMs = 40;      // Quanto audio fica na fila da saida, contra soluços da thread principal.
    float volume = 1.0f;
    SpeechSettings speech;
    SpatialSettings spatial;
};

struct VoiceStats {
    int framesSent = 0, framesReceived = 0, capturedFramesDropped = 0;
};

// Voz por proximidade. Tudo roda na thread principal, numa chamada de update() por quadro do jogo:
//   microfone -> deteccao de fala -> voz de passaro -> Opus -> VoiceLink
//   VoiceLink -> buffer de jitter por falante -> Opus -> Doppler -> distancia e pan -> mixagem -> saida
// Sem microfone, sem saida ou sem Opus, as partes que faltam viram silencio e o jogo segue.
class VoiceChat {
public:
    explicit VoiceChat(VoiceSettings settings = VoiceSettings{});

    bool start();   // Abre os dispositivos. Falso quando nao ha nenhum (o resto continua funcionando).
    void attach(VoiceLink* link, PlayerId local);
    void update(float dt, const VoiceListener& me, const std::vector<PlayerPublicState>& others);

    bool localSpeaking() const { return sender_.speaking(); }
    float localLevel() const { return sender_.level(); }   // 0 a 1, para abrir mais ou menos o bico.
    bool speaking(PlayerId player) const { return mixer_.speaking(player); }

    void setMuted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }
    void setBirdVoice(bool enabled, float semitones = 5.0f);
    void setVolume(float volume);
    const VoiceSettings& settings() const { return settings_; }
    bool hasMicrophone() const { return devices_.hasCapture(); }
    bool hasOutput() const { return devices_.hasOutput(); }
    const VoiceStats& stats() const { return stats_; }
    const VoiceMixer& mixer() const { return mixer_; }

    // Sem dispositivos (useDevices = false): entrada e saida em memoria.
    void feedMicrophone(const float* samples, size_t count);
    void renderOutput(float* stereo);   // Um bloco de kVoiceFrame quadros estereo intercalados.

private:
    void processCapture();

    VoiceSettings settings_;
    VoiceDevices devices_;
    VoiceSender sender_;
    VoiceMixer mixer_;
    VoiceLink* link_ = nullptr;
    PlayerId local_ = kNoPlayer;
    bool muted_ = false;
    std::vector<float> captured_;
    std::vector<float> block_;
    VoiceStats stats_;
};

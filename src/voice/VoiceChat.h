#pragma once
#include "shared/GameTypes.h"
#include "shared/VoiceLink.h"
#include "voice/Spatializer.h"
#include "voice/VoiceDevices.h"
#include "voice/VoiceDsp.h"
#include "voice/VoiceMixer.h"
#include "voice/VoiceSender.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

struct VoiceSettings {
    bool useDevices = true;      // false: sem SDL, o audio entra por feedMicrophone e sai por renderOutput (testes).
    bool audioThread = true;     // Com dispositivos, microfone e saida rodam numa thread propria.
    bool birdVoice = true;
    float pitchSemitones = 8.0f;   // Bem mais aguda que a voz original.
    int bitrate = 24000;
    int jitterFrames = 4;        // 80 ms de partida; cresce sozinho se a rede ficar irregular.
    int outputQueueMs = 60;      // Quanto audio fica na fila da saida.
    float volume = 1.6f;           // A voz fica acima dos efeitos do jogo; o limitador evita saturar.
    SpeechSettings speech;
    SpatialSettings spatial;
};

struct VoiceStats {
    int framesSent = 0, framesReceived = 0, capturedFramesDropped = 0;
};

// Voz por proximidade:
//   microfone -> deteccao de fala -> ganho -> voz de passaro -> Opus -> VoiceLink
//   VoiceLink -> buffer de jitter por falante -> Opus -> Doppler -> distancia e pan -> mixagem -> saida
// Com dispositivos, captura, codificacao, mixagem e saida rodam numa thread de audio a cada 5 ms, entao
// um quadro lento do jogo nao corta a voz. A thread principal so troca os quadros com a rede e informa as
// posicoes em update(). Sem dispositivos (testes), tudo roda dentro de update().
// Sem microfone, sem saida ou sem Opus, as partes que faltam viram silencio e o jogo segue.
class VoiceChat {
public:
    explicit VoiceChat(VoiceSettings settings = VoiceSettings{});
    ~VoiceChat();
    VoiceChat(const VoiceChat&) = delete;
    VoiceChat& operator=(const VoiceChat&) = delete;

    bool start();   // Abre os dispositivos. Falso quando nao ha nenhum (o resto continua funcionando).
    void attach(VoiceLink* link, PlayerId local);
    void update(float dt, const VoiceListener& me, const std::vector<PlayerPublicState>& others);

    bool localSpeaking() const { return speaking_.load(); }
    float localLevel() const { return level_.load(); }   // 0 a 1, para abrir mais ou menos o bico.
    float microphoneGainDb() const { return gainDb_.load(); }
    bool speaking(PlayerId player) const;
    bool threaded() const { return audio_.joinable(); }

    void setMuted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_.load(); }
    void setBirdVoice(bool enabled, float semitones = 8.0f);
    void setVolume(float volume);
    const VoiceSettings& settings() const { return settings_; }
    bool hasMicrophone() const { return devices_.hasCapture(); }
    bool hasOutput() const { return devices_.hasOutput(); }
    VoiceStats stats() const;   // Copia protegida: a thread de audio atualiza os numeros.
    const VoiceMixer& mixer() const { return mixer_; }

    // Sem dispositivos (useDevices = false): entrada e saida em memoria.
    void feedMicrophone(const float* samples, size_t count);
    void renderOutput(float* stereo);   // Um bloco de kVoiceFrame quadros estereo intercalados.

private:
    void processCapture();
    void fillOutput();
    void pumpAudio();
    void audioLoop();
    void stopAudioThread();

    VoiceSettings settings_;
    VoiceDevices devices_;
    VoiceSender sender_;
    VoiceMixer mixer_;
    VoiceLink* link_ = nullptr;
    PlayerId local_ = kNoPlayer;
    std::atomic<bool> muted_{false};
    std::vector<float> captured_;
    std::vector<float> block_;
    std::vector<VoiceFrame> outbox_;   // Voz pronta para a rede, enviada pela thread principal.
    VoiceStats stats_;
    mutable std::mutex mutex_;
    std::thread audio_;
    std::atomic<bool> running_{false}, speaking_{false};
    std::atomic<float> level_{0.0f}, gainDb_{0.0f};
};

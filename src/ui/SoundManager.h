#pragma once
#include "shared/GameplayView.h"
#include "ui/Ambience.h"
#include <SDL.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

enum class UiSound { Move, Confirm, Back, Mark, Collect, Deliver, Upgrade, Denied, Record, NestMove, Landed, TakeOff,
                     Spill, Steal, Alarm, Knock,
                     Flap,                                              // Sopro de uma batida de asa.
                     ChirpTweet, ChirpTrill, ChirpWhistle, ChirpCaw,     // Cantos dos NPCs, do menor ao maior.
                     PlaneCrash,                                        // Batida do aviao do teto.
                     WhaleSpout, WhaleSplash, WhaleBite, WhaleChew, WhaleSpit, WhaleRumble,   // Baleia do limite (ui/WhaleSounds).
                     Count };

UiSound soundFor(GameplayFeedback feedback);

// Sintetiza os efeitos ao abrir e mistura tudo na thread de audio do SDL.
// Sem dispositivo de audio, todas as chamadas viram silencio.
class SoundManager {
public:
    enum class Output { Device, Silent };

    explicit SoundManager(Output output = Output::Device);
    ~SoundManager();
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    bool hasDevice() const { return device_ != 0; }
    void play(UiSound sound, float volume = 1.0f);
    void play(GameplayFeedback feedback) { play(soundFor(feedback)); }
    // Volume desejado de cada camada do ambiente. A mistura chega nele aos poucos, sem estalos.
    void setAmbience(const AmbientMix& mix);
    AmbientMix ambience() const;
    // Motor do aviao do teto: volume (0 a 1) e velocidade de leitura do loop, que faz o Doppler.
    void setPlaneEngine(float volume, float rate);

    static std::vector<int16_t> synthesize(UiSound sound);
    static int sampleRate();

private:
    struct Voice {
        const std::vector<int16_t>* samples = nullptr;
        size_t position = 0;
        float gain = 1;
    };
    static constexpr int kVoices = 8;

    static void audioCallback(void* self, Uint8* stream, int bytes);
    void mix(int16_t* output, int count);
    void mixAmbience(int count);
    void openDevice();

    SDL_AudioDeviceID device_ = 0;
    bool subsystemOpen_ = false;
    std::array<std::vector<int16_t>, static_cast<size_t>(UiSound::Count)> bank_;
    std::array<Voice, kVoices> voices_{};
    std::vector<int> mixBuffer_;
    std::array<std::vector<int16_t>, kAmbientLayers> ambientLoops_;
    std::array<std::atomic<float>, kAmbientLayers> ambientTarget_{};
    std::array<float, kAmbientLayers> ambientGain_{};     // So a thread de audio mexe.
    std::array<size_t, kAmbientLayers> ambientPosition_{};
    void mixPlaneEngine(int count);
    std::vector<int16_t> engineLoop_;
    std::atomic<float> engineTarget_{0}, engineRateTarget_{1};
    float engineGain_ = 0, engineRate_ = 1;   // So a thread de audio mexe.
    double enginePosition_ = 0;
};

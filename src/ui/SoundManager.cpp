#include "ui/SoundManager.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr int kSampleRate = 44100;
constexpr int kBufferSamples = 1024;
constexpr float kTwoPi = 6.2831853f;
constexpr float kMasterVolume = 0.5f;

enum class Wave { Sine, Triangle, Square };

// Uma nota com glissando, que comeca "start" segundos depois do inicio do efeito.
struct Tone {
    float start, seconds, fromHz, toHz, volume;
    Wave wave;
};

float oscillator(Wave wave, float phase) {
    const float turn = phase - std::floor(phase);
    switch (wave) {
        case Wave::Triangle: return 4.0f * std::abs(turn - 0.5f) - 1.0f;
        case Wave::Square: return turn < 0.5f ? 0.6f : -0.6f;
        default: return std::sin(kTwoPi * turn);
    }
}

// Ataque curto e queda exponencial evitam estalos no comeco e no fim da nota.
float envelope(float t, float seconds) {
    const float attack = std::min(t / 0.006f, 1.0f);
    const float release = std::min((seconds - t) / 0.02f, 1.0f);
    return attack * release * std::exp(-2.5f * t / seconds);
}

void addTone(std::vector<float>& mix, const Tone& tone) {
    const size_t first = static_cast<size_t>(tone.start * kSampleRate);
    const size_t count = static_cast<size_t>(tone.seconds * kSampleRate);
    if (mix.size() < first + count) mix.resize(first + count, 0.0f);
    float phase = 0;
    for (size_t i = 0; i < count; i++) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float hz = tone.fromHz + (tone.toHz - tone.fromHz) * (t / tone.seconds);
        phase += hz / kSampleRate;
        mix[first + i] += oscillator(tone.wave, phase) * envelope(t, tone.seconds) * tone.volume;
    }
}

std::vector<Tone> tonesFor(UiSound sound) {
    switch (sound) {
        case UiSound::Move: return {{0, 0.05f, 620, 700, 0.5f, Wave::Triangle}};
        case UiSound::Confirm: return {{0, 0.07f, 660, 660, 0.55f, Wave::Triangle}, {0.07f, 0.12f, 990, 990, 0.55f, Wave::Triangle}};
        case UiSound::Back: return {{0, 0.09f, 520, 340, 0.5f, Wave::Triangle}};
        case UiSound::Mark: return {{0, 0.08f, 880, 880, 0.5f, Wave::Sine}, {0.08f, 0.16f, 1320, 1320, 0.5f, Wave::Sine}};
        case UiSound::Collect: return {{0, 0.1f, 780, 1250, 0.7f, Wave::Sine}};
        case UiSound::Deliver: return {{0, 0.09f, 523, 523, 0.6f, Wave::Triangle}, {0.09f, 0.09f, 659, 659, 0.6f, Wave::Triangle},
                                       {0.18f, 0.2f, 784, 784, 0.6f, Wave::Triangle}};
        case UiSound::Upgrade: return {{0, 0.1f, 440, 660, 0.55f, Wave::Square}, {0.1f, 0.24f, 660, 1100, 0.55f, Wave::Square}};
        case UiSound::Denied: return {{0, 0.12f, 196, 170, 0.65f, Wave::Square}, {0.14f, 0.18f, 165, 130, 0.65f, Wave::Square}};
        case UiSound::Record: return {{0, 0.12f, 523, 523, 0.55f, Wave::Square}, {0.12f, 0.12f, 659, 659, 0.55f, Wave::Square},
                                      {0.24f, 0.12f, 784, 784, 0.55f, Wave::Square}, {0.36f, 0.5f, 1047, 1047, 0.6f, Wave::Square},
                                      {0.36f, 0.5f, 523, 523, 0.3f, Wave::Triangle}};
        case UiSound::NestMove: return {{0, 0.06f, 500, 560, 0.5f, Wave::Sine}};
        case UiSound::Landed: return {{0, 0.16f, 330, 180, 0.7f, Wave::Sine}, {0.08f, 0.14f, 660, 880, 0.3f, Wave::Triangle}};
        case UiSound::TakeOff: return {{0, 0.32f, 220, 760, 0.6f, Wave::Triangle}};
        case UiSound::Count: break;
    }
    return {};
}
}

UiSound soundFor(GameplayFeedback feedback) {
    switch (feedback) {
        case GameplayFeedback::CashewCollected: return UiSound::Collect;
        case GameplayFeedback::CashewDelivered: return UiSound::Deliver;
        case GameplayFeedback::UpgradeBought: return UiSound::Upgrade;
        case GameplayFeedback::UpgradeDenied: return UiSound::Denied;
        case GameplayFeedback::RecordBeaten: return UiSound::Record;
        case GameplayFeedback::MenuMove: return UiSound::NestMove;
        case GameplayFeedback::NestLanded: return UiSound::Landed;
        case GameplayFeedback::TakeOff: return UiSound::TakeOff;
    }
    return UiSound::Move;
}

int SoundManager::sampleRate() { return kSampleRate; }

std::vector<int16_t> SoundManager::synthesize(UiSound sound) {
    std::vector<float> mix;
    for (const Tone& tone : tonesFor(sound)) addTone(mix, tone);
    std::vector<int16_t> samples(mix.size());
    std::transform(mix.begin(), mix.end(), samples.begin(), [](float value) {
        return static_cast<int16_t>(std::clamp(value * kMasterVolume, -1.0f, 1.0f) * 32767.0f);
    });
    return samples;
}

SoundManager::SoundManager(Output output) : mixBuffer_(kBufferSamples * 4, 0) {
    if (output == Output::Silent) return;
    for (size_t i = 0; i < bank_.size(); i++) bank_[i] = synthesize(static_cast<UiSound>(i));
    openDevice();
}

void SoundManager::openDevice() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::cerr << "Audio indisponivel, jogando em silencio: " << SDL_GetError() << "\n";
        return;
    }
    subsystemOpen_ = true;
    SDL_AudioSpec wanted{};
    wanted.freq = kSampleRate;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 1;
    wanted.samples = kBufferSamples;
    wanted.callback = &SoundManager::audioCallback;
    wanted.userdata = this;
    SDL_AudioSpec obtained{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
    if (device_ == 0) {
        std::cerr << "Audio indisponivel, jogando em silencio: " << SDL_GetError() << "\n";
        return;
    }
    SDL_PauseAudioDevice(device_, 0);
}

SoundManager::~SoundManager() {
    if (device_ != 0) SDL_CloseAudioDevice(device_);
    if (subsystemOpen_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SoundManager::play(UiSound sound) {
    if (device_ == 0) return;
    const std::vector<int16_t>& samples = bank_[static_cast<size_t>(sound)];
    SDL_LockAudioDevice(device_);
    // Com todas as vozes ocupadas, substitui a que esta mais perto do fim.
    Voice* chosen = &voices_[0];
    for (Voice& voice : voices_) {
        if (!voice.samples) { chosen = &voice; break; }
        if (voice.position > chosen->position) chosen = &voice;
    }
    chosen->samples = &samples;
    chosen->position = 0;
    SDL_UnlockAudioDevice(device_);
}

void SoundManager::audioCallback(void* self, Uint8* stream, int bytes) {
    static_cast<SoundManager*>(self)->mix(reinterpret_cast<int16_t*>(stream), bytes / static_cast<int>(sizeof(int16_t)));
}

// Roda na thread de audio: nao aloca memoria nem espera por nada alem da trava do SDL.
void SoundManager::mix(int16_t* output, int count) {
    const int samples = std::min(count, static_cast<int>(mixBuffer_.size()));
    std::fill(mixBuffer_.begin(), mixBuffer_.begin() + samples, 0);
    for (Voice& voice : voices_) {
        if (!voice.samples) continue;
        const size_t left = voice.samples->size() - voice.position;
        const int amount = static_cast<int>(std::min<size_t>(left, static_cast<size_t>(samples)));
        for (int i = 0; i < amount; i++) mixBuffer_[i] += (*voice.samples)[voice.position + i];
        voice.position += amount;
        if (voice.position >= voice.samples->size()) voice = Voice{};
    }
    for (int i = 0; i < samples; i++) output[i] = static_cast<int16_t>(std::clamp(mixBuffer_[i], -32768, 32767));
    std::fill(output + samples, output + count, static_cast<int16_t>(0));
}

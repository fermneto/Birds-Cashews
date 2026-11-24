#include "ui/SoundManager.h"
#include "ui/WhaleSounds.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

namespace {
constexpr int kSampleRate = 44100;
constexpr int kBufferSamples = 1024;
constexpr float kTwoPi = 6.2831853f;
constexpr float kMasterVolume = 0.5f;
constexpr float kAmbientFollow = 1.0f / 22050.0f;   // Meio segundo para o ambiente chegar ao volume novo.

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
        // Cajus caindo: tres quedas curtas, cada vez mais graves.
        case UiSound::Spill: return {{0, 0.1f, 880, 440, 0.6f, Wave::Triangle}, {0.1f, 0.1f, 740, 370, 0.6f, Wave::Triangle},
                                     {0.2f, 0.14f, 620, 260, 0.6f, Wave::Triangle}};
        // Roubo: duas notas rapidas e travessas.
        case UiSound::Steal: return {{0, 0.07f, 988, 988, 0.5f, Wave::Square}, {0.09f, 0.1f, 784, 1175, 0.5f, Wave::Square}};
        // Alarme: sirene que sobe e desce duas vezes.
        case UiSound::Alarm: return {{0, 0.18f, 700, 1100, 0.55f, Wave::Square}, {0.18f, 0.18f, 1100, 700, 0.55f, Wave::Square},
                                     {0.36f, 0.18f, 700, 1100, 0.55f, Wave::Square}};
        // Mergulho certeiro: pancada grave seguida de um brilho.
        case UiSound::Knock: return {{0, 0.12f, 150, 90, 0.75f, Wave::Square}, {0.1f, 0.16f, 1200, 1600, 0.35f, Wave::Sine}};
        // Piado curto que sobe, de passaro pequeno.
        case UiSound::ChirpTweet: return {{0, 0.06f, 3200, 4200, 0.35f, Wave::Sine}, {0.09f, 0.05f, 3600, 4400, 0.3f, Wave::Sine}};
        // Trinado: notas rapidas alternadas.
        case UiSound::ChirpTrill: {
            std::vector<Tone> trill;
            for (int i = 0; i < 7; i++) trill.push_back({i * 0.045f, 0.035f, i % 2 ? 2900.0f : 3400.0f, i % 2 ? 3000.0f : 3500.0f, 0.3f, Wave::Sine});
            return trill;
        }
        // Assobio de duas notas, de passaro medio: "fiu-fiu".
        case UiSound::ChirpWhistle: return {{0, 0.16f, 1500, 2300, 0.35f, Wave::Sine}, {0.24f, 0.2f, 1700, 1250, 0.35f, Wave::Sine}};
        // Grasnado grave e aspero, de passaro grande.
        case UiSound::ChirpCaw: return {{0, 0.22f, 520, 430, 0.3f, Wave::Square}, {0, 0.22f, 780, 640, 0.15f, Wave::Triangle},
                                        {0.3f, 0.18f, 500, 420, 0.25f, Wave::Square}};
        case UiSound::Flap:
        case UiSound::WhaleSpout:
        case UiSound::WhaleSplash:
        case UiSound::WhaleBite:
        case UiSound::WhaleChew:
        case UiSound::WhaleSpit:
        case UiSound::WhaleRumble:
        case UiSound::Count: break;
    }
    return {};
}

// Batida de asa: sopro de ruido grave que cresce rapido e se desfaz, sem nota definida.
std::vector<float> flapWhoosh() {
    constexpr float kSeconds = 0.32f;
    std::mt19937 random(0xF1A9u);
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    std::vector<float> mix(static_cast<size_t>(kSeconds * kSampleRate));
    float low = 0, lower = 0;
    for (size_t i = 0; i < mix.size(); i++) {
        const float t = static_cast<float>(i) / kSampleRate;
        low += (white(random) - low) * (0.10f + 0.12f * (1.0f - t / kSeconds));   // Fica mais grave no fim.
        lower += (low - lower) * 0.3f;
        const float swell = std::min(t / 0.07f, 1.0f) * std::exp(-7.0f * std::max(0.0f, t - 0.07f));
        mix[i] = lower * swell * 5.0f;
    }
    return mix;
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
        case GameplayFeedback::CargoLost: return UiSound::Spill;
        case GameplayFeedback::CashewStolen: return UiSound::Steal;
        case GameplayFeedback::NestAlarm: return UiSound::Alarm;
        case GameplayFeedback::RivalKnocked: return UiSound::Knock;
    }
    return UiSound::Move;
}

int SoundManager::sampleRate() { return kSampleRate; }

std::vector<int16_t> SoundManager::synthesize(UiSound sound) {
    std::vector<float> mix = sound == UiSound::Flap ? flapWhoosh() : std::vector<float>{};
    if (sound >= UiSound::WhaleSpout && sound <= UiSound::WhaleRumble)
        mix = synthesizeWhaleSound(static_cast<WhaleSound>(static_cast<int>(sound) - static_cast<int>(UiSound::WhaleSpout)), kSampleRate);
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
    for (int i = 0; i < kAmbientLayers; i++) ambientLoops_[i] = synthesizeAmbientLoop(static_cast<AmbientLayer>(i), kSampleRate);
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

void SoundManager::play(UiSound sound, float volume) {
    if (device_ == 0 || volume <= 0.0f) return;
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
    chosen->gain = std::min(volume, 1.0f);
    SDL_UnlockAudioDevice(device_);
}

void SoundManager::setAmbience(const AmbientMix& mix) {
    for (int i = 0; i < kAmbientLayers; i++)
        ambientTarget_[i].store(std::clamp(mix[static_cast<AmbientLayer>(i)], 0.0f, 1.0f), std::memory_order_relaxed);
}

AmbientMix SoundManager::ambience() const {
    AmbientMix mix;
    for (int i = 0; i < kAmbientLayers; i++) mix[static_cast<AmbientLayer>(i)] = ambientTarget_[i].load(std::memory_order_relaxed);
    return mix;
}

// Cada camada toca em loop; o volume anda um pouco a cada amostra em direcao ao pedido.
void SoundManager::mixAmbience(int count) {
    for (int layer = 0; layer < kAmbientLayers; layer++) {
        const std::vector<int16_t>& loop = ambientLoops_[layer];
        if (loop.empty()) continue;
        const float target = ambientTarget_[layer].load(std::memory_order_relaxed);
        float gain = ambientGain_[layer];
        size_t position = ambientPosition_[layer];
        for (int i = 0; i < count; i++) {
            gain += (target - gain) * kAmbientFollow * 2.0f;
            mixBuffer_[i] += static_cast<int>(loop[position] * gain);
            if (++position >= loop.size()) position = 0;
        }
        ambientGain_[layer] = gain;
        ambientPosition_[layer] = position;
    }
}

void SoundManager::audioCallback(void* self, Uint8* stream, int bytes) {
    static_cast<SoundManager*>(self)->mix(reinterpret_cast<int16_t*>(stream), bytes / static_cast<int>(sizeof(int16_t)));
}

// Roda na thread de audio: nao aloca memoria nem espera por nada alem da trava do SDL.
void SoundManager::mix(int16_t* output, int count) {
    const int samples = std::min(count, static_cast<int>(mixBuffer_.size()));
    std::fill(mixBuffer_.begin(), mixBuffer_.begin() + samples, 0);
    mixAmbience(samples);
    for (Voice& voice : voices_) {
        if (!voice.samples) continue;
        const size_t left = voice.samples->size() - voice.position;
        const int amount = static_cast<int>(std::min<size_t>(left, static_cast<size_t>(samples)));
        for (int i = 0; i < amount; i++) mixBuffer_[i] += static_cast<int>((*voice.samples)[voice.position + i] * voice.gain);
        voice.position += amount;
        if (voice.position >= voice.samples->size()) voice = Voice{};
    }
    for (int i = 0; i < samples; i++) output[i] = static_cast<int16_t>(std::clamp(mixBuffer_[i], -32768, 32767));
    std::fill(output + samples, output + count, static_cast<int16_t>(0));
}

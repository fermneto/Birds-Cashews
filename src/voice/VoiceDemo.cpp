#include "voice/VoiceDemo.h"
#include "voice/LoopbackVoiceLink.h"
#include "voice/VoiceChat.h"
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>

namespace {
constexpr float kOrbitRadius = 25.0f, kOrbitCenter = 30.0f, kOrbitSpeed = 40.0f;
constexpr PlayerId kVirtualId = 1;
std::atomic<bool> stopRequested{false};

void onInterrupt(int) { stopRequested = true; }

template <typename T>
void writeLe(std::ofstream& out, T value) {
    for (size_t i = 0; i < sizeof(T); i++) out.put(static_cast<char>((static_cast<uint64_t>(value) >> (8 * i)) & 0xff));
}

void printState(float t, const VoiceListener& me, const PlayerPublicState& other, const VoiceChat& chat, const SpatialSettings& spatial) {
    const SpatialParams params = spatialize(me, other, spatial);
    const RemoteVoice* voice = chat.mixer().voice(other.id);
    std::cout << std::fixed << std::setprecision(2) << std::setw(6) << t << " s  distancia " << std::setw(5) << params.distance
              << " m  volume " << params.gain << "  pan " << std::setw(5) << params.pan << "  doppler alvo " << params.doppler
              << "  doppler atual " << (voice ? voice->resampler().currentFactor() : 1.0f)
              << (chat.localSpeaking() ? "  [falando]" : "") << "\n";
}
}

PlayerPublicState virtualSpeaker(float seconds, float parkSeconds, PlayerId id) {
    PlayerPublicState state;
    state.id = id;
    if (seconds < parkSeconds) {
        state.position = {0, 0, -10};
        return state;
    }
    const float omega = kOrbitSpeed / kOrbitRadius;
    const float angle = omega * (seconds - parkSeconds);
    state.position = {kOrbitRadius * std::sin(angle), 0, -kOrbitCenter + kOrbitRadius * std::cos(angle)};
    state.velocity = {kOrbitSpeed * std::cos(angle), 0, -kOrbitSpeed * std::sin(angle)};
    state.yaw = std::atan2(state.velocity.x, -state.velocity.z);
    return state;
}

bool writeStereoWav(const std::string& path, const std::vector<float>& stereo, int sampleRate) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const uint32_t dataBytes = static_cast<uint32_t>(stereo.size() * sizeof(int16_t));
    out.write("RIFF", 4);
    writeLe<uint32_t>(out, 36 + dataBytes);
    out.write("WAVEfmt ", 8);
    writeLe<uint32_t>(out, 16);
    writeLe<uint16_t>(out, 1);   // PCM.
    writeLe<uint16_t>(out, 2);
    writeLe<uint32_t>(out, static_cast<uint32_t>(sampleRate));
    writeLe<uint32_t>(out, static_cast<uint32_t>(sampleRate) * 4);
    writeLe<uint16_t>(out, 4);
    writeLe<uint16_t>(out, 16);
    out.write("data", 4);
    writeLe<uint32_t>(out, dataBytes);
    for (float sample : stereo) {
        const auto value = static_cast<int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        writeLe<uint16_t>(out, static_cast<uint16_t>(value));
    }
    return static_cast<bool>(out);
}

int runVoiceDemo() {
    VoiceChat chat;
    chat.start();
    if (!chat.hasMicrophone() || !chat.hasOutput()) {
        std::cerr << "A demonstracao precisa de microfone e de saida de audio.\n";
        return 1;
    }
    LoopbackVoiceLink link = LoopbackVoiceLink::echo(kVirtualId);
    chat.attach(&link, 0);
    // Depois do SDL, que trata SIGINT e SIGTERM como evento de saida que aqui ninguem le.
    std::signal(SIGINT, onInterrupt);
    std::signal(SIGTERM, onInterrupt);
    std::cout << "Fale no microfone (use fones para nao dar eco). Sua voz de passaro volta de um passaro virtual\n"
              << "que fica 2 s parado na frente e depois circula ao seu redor. Ctrl+C para sair.\n";
    const VoiceListener me;
    const Uint64 start = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    float last = 0, lastPrint = -1;
    while (!stopRequested) {
        const float t = static_cast<float>(static_cast<double>(SDL_GetPerformanceCounter() - start) / frequency);
        const PlayerPublicState other = virtualSpeaker(t, 2.0f, kVirtualId);
        chat.update(t - last, me, {other});
        last = t;
        if (t - lastPrint >= 0.5f) {
            printState(t, me, other, chat, chat.settings().spatial);
            lastPrint = t;
        }
        SDL_Delay(10);
    }
    std::cout << "\nQuadros enviados: " << chat.stats().framesSent << ", recebidos: " << chat.stats().framesReceived << "\n";
    return 0;
}

// Sinal sintetico parecido com voz: tom harmonico de 200 Hz em frases de 2,5 s com pausas de 0,5 s,
// sobre um ruido de fundo baixo. O falante fica parado na primeira frase (mede so a voz de passaro)
// e circula nas outras (mede o Doppler).
int runVoiceFileDemo(const std::string& outputPath) {
    constexpr float kSeconds = 12.0f, kPark = 3.25f, kFundamental = 200.0f;
    VoiceSettings settings;
    settings.useDevices = false;
    VoiceChat chat(settings);
    chat.start();
    LoopbackVoiceLink link = LoopbackVoiceLink::echo(kVirtualId);
    chat.attach(&link, 0);

    std::mt19937 random(7);
    std::normal_distribution<float> noise(0.0f, 0.001f);
    const VoiceListener me;
    const int blocks = static_cast<int>(kSeconds * kVoiceRate / kVoiceFrame);
    std::vector<float> mic(kVoiceFrame), block(2 * kVoiceFrame), output;
    output.reserve(static_cast<size_t>(blocks) * block.size());
    const float shifted = kFundamental * std::pow(2.0f, settings.pitchSemitones / 12.0f);
    std::cout << "# tempo_s distancia_m volume pan doppler_alvo doppler_atual frequencia_esperada_hz\n";
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < kVoiceFrame; i++) {
            const double t = static_cast<double>(b * kVoiceFrame + i) / kVoiceRate;
            const double phrase = std::fmod(t - 0.5, 3.0);
            float sample = noise(random);
            if (t >= 0.5 && phrase < 2.5) {
                const double fade = std::min({phrase / 0.02, (2.5 - phrase) / 0.02, 1.0});
                for (int h = 1; h <= 5; h++) sample += static_cast<float>(fade * 0.12 / h * std::sin(2.0 * M_PI * kFundamental * h * t));
            }
            mic[static_cast<size_t>(i)] = sample;
        }
        const float t = static_cast<float>(b) * kVoiceFrame / kVoiceRate;
        const PlayerPublicState other = virtualSpeaker(t, kPark, kVirtualId);
        chat.feedMicrophone(mic.data(), mic.size());
        chat.update(static_cast<float>(kVoiceFrame) / kVoiceRate, me, {other});
        chat.renderOutput(block.data());
        output.insert(output.end(), block.begin(), block.end());
        if (b % 5 == 0) {
            const SpatialParams params = spatialize(me, other, settings.spatial);
            const RemoteVoice* voice = chat.mixer().voice(kVirtualId);
            const float current = voice ? voice->resampler().currentFactor() : 1.0f;
            std::cout << std::fixed << std::setprecision(3) << t << " " << params.distance << " " << params.gain << " " << params.pan
                      << " " << params.doppler << " " << current << " " << shifted * current << "\n";
        }
    }
    if (!writeStereoWav(outputPath, output, kVoiceRate)) {
        std::cerr << "Nao foi possivel gravar " << outputPath << "\n";
        return 1;
    }
    std::cout << "# Gravado " << outputPath << ": " << kSeconds << " s, 48 kHz estereo. Tom de entrada " << kFundamental
              << " Hz, com voz de passaro " << shifted << " Hz. Quadros enviados: " << chat.stats().framesSent << "\n";
    return 0;
}

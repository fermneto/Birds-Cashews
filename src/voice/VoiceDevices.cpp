#include "voice/VoiceDevices.h"
#include "voice/VoiceDsp.h"
#include <iostream>

namespace {
SDL_AudioSpec voiceSpec(int channels, int samples) {
    SDL_AudioSpec spec{};
    spec.freq = kVoiceRate;
    spec.format = AUDIO_F32SYS;
    spec.channels = static_cast<Uint8>(channels);
    spec.samples = static_cast<Uint16>(samples);
    spec.callback = nullptr;   // Modo de fila: SDL_QueueAudio e SDL_DequeueAudio.
    return spec;
}
}

VoiceDevices::~VoiceDevices() { close(); }

bool VoiceDevices::open(bool capture, bool output) {
    if (!subsystemOpen_) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::cerr << "Audio indisponivel, voz desligada: " << SDL_GetError() << "\n";
            return false;
        }
        subsystemOpen_ = true;
    }
    if (capture && capture_ == 0) {
        // Sem mudancas permitidas: o SDL converte qualquer microfone para 48 kHz mono float.
        const SDL_AudioSpec wanted = voiceSpec(1, 480);
        SDL_AudioSpec obtained{};
        capture_ = SDL_OpenAudioDevice(nullptr, 1, &wanted, &obtained, 0);
        if (capture_ == 0) std::cerr << "Microfone indisponivel, sem voz propria: " << SDL_GetError() << "\n";
        else SDL_PauseAudioDevice(capture_, 0);
    }
    if (output && output_ == 0) {
        const SDL_AudioSpec wanted = voiceSpec(2, 512);
        SDL_AudioSpec obtained{};
        output_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
        if (output_ == 0) std::cerr << "Saida de audio indisponivel, sem voz dos outros: " << SDL_GetError() << "\n";
        else SDL_PauseAudioDevice(output_, 0);
    }
    return capture_ != 0 || output_ != 0;
}

void VoiceDevices::close() {
    if (capture_ != 0) SDL_CloseAudioDevice(capture_);
    if (output_ != 0) SDL_CloseAudioDevice(output_);
    capture_ = output_ = 0;
    if (subsystemOpen_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    subsystemOpen_ = false;
}

void VoiceDevices::readCapture(std::vector<float>& samples) {
    if (capture_ == 0) return;
    const Uint32 bytes = SDL_GetQueuedAudioSize(capture_);
    if (bytes == 0) return;
    scratch_.resize(bytes / sizeof(float));
    const Uint32 got = SDL_DequeueAudio(capture_, scratch_.data(), static_cast<Uint32>(scratch_.size() * sizeof(float)));
    samples.insert(samples.end(), scratch_.begin(), scratch_.begin() + got / sizeof(float));
}

int VoiceDevices::queuedOutputMs() const {
    if (output_ == 0) return 0;
    const Uint32 frames = SDL_GetQueuedAudioSize(output_) / (2 * sizeof(float));
    return static_cast<int>(frames * 1000 / kVoiceRate);
}

void VoiceDevices::queueOutput(const float* stereo, int frames) {
    if (output_ == 0) return;
    SDL_QueueAudio(output_, stereo, static_cast<Uint32>(frames * 2 * sizeof(float)));
}

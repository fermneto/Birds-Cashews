#pragma once
#include <SDL.h>
#include <vector>

// Microfone e saida da voz pelo SDL, em modo de fila (sem callback): tudo roda na thread principal,
// sem travas. A saida e um dispositivo proprio, aberto ao lado do do SoundManager; o SDL e o servidor
// de audio do sistema misturam os dois. Sem dispositivo, cada parte fica desligada e em silencio.
class VoiceDevices {
public:
    VoiceDevices() = default;
    ~VoiceDevices();
    VoiceDevices(const VoiceDevices&) = delete;
    VoiceDevices& operator=(const VoiceDevices&) = delete;

    bool open(bool capture, bool output);
    void close();
    bool hasCapture() const { return capture_ != 0; }
    bool hasOutput() const { return output_ != 0; }

    // Tudo o que o microfone gravou desde a ultima chamada (48 kHz mono).
    void readCapture(std::vector<float>& samples);
    // Milissegundos de audio ainda na fila da saida.
    int queuedOutputMs() const;
    void queueOutput(const float* stereo, int frames);

private:
    SDL_AudioDeviceID capture_ = 0, output_ = 0;
    bool subsystemOpen_ = false;
    std::vector<float> scratch_;
};

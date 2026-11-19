#pragma once
#include "shared/VoiceLink.h"
#include "voice/JitterBuffer.h"
#include "voice/OpusCodec.h"
#include "voice/Spatializer.h"
#include "voice/VoiceDsp.h"
#include <array>
#include <map>
#include <memory>

// A voz de um falante remoto: buffer de jitter -> Opus -> Doppler -> volume e pan.
// Cada tique de 20 ms consome um quadro do buffer de jitter e soma um bloco estereo na mixagem.
class RemoteVoice {
public:
    explicit RemoteVoice(int jitterFrames = 3);

    void receive(const VoiceFrame& frame);
    void setSpatial(const SpatialParams& params) { target_ = params; }
    void mixInto(float* stereo);   // Soma kVoiceFrame quadros estereo intercalados.

    bool speaking() const { return jitter_.playing(); }
    int idleTicks() const { return idle_; }
    const JitterBuffer& jitter() const { return jitter_; }
    const DopplerResampler& resampler() const { return resampler_; }
    const SpatialParams& spatial() const { return target_; }

private:
    JitterBuffer jitter_;
    VoiceDecoder decoder_;
    DopplerResampler resampler_;
    SpatialParams target_;
    float left_ = 0, right_ = 0;
    int idle_ = 100, talkspurts_ = 0;   // Comeca em repouso (mais que a cauda, menos que o esquecimento).
    std::array<float, kVoiceFrame> pcm_{}, shifted_{};
};

// Soma as vozes de todos os falantes, com limitador. Feita para salas de ate 6 jogadores
// (5 falantes ao mesmo tempo), com folga ate kMaxVoices.
class VoiceMixer {
public:
    static constexpr size_t kMaxVoices = 8;

    explicit VoiceMixer(int jitterFrames = 3);

    void receive(const VoiceFrame& frame);
    void setSpatial(PlayerId speaker, const SpatialParams& params);
    void render(float* stereo);   // Um bloco de kVoiceFrame quadros estereo intercalados.
    void setVolume(float volume) { volume_ = volume; }
    void forget(PlayerId speaker) { voices_.erase(speaker); }

    bool speaking(PlayerId speaker) const;
    size_t voices() const { return voices_.size(); }
    const RemoteVoice* voice(PlayerId speaker) const;
    float limiterGain() const { return limiter_.gain(); }

private:
    int jitterFrames_;
    std::map<PlayerId, std::unique_ptr<RemoteVoice>> voices_;
    std::map<PlayerId, SpatialParams> spatial_;
    Limiter limiter_;
    float volume_ = 1.0f;
};

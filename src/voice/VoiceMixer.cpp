#include "voice/VoiceMixer.h"
#include <algorithm>

namespace {
constexpr int kMaxConcealTicks = 6;    // Ate 120 ms de atraso coberto antes de encerrar a fala.
constexpr int kTailTicks = 10;          // Depois de parar, ainda toca 200 ms para esvaziar o Doppler.
constexpr int kForgetTicks = 30 * 50;   // Falante calado por 30 s sai da mixagem.
constexpr float kGainSmoothing = 0.002f;   // Cerca de 10 ms: volume e pan mudam sem degrau.
}

RemoteVoice::RemoteVoice(int jitterFrames) : jitter_(jitterFrames, kMaxConcealTicks, true) {}

void RemoteVoice::receive(const VoiceFrame& frame) { jitter_.push(frame.sequence, frame.payload); }

void RemoteVoice::mixInto(float* stereo) {
    JitterBuffer::Item item = jitter_.pop();
    if (jitter_.stats().talkspurts != talkspurts_) {
        // Fala nova: o que restou no Doppler ja era silencio, entao recomeca limpo e com pouco atraso.
        talkspurts_ = jitter_.stats().talkspurts;
        if (idle_ > kTailTicks) {
            decoder_.reset();
            resampler_.reset();
        }
    }
    switch (item.kind) {
        case JitterBuffer::Kind::Packet: decoder_.decode(item.payload, pcm_.data()); idle_ = 0; break;
        case JitterBuffer::Kind::Lost: decoder_.conceal(item.next.empty() ? nullptr : &item.next, pcm_.data()); idle_ = 0; break;
        case JitterBuffer::Kind::Idle: pcm_.fill(0.0f); idle_++; break;
    }
    if (idle_ > kTailTicks) return;
    resampler_.push(pcm_.data(), kVoiceFrame);
    resampler_.render(shifted_.data(), kVoiceFrame, target_.doppler);
    const float targetLeft = target_.gain * target_.left, targetRight = target_.gain * target_.right;
    for (int i = 0; i < kVoiceFrame; i++) {
        left_ += (targetLeft - left_) * kGainSmoothing;
        right_ += (targetRight - right_) * kGainSmoothing;
        stereo[2 * i] += shifted_[static_cast<size_t>(i)] * left_;
        stereo[2 * i + 1] += shifted_[static_cast<size_t>(i)] * right_;
    }
}

VoiceMixer::VoiceMixer(int jitterFrames) : jitterFrames_(jitterFrames) {}

void VoiceMixer::receive(const VoiceFrame& frame) {
    if (frame.payload.empty() || frame.payload.size() > kMaxVoicePayload || frame.speaker == kNoPlayer) return;
    auto found = voices_.find(frame.speaker);
    if (found == voices_.end()) {
        if (voices_.size() >= kMaxVoices) return;
        found = voices_.emplace(frame.speaker, std::make_unique<RemoteVoice>(jitterFrames_)).first;
        auto spatial = spatial_.find(frame.speaker);
        if (spatial != spatial_.end()) found->second->setSpatial(spatial->second);
    }
    found->second->receive(frame);
}

void VoiceMixer::setSpatial(PlayerId speaker, const SpatialParams& params) {
    spatial_[speaker] = params;
    auto found = voices_.find(speaker);
    if (found != voices_.end()) found->second->setSpatial(params);
}

void VoiceMixer::render(float* stereo) {
    std::fill(stereo, stereo + 2 * kVoiceFrame, 0.0f);
    for (auto it = voices_.begin(); it != voices_.end();) {
        it->second->mixInto(stereo);
        if (it->second->idleTicks() > kForgetTicks) it = voices_.erase(it);
        else ++it;
    }
    for (int i = 0; i < 2 * kVoiceFrame; i++) stereo[i] *= volume_;
    limiter_.process(stereo, kVoiceFrame);
}

bool VoiceMixer::speaking(PlayerId speaker) const {
    const RemoteVoice* found = voice(speaker);
    return found && found->speaking();
}

const RemoteVoice* VoiceMixer::voice(PlayerId speaker) const {
    auto found = voices_.find(speaker);
    return found == voices_.end() ? nullptr : found->second.get();
}

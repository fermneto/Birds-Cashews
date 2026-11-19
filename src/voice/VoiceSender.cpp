#include "voice/VoiceSender.h"

namespace {
constexpr size_t kPreRollFrames = 3;
}

VoiceSender::VoiceSender(const SpeechSettings& speech, bool birdVoice, float semitones, int bitrate)
    : detector_(speech), shifter_(semitones), encoder_(bitrate), birdVoice_(birdVoice), shifted_(kVoiceFrame), boosted_(kVoiceFrame), cleaned_(kVoiceFrame) {}

void VoiceSender::setBirdVoice(bool enabled, float semitones) {
    if (enabled && !birdVoice_) shifter_.reset();
    birdVoice_ = enabled;
    shifter_.setSemitones(semitones);
}

std::vector<VoiceFrame> VoiceSender::process(const float* frame, PlayerId speaker, bool muted) {
    std::copy(frame, frame + kVoiceFrame, cleaned_.begin());
    bool voiceLike = true;
    if (denoise_) {
        highPass_.process(cleaned_.data(), kVoiceFrame);
        suppressor_.process(cleaned_.data(), cleaned_.data(), kVoiceFrame);
        voiceLike = suppressor_.voiceLikely();
    }
    const bool voiced = detector_.process(cleaned_.data(), kVoiceFrame, voiceLike);
    if (denoise_) suppressor_.setSpeaking(voiced);
    std::copy(cleaned_.begin(), cleaned_.end(), boosted_.begin());
    gain_.process(boosted_.data(), kVoiceFrame, voiced);
    const float* source = boosted_.data();
    if (birdVoice_) {
        shifter_.process(boosted_.data(), shifted_.data(), kVoiceFrame);
        source = shifted_.data();
    }
    std::vector<uint8_t> packet = encoder_.encode(source);
    const bool wasSpeaking = speaking_;
    speaking_ = voiced && !muted && !packet.empty();

    std::vector<VoiceFrame> frames;
    if (speaking_) {
        if (!wasSpeaking)
            for (const auto& earlier : previous_) frames.push_back({speaker, sequence_++, earlier});
        frames.push_back({speaker, sequence_++, packet});
    }
    previous_.push_back(std::move(packet));
    while (previous_.size() > kPreRollFrames) previous_.pop_front();
    return frames;
}

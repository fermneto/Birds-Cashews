#include "voice/VoiceSender.h"

VoiceSender::VoiceSender(const SpeechSettings& speech, bool birdVoice, float semitones, int bitrate)
    : detector_(speech), shifter_(semitones), encoder_(bitrate), birdVoice_(birdVoice), shifted_(kVoiceFrame) {}

void VoiceSender::setBirdVoice(bool enabled, float semitones) {
    if (enabled && !birdVoice_) shifter_.reset();
    birdVoice_ = enabled;
    shifter_.setSemitones(semitones);
}

std::vector<VoiceFrame> VoiceSender::process(const float* frame, PlayerId speaker, bool muted) {
    const bool voiced = detector_.process(frame, kVoiceFrame);
    const float* source = frame;
    if (birdVoice_) {
        shifter_.process(frame, shifted_.data(), kVoiceFrame);
        source = shifted_.data();
    }
    std::vector<uint8_t> packet = encoder_.encode(source);
    const bool wasSpeaking = speaking_;
    speaking_ = voiced && !muted && !packet.empty();

    std::vector<VoiceFrame> frames;
    if (speaking_) {
        if (!wasSpeaking && !previous_.empty()) frames.push_back({speaker, sequence_++, previous_});
        frames.push_back({speaker, sequence_++, packet});
    }
    previous_ = std::move(packet);
    return frames;
}

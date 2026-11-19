#include "voice/VoiceChat.h"
#include <algorithm>

namespace {
constexpr size_t kMaxCaptureBacklog = 10 * kVoiceFrame;   // Mais de 200 ms parados: descarta o mais antigo.
}

VoiceChat::VoiceChat(VoiceSettings settings)
    : settings_(settings),
      sender_(settings.speech, settings.birdVoice, settings.pitchSemitones, settings.bitrate),
      mixer_(settings.jitterFrames),
      block_(2 * kVoiceFrame, 0.0f) {
    mixer_.setVolume(settings.volume);
}

bool VoiceChat::start() {
    if (!settings_.useDevices) return true;
    return devices_.open(true, true);
}

void VoiceChat::attach(VoiceLink* link, PlayerId local) {
    link_ = link;
    local_ = local;
}

void VoiceChat::setBirdVoice(bool enabled, float semitones) {
    settings_.birdVoice = enabled;
    settings_.pitchSemitones = semitones;
    sender_.setBirdVoice(enabled, semitones);
}

void VoiceChat::setVolume(float volume) {
    settings_.volume = std::max(volume, 0.0f);
    mixer_.setVolume(settings_.volume);
}

void VoiceChat::feedMicrophone(const float* samples, size_t count) { captured_.insert(captured_.end(), samples, samples + count); }

void VoiceChat::processCapture() {
    if (settings_.useDevices) devices_.readCapture(captured_);
    if (captured_.size() > kMaxCaptureBacklog) {
        const size_t drop = (captured_.size() - kMaxCaptureBacklog) / kVoiceFrame * kVoiceFrame + kVoiceFrame;
        const size_t amount = std::min(drop, captured_.size());
        captured_.erase(captured_.begin(), captured_.begin() + static_cast<std::ptrdiff_t>(amount));
        stats_.capturedFramesDropped += static_cast<int>(amount / kVoiceFrame);
    }
    size_t used = 0;
    while (captured_.size() - used >= kVoiceFrame) {
        const std::vector<VoiceFrame> frames = sender_.process(captured_.data() + used, local_, muted_ || !link_);
        used += kVoiceFrame;
        for (const VoiceFrame& frame : frames) {
            link_->sendVoice(frame);
            stats_.framesSent++;
        }
    }
    captured_.erase(captured_.begin(), captured_.begin() + static_cast<std::ptrdiff_t>(used));
}

void VoiceChat::update(float /*dt*/, const VoiceListener& me, const std::vector<PlayerPublicState>& others) {
    processCapture();
    if (link_) {
        for (const VoiceFrame& frame : link_->receiveVoice()) {
            if (frame.speaker == local_) continue;   // Nunca toca a propria voz.
            mixer_.receive(frame);
            stats_.framesReceived++;
        }
    }
    // Falante sem estado conhecido toca no centro, com volume cheio e sem Doppler (valor padrao).
    for (const PlayerPublicState& other : others) {
        if (other.id != local_) mixer_.setSpatial(other.id, spatialize(me, other, settings_.spatial));
    }
    if (settings_.useDevices && devices_.hasOutput()) {
        // Mantem a fila da saida com cerca de outputQueueMs: o relogio da placa de som dita o ritmo da mixagem.
        while (devices_.queuedOutputMs() < settings_.outputQueueMs) {
            mixer_.render(block_.data());
            devices_.queueOutput(block_.data(), kVoiceFrame);
        }
    }
}

void VoiceChat::renderOutput(float* stereo) { mixer_.render(stereo); }

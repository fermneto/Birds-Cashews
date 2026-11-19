#include "voice/VoiceChat.h"
#include <algorithm>
#include <chrono>

namespace {
constexpr size_t kMaxCaptureBacklog = 10 * kVoiceFrame;   // Mais de 200 ms parados: descarta o mais antigo.
constexpr size_t kMaxOutbox = 50;                        // 1 s de voz esperando a thread principal.
}

VoiceChat::VoiceChat(VoiceSettings settings)
    : settings_(settings),
      sender_(settings.speech, settings.birdVoice, settings.pitchSemitones, settings.bitrate),
      mixer_(settings.jitterFrames),
      block_(2 * kVoiceFrame, 0.0f) {
    mixer_.setVolume(settings.volume);
    sender_.setNoiseSuppression(settings.noiseSuppression);
}

VoiceChat::~VoiceChat() { stopAudioThread(); }

bool VoiceChat::start() {
    if (!settings_.useDevices) return true;
    const bool opened = devices_.open(true, true);
    if (opened && settings_.audioThread && !audio_.joinable()) {
        running_ = true;
        audio_ = std::thread(&VoiceChat::audioLoop, this);
    }
    return opened;
}

void VoiceChat::stopAudioThread() {
    running_ = false;
    if (audio_.joinable()) audio_.join();
}

// A cada 5 ms: le o microfone, codifica, mistura e completa a fila da saida.
void VoiceChat::audioLoop() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pumpAudio();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void VoiceChat::pumpAudio() {
    processCapture();
    fillOutput();
}

void VoiceChat::attach(VoiceLink* link, PlayerId local) {
    std::lock_guard<std::mutex> lock(mutex_);
    link_ = link;
    local_ = local;
    outbox_.clear();
}

void VoiceChat::setBirdVoice(bool enabled, float semitones) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.birdVoice = enabled;
    settings_.pitchSemitones = semitones;
    sender_.setBirdVoice(enabled, semitones);
}

void VoiceChat::setVolume(float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.volume = std::max(volume, 0.0f);
    mixer_.setVolume(settings_.volume);
}

VoiceStats VoiceChat::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

bool VoiceChat::speaking(PlayerId player) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mixer_.speaking(player);
}

void VoiceChat::feedMicrophone(const float* samples, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    captured_.insert(captured_.end(), samples, samples + count);
}

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
            if (outbox_.size() < kMaxOutbox) outbox_.push_back(frame);
            stats_.framesSent++;
        }
    }
    captured_.erase(captured_.begin(), captured_.begin() + static_cast<std::ptrdiff_t>(used));
    speaking_ = sender_.speaking();
    level_ = sender_.level();
    gainDb_ = sender_.gainDb();
}

// Mantem a fila da saida com cerca de outputQueueMs: o relogio da placa de som dita o ritmo da mixagem.
void VoiceChat::fillOutput() {
    if (!settings_.useDevices || !devices_.hasOutput()) return;
    while (devices_.queuedOutputMs() < settings_.outputQueueMs) {
        mixer_.render(block_.data());
        devices_.queueOutput(block_.data(), kVoiceFrame);
    }
}

void VoiceChat::update(float /*dt*/, const VoiceListener& me, const std::vector<PlayerPublicState>& others) {
    std::vector<VoiceFrame> outgoing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!threaded()) processCapture();
        outgoing.swap(outbox_);
    }
    // A rede pertence a thread principal: os quadros saem e entram aqui.
    if (link_) {
        for (const VoiceFrame& frame : outgoing) link_->sendVoice(frame);
    }
    std::vector<VoiceFrame> incoming;
    if (link_) incoming = link_->receiveVoice();

    std::lock_guard<std::mutex> lock(mutex_);
    for (const VoiceFrame& frame : incoming) {
        if (frame.speaker == local_) continue;   // Nunca toca a propria voz.
        mixer_.receive(frame);
        stats_.framesReceived++;
    }
    // Falante sem estado conhecido toca no centro, com volume cheio e sem Doppler (valor padrao).
    for (const PlayerPublicState& other : others) {
        if (other.id != local_) mixer_.setSpatial(other.id, spatialize(me, other, settings_.spatial));
    }
    if (!threaded()) fillOutput();
}

void VoiceChat::renderOutput(float* stereo) {
    std::lock_guard<std::mutex> lock(mutex_);
    mixer_.render(stereo);
}

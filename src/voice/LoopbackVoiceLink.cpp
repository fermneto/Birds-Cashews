#include "voice/LoopbackVoiceLink.h"
#include <algorithm>

void LoopbackVoiceHub::join(LoopbackVoiceLink* link) {
    std::lock_guard<std::mutex> lock(mutex_);
    links_.push_back(link);
}

void LoopbackVoiceHub::leave(LoopbackVoiceLink* link) {
    std::lock_guard<std::mutex> lock(mutex_);
    links_.erase(std::remove(links_.begin(), links_.end(), link), links_.end());
}

void LoopbackVoiceHub::broadcast(const LoopbackVoiceLink* from, const VoiceFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (LoopbackVoiceLink* link : links_)
        if (link != from) link->deliver(frame);
}

LoopbackVoiceLink::LoopbackVoiceLink(std::shared_ptr<LoopbackVoiceHub> hub) : hub_(std::move(hub)) {
    if (hub_) hub_->join(this);
}

LoopbackVoiceLink LoopbackVoiceLink::echo(PlayerId echoAs) {
    LoopbackVoiceLink link;
    link.echoAs_ = echoAs;
    return link;
}

LoopbackVoiceLink::LoopbackVoiceLink(LoopbackVoiceLink&& other) noexcept : hub_(std::move(other.hub_)), echoAs_(other.echoAs_) {
    std::lock_guard<std::mutex> lock(other.mutex_);
    inbox_ = std::move(other.inbox_);
    if (hub_) {
        hub_->leave(&other);
        hub_->join(this);
    }
}

LoopbackVoiceLink::~LoopbackVoiceLink() {
    if (hub_) hub_->leave(this);
}

void LoopbackVoiceLink::sendVoice(const VoiceFrame& frame) {
    if (echoAs_ != kNoPlayer) {
        VoiceFrame copy = frame;
        copy.speaker = echoAs_;
        deliver(copy);
    } else if (hub_) {
        hub_->broadcast(this, frame);
    }
}

void LoopbackVoiceLink::deliver(const VoiceFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    inbox_.push_back(frame);
}

std::vector<VoiceFrame> LoopbackVoiceLink::receiveVoice() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<VoiceFrame> frames(inbox_.begin(), inbox_.end());
    inbox_.clear();
    return frames;
}

#include "voice/JitterBuffer.h"
#include <algorithm>

namespace {
constexpr int64_t kNewTalkspurtGap = 64;   // Um salto maior que isso e uma fala nova, nao um quadro adiantado.
}

JitterBuffer::JitterBuffer(int targetFrames, int maxConceal)
    : target_(std::max(targetFrames, 1)), maxConceal_(maxConceal), maxFrames_(target_ + 4) {}

void JitterBuffer::reset() {
    packets_.clear();
    playing_ = anchored_ = hasPlayed_ = false;
    waited_ = misses_ = 0;
}

// A sequencia tem 16 bits e da a volta: a diferenca com sinal ate a referencia resolve a volta.
int64_t JitterBuffer::unwrap(uint16_t sequence) const {
    return refAbs_ + static_cast<int16_t>(static_cast<uint16_t>(sequence - refSeq_));
}

void JitterBuffer::push(uint16_t sequence, std::vector<uint8_t> payload) {
    if (!anchored_) {
        refSeq_ = sequence;
        anchored_ = true;
    }
    int64_t abs = unwrap(sequence);
    const bool late = playing_ ? abs < next_ : (hasPlayed_ && abs <= lastPlayed_ && lastPlayed_ - abs < 1000);
    if (late) {
        stats_.late++;
        return;
    }
    if (playing_ && abs - next_ > kNewTalkspurtGap) {
        // A pessoa voltou a falar depois de uma perda longa: recomeca do repouso.
        packets_.clear();
        playing_ = false;
        waited_ = misses_ = 0;
    }
    // Mantem a referencia perto da sequencia atual, para a volta dos 16 bits nunca confundir.
    refSeq_ = sequence;
    refAbs_ = abs;
    if (!packets_.emplace(abs, std::move(payload)).second) {
        stats_.duplicates++;
        return;
    }
    stats_.received++;
}

JitterBuffer::Item JitterBuffer::pop() {
    Item item;
    if (!playing_) {
        if (packets_.empty()) return item;
        // Espera juntar o alvo ou o primeiro quadro esperar o alvo, o que vier antes: falas curtas tambem tocam.
        if (static_cast<int>(packets_.size()) < target_ && ++waited_ < target_) return item;
        playing_ = true;
        next_ = packets_.begin()->first;
        waited_ = misses_ = 0;
        stats_.talkspurts++;
    }
    // Atraso acumulado demais (relogios diferentes ou rajada): descarta os mais antigos.
    while (static_cast<int>(packets_.size()) > maxFrames_) {
        packets_.erase(packets_.begin());
        stats_.overflow++;
        next_ = std::max(next_, packets_.begin()->first);
    }

    auto found = packets_.find(next_);
    if (found != packets_.end()) {
        item.kind = Kind::Packet;
        item.payload = std::move(found->second);
        packets_.erase(found);
        misses_ = 0;
    } else if (!packets_.empty() || misses_ < maxConceal_) {
        // Perdido, ou a pessoa acabou de parar: a ocultacao suaviza o fim.
        item.kind = Kind::Lost;
        auto following = packets_.find(next_ + 1);
        if (following != packets_.end()) item.next = following->second;
        if (packets_.empty()) misses_++;
        else stats_.lost++;
    } else {
        playing_ = false;
        hasPlayed_ = true;
        lastPlayed_ = next_ - 1;
        misses_ = waited_ = 0;
        return item;
    }
    lastPlayed_ = next_;
    hasPlayed_ = true;
    next_++;
    return item;
}

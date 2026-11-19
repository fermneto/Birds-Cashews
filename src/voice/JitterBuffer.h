#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

// Buffer de jitter de um falante. Recebe os quadros na ordem em que chegam e entrega um por tique de 20 ms,
// na ordem de envio. Espera cerca de "target" quadros (60 ms) antes de comecar a tocar, reordena,
// descarta duplicados e atrasados, marca as perdas para a ocultacao e volta ao repouso quando a pessoa para.
class JitterBuffer {
public:
    enum class Kind { Packet, Lost, Idle };
    struct Item {
        Kind kind = Kind::Idle;
        std::vector<uint8_t> payload;   // Packet: o quadro a decodificar.
        std::vector<uint8_t> next;      // Lost: o quadro seguinte, se ja chegou, para usar o FEC.
    };
    struct Stats {
        int received = 0, duplicates = 0, late = 0, lost = 0, overflow = 0, talkspurts = 0;
    };

    explicit JitterBuffer(int targetFrames = 3, int maxConceal = 2);

    void push(uint16_t sequence, std::vector<uint8_t> payload);
    Item pop();   // Um tique de 20 ms.
    void reset();

    bool playing() const { return playing_; }
    size_t buffered() const { return packets_.size(); }
    const Stats& stats() const { return stats_; }

private:
    int64_t unwrap(uint16_t sequence) const;

    int target_, maxConceal_, maxFrames_;
    std::map<int64_t, std::vector<uint8_t>> packets_;
    int64_t refAbs_ = 1 << 20;   // Sequencia sem a volta dos 16 bits, ancorada em refSeq_.
    uint16_t refSeq_ = 0;
    int64_t next_ = 0;           // Proximo quadro a tocar.
    bool playing_ = false, anchored_ = false, hasPlayed_ = false;
    int64_t lastPlayed_ = 0;
    int waited_ = 0, misses_ = 0;
    Stats stats_;
};

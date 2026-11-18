#pragma once
#include <cstdint>

// Gerador proprio: o mesmo resultado em qualquer computador, essencial para o multiplayer.
class Rng {
public:
    explicit Rng(uint64_t seed) : state_(seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL) { next(); next(); }

    uint64_t next() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1DULL;
    }
    float unit() { return static_cast<float>(next() >> 40) / static_cast<float>(1ULL << 24); }
    float range(float low, float high) { return low + (high - low) * unit(); }
    int range(int low, int high) { return low + static_cast<int>(next() >> 33) % (high - low + 1); }

private:
    uint64_t state_;
};

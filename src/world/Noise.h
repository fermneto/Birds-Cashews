#pragma once
#include <array>
#include <cstdint>

// Ruido de valor em 2D, somado em oitavas para formar o relevo.
class ValueNoise {
public:
    explicit ValueNoise(uint32_t seed);
    float sample(float x, float y) const;
    float fractal(float x, float y, int octaves) const;

private:
    float lattice(int x, int y) const;
    std::array<float, 256> values_;
    std::array<uint8_t, 256> order_;
};

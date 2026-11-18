#include "world/Noise.h"
#include "world/Rng.h"
#include <cmath>
#include <utility>

ValueNoise::ValueNoise(uint32_t seed) {
    Rng rng(seed);
    for (int i = 0; i < 256; i++) {
        values_[i] = rng.unit();
        order_[i] = static_cast<uint8_t>(i);
    }
    for (int i = 255; i > 0; i--) std::swap(order_[i], order_[rng.range(0, i)]);
}

float ValueNoise::lattice(int x, int y) const {
    return values_[order_[(order_[x & 255] + y) & 255]];
}

float ValueNoise::sample(float x, float y) const {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = x - ix, fy = y - iy;
    const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    const float bottom = lattice(ix, iy) + (lattice(ix + 1, iy) - lattice(ix, iy)) * u;
    const float top = lattice(ix, iy + 1) + (lattice(ix + 1, iy + 1) - lattice(ix, iy + 1)) * u;
    return bottom + (top - bottom) * v;
}

float ValueNoise::fractal(float x, float y, int octaves) const {
    float total = 0, amplitude = 0.5f, weight = 0;
    for (int i = 0; i < octaves; i++) {
        total += sample(x, y) * amplitude;
        weight += amplitude;
        amplitude *= 0.5f;
        x = x * 2.03f + 17.0f;
        y = y * 2.03f + 31.0f;
    }
    return total / weight;
}

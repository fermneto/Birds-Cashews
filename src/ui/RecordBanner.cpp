#include "ui/RecordBanner.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kEnterSeconds = 0.35f;
constexpr float kMinimumSeconds = 3.5f;
constexpr float kFadeSeconds = 0.6f;
}

// O aviso fica enquanto a partida pedir, e nunca menos que o tempo minimo de leitura.
void RecordBanner::update(bool beatenNow, float seconds) {
    if (beatenNow && !wasBeaten_) {
        active_ = true;
        elapsed_ = 0;
        fading_ = 0;
    }
    wasBeaten_ = beatenNow;
    if (!active_) return;

    elapsed_ += seconds;
    if (beatenNow || elapsed_ < kMinimumSeconds) {
        fading_ = 0;
        return;
    }
    fading_ += seconds;
    if (fading_ >= kFadeSeconds) active_ = false;
}

float RecordBanner::opacity() const {
    if (!active_) return 0;
    const float enter = std::min(elapsed_ / kEnterSeconds, 1.0f);
    return enter * (1.0f - std::min(fading_ / kFadeSeconds, 1.0f));
}

// Passa do tamanho final na entrada e depois pulsa de leve.
float RecordBanner::scale() const {
    if (!active_) return 1.0f;
    const float enter = std::min(elapsed_ / kEnterSeconds, 1.0f);
    const float overshoot = 0.6f + 0.4f * enter + 0.25f * std::sin(enter * 3.14159f);
    return overshoot + (enter >= 1.0f ? 0.03f * std::sin((elapsed_ - kEnterSeconds) * 5.0f) : 0.0f);
}

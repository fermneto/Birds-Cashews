#include "net/RemoteTrack.h"
#include <cmath>

namespace net {
namespace {
constexpr size_t kMaxSamples = 32;
constexpr double kOffsetFollow = 0.02;   // Quanto a estimativa sobe por pacote quando a rede fica mais lenta.
constexpr float kFullTurn = 6.2831853f;

float lerpAngle(float a, float b, float t) { return a + std::remainder(b - a, kFullTurn) * t; }
}

void RemoteTrack::push(double senderTime, double localTime, const PlayerPublicState& state) {
    if (!samples_.empty() && senderTime <= samples_.back().time) return;
    const double offset = localTime - senderTime;
    if (samples_.empty() || offset < offset_) offset_ = offset;
    else offset_ += (offset - offset_) * kOffsetFollow;
    samples_.push_back({senderTime, state});
    while (samples_.size() > kMaxSamples) samples_.pop_front();
}

PlayerPublicState RemoteTrack::sample(double localTime) const {
    if (samples_.empty()) return {};
    const double time = localTime - offset_ - kInterpolationDelay;
    if (time <= samples_.front().time) return samples_.front().state;

    const Sample& last = samples_.back();
    if (time >= last.time) {
        PlayerPublicState state = last.state;
        state.position += state.velocity * static_cast<float>(std::min(time - last.time, kMaxExtrapolation));
        return state;
    }

    size_t next = 1;
    while (samples_[next].time < time) next++;
    const Sample& a = samples_[next - 1];
    const Sample& b = samples_[next];
    const double span = b.time - a.time;
    const float s = static_cast<float>((time - a.time) / span), h = static_cast<float>(span);
    const float s2 = s * s, s3 = s2 * s;
    const float h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;

    PlayerPublicState state = s < 0.5f ? a.state : b.state;   // Campos discretos vem do estado mais proximo.
    state.position = h00 * a.state.position + h10 * h * a.state.velocity + h01 * b.state.position
                   + h11 * h * b.state.velocity;
    state.velocity = glm::mix(a.state.velocity, b.state.velocity, s);
    state.yaw = lerpAngle(a.state.yaw, b.state.yaw, s);
    state.pitch = lerpAngle(a.state.pitch, b.state.pitch, s);
    state.roll = lerpAngle(a.state.roll, b.state.roll, s);
    return state;
}

} // namespace net

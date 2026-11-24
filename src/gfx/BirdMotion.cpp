#include "gfx/BirdMotion.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kTau = 6.2831853f;

float approach(float value, float target, float rate, float dt) {
    return value + (target - value) * std::min(1.0f, rate * dt);
}

// Arremessado, o passaro tenta bater as asas e falha: cada asa num tempo, batidas curtas que perdem o ritmo,
// abertura que fecha e abre e o vento jogando as pontas para tras.
WingPose flailPose(float t) {
    const float left = 0.30f + 0.60f * std::sin(t * 14.0f + 0.9f * std::sin(t * 3.7f)) * (0.6f + 0.4f * std::sin(t * 5.1f));
    const float right = 0.30f + 0.60f * std::sin(t * 15.3f + 1.7f + 0.9f * std::sin(t * 4.1f)) * (0.6f + 0.4f * std::sin(t * 4.4f + 1.0f));
    WingPose pose;
    pose.lift = 0.5f * (left + right) + 0.08f * std::sin(t * 19.0f) * (0.5f + 0.5f * std::sin(t * 2.3f));
    pose.tilt = 0.5f * (right - left);
    pose.spread = 0.75f + 0.20f * std::sin(t * 9.0f);
    pose.sweep = 0.45f + 0.25f * std::sin(t * 6.3f + 0.5f);
    return pose;
}

void blendFlail(WingPose& pose, const WingPose& flail, float amount) {
    pose.lift += (flail.lift - pose.lift) * amount;
    pose.tilt += (flail.tilt - pose.tilt) * amount;
    pose.spread += (flail.spread - pose.spread) * amount;
    pose.sweep += (flail.sweep - pose.sweep) * amount;
}
}

WingPose BirdMotion::advance(const BirdInstance& bird, float beatRate, float time) {
    State& s = states_[bird.key];
    const float dt = s.started ? std::clamp(time - s.lastTime, 0.0f, 0.1f) : 0.0f;
    const float climb = dt > 0 ? (bird.position.y - s.lastPosition.y) / dt : 0.0f;
    s.lastTime = time;
    s.lastPosition = bird.position;
    const float personal = static_cast<float>(bird.seed % 997u) / 997.0f;

    // Cada modo vira um alvo de batida e de recolhimento; o estado segue o alvo aos poucos.
    float flapTarget = 0, foldTarget = 0;
    if (bird.mode == FlightMode::Flap) {
        flapTarget = 1;
    } else if (bird.mode == FlightMode::Dive) {
        foldTarget = 1;
    } else {
        // Voo intermitente: rajadas curtas de batidas entre os planeios, num ritmo proprio de cada passaro.
        const float cycle = 3.0f + 2.5f * personal;
        const float moment = time / cycle + personal;
        if (moment - std::floor(moment) < bird.glideFlaps) flapTarget = 0.85f;
        // Subindo sem bater asa nao convence: a subida pede batidas.
        if (climb > 2.5f) flapTarget = std::max(flapTarget, std::min(1.0f, (climb - 2.5f) / 4.0f));
    }
    const float perchTarget = bird.perched ? 1.0f : 0.0f;
    if (bird.perched) flapTarget = foldTarget = 0;
    if (!s.started) {
        s.started = true;
        s.flap = flapTarget;
        s.fold = foldTarget;
        s.perch = perchTarget;
        s.phase = personal * kTau;
    }
    s.flap = approach(s.flap, flapTarget, 5.0f, dt);
    s.fold = approach(s.fold, foldTarget, 4.0f, dt);
    s.perch = approach(s.perch, perchTarget, 3.0f, dt);
    s.flail = bird.flail > s.flail ? approach(s.flail, bird.flail, 10.0f, dt) : approach(s.flail, bird.flail, 3.0f, dt);
    s.phase = std::fmod(s.phase + dt * beatRate * (0.35f + 0.65f * s.flap), kTau);

    WingPose pose;
    // No planeio as asas flexionam com o vento; batendo, sobem e descem pela fase.
    const float flex = 0.06f * std::sin(time * 3.1f + personal * kTau) + 0.025f * std::sin(time * 1.1f + personal * 11.0f);
    pose.lift = 0.08f + flex * (1.0f - s.flap) + s.flap * (0.10f + 0.72f * std::sin(s.phase));
    // Mergulhando, as asas recolhem para tras e tremem com o vento forte.
    const float tremble = 0.30f + 0.05f * std::sin(time * 13.0f + personal * kTau);
    pose.lift += (tremble - pose.lift) * s.fold;
    pose.spread = 1.0f - 0.45f * s.fold;
    pose.sweep = 0.55f * s.fold;
    // Pousado, as asas dobram rentes ao corpo.
    pose.lift += (-0.25f - pose.lift) * s.perch;
    pose.spread += (0.30f - pose.spread) * s.perch;
    pose.sweep += (0.95f - pose.sweep) * s.perch;
    if (s.flail > 0.001f) blendFlail(pose, flailPose(time + personal * 7.0f), s.flail);
    return pose;
}

void BirdMotion::forgetUnseen(float time) {
    for (auto it = states_.begin(); it != states_.end();) {
        if (std::abs(time - it->second.lastTime) > 1.0f) it = states_.erase(it);
        else ++it;
    }
}

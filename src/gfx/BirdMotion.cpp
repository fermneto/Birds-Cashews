#include "gfx/BirdMotion.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kTau = 6.2831853f;

float approach(float value, float target, float rate, float dt) {
    return value + (target - value) * std::min(1.0f, rate * dt);
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
    return pose;
}

void BirdMotion::forgetUnseen(float time) {
    for (auto it = states_.begin(); it != states_.end();) {
        if (std::abs(time - it->second.lastTime) > 1.0f) it = states_.erase(it);
        else ++it;
    }
}

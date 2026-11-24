#include "events/BoundaryWhale.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265f, kTwoPi = 6.2831853f;

float unit(float a, float b, float x) { return std::clamp((x - a) / (b - a), 0.0f, 1.0f); }
float ease(float a, float b, float x) { return glm::smoothstep(a, b, x); }
glm::vec3 lifted(glm::vec3 point, float y) { return {point.x, y, point.z}; }
}

namespace whale {
namespace {
constexpr float kBitePitch = 0.95f;   // Cabeca erguida no apice, na hora da mordida.
constexpr float kChewPitch = 0.3f;

// Base de eixos da cabeca: guinada pela direcao horizontal, arfagem e rolagem em radianos.
Pose orient(glm::vec3 heading, float pitch, float roll) {
    Pose pose;
    pose.forward = heading * std::cos(pitch) + glm::vec3(0, std::sin(pitch), 0);
    const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), pose.forward));
    const glm::vec3 up = glm::cross(pose.forward, right);
    pose.right = right * std::cos(roll) + up * std::sin(roll);
    pose.up = glm::cross(pose.forward, pose.right);
    return pose;
}
}

glm::vec3 outward(glm::vec3 position) {
    const glm::vec2 flat(position.x, position.z);
    const float length = glm::length(flat);
    return length > 1e-3f ? glm::vec3(flat.x / length, 0, flat.y / length) : glm::vec3(0, 0, 1);
}

float yawToCenter(glm::vec3 position) { return std::atan2(-position.x, position.z); }

glm::mat4 Pose::model() const {
    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right * kScale, 0);
    model[1] = glm::vec4(up * kScale, 0);
    model[2] = glm::vec4(forward * kScale, 0);
    model[3] = glm::vec4(mouth, 1);
    return model;
}

glm::vec3 Pose::holdPoint() const { return mouth + (up * 0.5f - forward * 3.5f) * kScale; }

// A baleia vem de fora do mapa olhando para a ilha. Salta em arco balistico ate a altura do passaro, fecha a
// boca no apice, cai de volta com ele, bate o corpo na agua (que freia a cabeca), mastiga com a cabeca fora da
// agua, cospe e mergulha de cabeca. Os instantes vem de whaleTimeline, pela altura da mordida.
Pose poseAt(glm::vec3 bite, float age) {
    const WhaleTimeline time = whaleTimeline(bite.y);
    age = std::max(age, 0.0f);
    const glm::vec3 out = outward(bite), in = -out;
    // Um passaro que demorou a ser mordido e ficou mais longe e levado de volta ate a baleia, junto do limite.
    const float reach = std::min(glm::length(glm::vec2(bite.x, bite.z)), World::kBoundaryRadius + 15.0f);
    const glm::vec3 base = out * reach;
    // No apice o ponto entre as mandibulas fica sobre o passaro.
    const Pose biting = orient(in, kBitePitch, 0);
    glm::vec3 top = base + glm::vec3(0, time.target, 0) - (biting.up * 0.5f - biting.forward * 3.5f) * kScale;
    top.y = time.apex;
    const glm::vec3 deep = lifted(top, 0) + out * (28.0f + 0.08f * time.apex);   // Saltos altos tambem andam mais.
    const glm::vec3 chew = base + out * 4.0f + glm::vec3(0, 6.5f, 0);
    const float chewSeconds = time.spit - time.fallEnd;
    glm::vec3 mouth;
    float pitch = 0, roll = 0, jaw = 0;
    if (age < time.bite) {
        // Sobe freando sob a gravidade; anda em linha reta para dentro, de fora do mapa ate o passaro.
        const float u = unit(0, time.bite, age), left = time.bite - age;
        mouth = glm::mix(deep, top, u);
        mouth.y = time.apex - 0.5f * time.gravity * left * left;
        pitch = glm::mix(1.3f, kBitePitch, u);
        jaw = kJawOpen * ease(0.15f, 0.75f, u);
    } else if (age < time.fallEnd) {
        // Cai solta ate o corpo bater na agua; dai a cabeca freia por igual ate parar na altura de mastigar.
        const float u = unit(time.bite, time.fallEnd, age);
        mouth = glm::mix(top, chew, ease(0, 1, u));
        if (age < time.brake) {
            const float fall = age - time.bite;
            mouth.y = time.apex - 0.5f * time.gravity * fall * fall;
        } else {
            const float braking = time.fallEnd - time.brake, s = std::min(age - time.brake, braking);
            const float startY = time.apex - 0.5f * time.gravity * (time.brake - time.bite) * (time.brake - time.bite);
            const float slowing = braking > 0 ? time.impactSpeed / braking : 0.0f;
            mouth.y = glm::mix(startY - time.impactSpeed * s + 0.5f * slowing * s * s, chew.y, ease(0.85f, 1.0f, s / braking));
        }
        pitch = glm::mix(kBitePitch, kChewPitch, ease(0, 1, u));
        jaw = kJawOpen * (1.0f - ease(0, 0.12f, age - time.bite));   // Fecha de uma vez.
    } else if (age < time.spit) {
        // Mastiga 2,5 vezes por segundo. O balanco comeca e termina parado, para emendar com as outras fases.
        const float t = age - time.fallEnd;
        const float calm = std::sin(kPi * t / chewSeconds);
        mouth = chew + glm::vec3(0, 0.7f * calm * std::sin(kTwoPi * 1.2f * t), 0);
        pitch = kChewPitch + 0.05f * calm * std::sin(kTwoPi * 0.8f * t);
        roll = 0.10f * calm * std::sin(kTwoPi * 0.9f * t);
        jaw = 0.3f * (0.5f - 0.5f * std::cos(kTwoPi * 2.5f * t));
    } else if (age < time.sinkStart) {
        const float u = unit(time.spit, time.sinkStart, age);
        mouth = chew + glm::vec3(0, 1.2f * ease(0, 1, u), 0);
        pitch = glm::mix(kChewPitch, 0.55f, ease(0, 1, u));
        jaw = 0.3f + 0.6f * ease(0, 0.25f, u);
    } else {
        const float u = ease(0, 1, unit(time.sinkStart, time.end, age));
        mouth = chew + glm::vec3(0, 1.2f - 42.0f * u, 0) + in * (12.0f * u);
        pitch = glm::mix(0.55f, -0.8f, u);
        jaw = 0.9f * (1.0f - ease(0, 0.3f, u));
    }
    Pose pose = orient(in, pitch, roll);
    pose.mouth = mouth;
    pose.jaw = jaw;
    return pose;
}

// Enquanto a baleia sobe, o passaro perde o embalo aos poucos e paira, virando para ela; um ajuste suave ao longo
// de todo o salto o leva ate onde a boca vai fechar, sem puxao. Preso, ele gira para olhar por entre as
// mandibulas na direcao da ilha, que e para onde vai ser cuspido.
HeldBird heldAt(glm::vec3 bite, glm::vec3 start, glm::vec3 velocity, float startYaw, float age) {
    const WhaleTimeline time = whaleTimeline(bite.y);
    const Pose pose = poseAt(bite, std::min(age, time.spit));
    const float yawOut = yawToCenter(bite) + kPi;
    HeldBird held;
    if (age < time.bite) {
        const auto coast = [&](float t) { return start + velocity * (kCoastSeconds * (1.0f - std::exp(-std::max(t, 0.0f) / kCoastSeconds))); };
        const glm::vec3 aim = poseAt(bite, time.bite).holdPoint();
        const float s = ease(0, time.bite, age);
        held.position = coast(age) + (aim - coast(time.bite)) * s;
        held.yaw = startYaw + std::remainder(yawOut - startYaw, kTwoPi) * s;
        held.pitch = -0.3f * s;
        return held;
    }
    const float turn = ease(time.bite, time.bite + 0.4f, age);
    held.position = pose.holdPoint();
    held.yaw = std::remainder(yawOut + kPi * turn, kTwoPi);
    held.pitch = glm::mix(-0.3f, 0.6f * std::asin(std::clamp(pose.forward.y, -1.0f, 1.0f)), turn);
    return held;
}
} // namespace whale

using namespace whale;

namespace {
bool crossed(float before, float after, float moment) { return before < moment && after >= moment; }

SeaMark markAt(SeaMarkKind kind, glm::vec3 position, float yaw, glm::vec3 scale) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model = glm::rotate(model, -yaw, glm::vec3(0, 1, 0));
    return {kind, glm::scale(model, scale)};
}

// Gotas lancadas de um ponto, que sobem e caem de volta no mar.
void appendDroplets(WhaleScene& scene, glm::vec3 from, glm::vec3 push, float spread, float rise, float time, int count, float size) {
    for (int i = 0; i < count; i++) {
        const float angle = 6.2831853f * (static_cast<float>(i) + 0.37f * (i % 3)) / static_cast<float>(count);
        const glm::vec3 velocity = push + glm::vec3(std::cos(angle) * spread, rise * (0.75f + 0.25f * ((i * 7) % 4) / 3.0f),
                                                    std::sin(angle) * spread);
        const glm::vec3 point = from + velocity * time + glm::vec3(0, -9.8f * time * time, 0);
        if (point.y < 0) continue;
        const float shrink = std::max(0.2f, 1.0f - time * 0.6f);
        scene.marks.push_back(markAt(SeaMarkKind::Bubble, point, 0, glm::vec3(size * shrink)));
    }
}

// Espuma que se abre e depois some; raio maximo em metros.
void appendFoam(WhaleScene& scene, glm::vec3 center, float progress, float smallest, float largest) {
    if (progress < 0 || progress > 1) return;
    const float radius = glm::mix(smallest, largest, std::sqrt(progress)) * (1.0f - glm::smoothstep(0.75f, 1.0f, progress));
    if (radius < 0.3f) return;
    scene.marks.push_back(markAt(SeaMarkKind::Foam, lifted(center, 0.4f), progress * 2.0f, glm::vec3(radius, 1, radius)));
}
}

void BoundaryWhale::handle(const GameEvent& event, const FlightModel& localFlight) {
    if (event.type != GameEventType::WhaleBite || event.target >= kMaxPlayers) return;
    attacks_.erase(std::remove_if(attacks_.begin(), attacks_.end(), [&](const WhaleAttack& a) { return a.target == event.target; }),
                   attacks_.end());
    WhaleAttack attack;
    attack.target = event.target;
    attack.bite = event.position;
    attack.timeline = whaleTimeline(event.position.y);
    if (event.target == localPlayer_) {
        attack.start = localFlight.position();
        attack.velocity = localFlight.velocity();
        attack.startYaw = localFlight.yaw();
        waitTimer_ = 0;
        cooldown_ = attack.timeline.spit + kCooldown;
    } else {
        attack.start = event.position;
        attack.startYaw = yawToCenter(event.position) + kPi;
    }
    attacks_.push_back(attack);
}

WhaleAttack* BoundaryWhale::localAttack() {
    for (WhaleAttack& attack : attacks_)
        if (attack.target == localPlayer_) return &attack;
    return nullptr;
}

const WhaleAttack* BoundaryWhale::localAttack() const {
    for (const WhaleAttack& attack : attacks_)
        if (attack.target == localPlayer_) return &attack;
    return nullptr;
}

bool BoundaryWhale::holdsLocal() const {
    const WhaleAttack* attack = localAttack();
    return attack && !attack->spat;
}

bool BoundaryWhale::throwsLocal() const {
    const WhaleAttack* attack = localAttack();
    return attack && attack->spat && attack->age < attack->timeline.throwEnd;
}

bool BoundaryWhale::busy() const { return localAttack() != nullptr; }

// Cuspido da boca ja de frente para o centro da ilha, com forca e subindo. Durante o arremesso a velocidade
// e mantida, e o jogador so volta a controlar o voo quando ele acaba.
bool BoundaryWhale::stepBird(PlayerBird& bird) {
    WhaleAttack* attack = localAttack();
    if (!attack) return false;
    FlightModel& flight = bird.flight();
    if (attack->spat) {
        if (attack->age < attack->timeline.throwEnd) {
            const glm::vec3 p = flight.position();
            const glm::vec2 inward = glm::length(glm::vec2(p.x, p.z)) > 1.0f ? -glm::normalize(glm::vec2(p.x, p.z)) : glm::vec2(0, -1);
            flight.setVelocity(glm::vec3(inward.x, 0, inward.y) * kSpitSpeed + glm::vec3(0, kSpitClimb, 0));
            flight.setAttitude(0.2f, 0.0f);
        }
        return false;
    }
    if (attack->age >= attack->timeline.spit) {
        const HeldBird held = heldAt(attack->bite, attack->start, attack->velocity, attack->startYaw, attack->timeline.spit);
        flight.place(held.position, yawToCenter(attack->bite), kSpitSpeed);
        flight.setVerticalSpeed(kSpitClimb);
        flight.setAttitude(0.2f, 0.0f);
        attack->spat = true;
        return false;
    }
    const HeldBird held = heldAt(attack->bite, attack->start, attack->velocity, attack->startYaw, attack->age);
    flight.place(held.position, held.yaw, 0.0f);
    flight.setAttitude(held.pitch, 0.0f);
    return true;
}

void BoundaryWhale::update(float seconds, PlayerBird& bird, const World& world, GameAuthority& authority, bool canTrigger) {
    seconds = std::max(0.0f, seconds);
    clock_ += seconds;
    cooldown_ = std::max(0.0f, cooldown_ - seconds);
    waitTimer_ = std::max(0.0f, waitTimer_ - seconds);
    advanceAttacks(seconds, bird);
    updateWarning(seconds, bird, world);
    requestBite(bird, world, authority, canTrigger);
}

// Sons e tremores nos momentos marcados. O passaro local so e solto depois de cuspido, mesmo que o jogo
// tenha ficado parado nos menus enquanto a baleia mastigava.
void BoundaryWhale::advanceAttacks(float seconds, PlayerBird& bird) {
    for (WhaleAttack& attack : attacks_) {
        const float before = attack.age, after = attack.age + seconds;
        attack.age = after;
        const WhaleTimeline& time = attack.timeline;
        const bool mine = attack.target == localPlayer_;
        const auto cue = [&](WhaleCueKind kind, float moment, float volume, float shake, glm::vec3 where) {
            if (!crossed(before, after, moment)) return;
            cues_.push_back({kind, where, volume});
            if (mine) bird.shake(shake);
        };
        const auto at = [&](float moment) { return poseAt(attack.bite, moment).mouth; };
        // O canto vem de onde ela vai abocanhar: quem esta la no alto ouve a baleia chegando de baixo.
        if (before <= 0.0f && after > 0.0f) cues_.push_back({WhaleCueKind::Song, at(time.bite), 1.0f});
        cue(WhaleCueKind::Splash, time.breach, 0.8f, 0.3f, lifted(at(time.breach), 0));
        cue(WhaleCueKind::Bite, time.bite, 1.0f, 1.0f, at(time.bite));
        // Queda de mais alto, pancada maior: o splash vem com um ronco grave por baixo.
        const float heavy = std::clamp((time.impactSpeed - 50.0f) / 150.0f, 0.0f, 1.0f);
        cue(WhaleCueKind::Splash, time.brake, 1.0f, 0.7f + 0.3f * heavy, at(time.brake));
        if (heavy > 0.15f) cue(WhaleCueKind::Rumble, time.brake, 0.5f + 0.5f * heavy, 0.0f, at(time.brake));
        for (float chew = time.fallEnd + 0.4f; chew < time.spit; chew += 0.4f) cue(WhaleCueKind::Chew, chew, 0.9f, 0.55f, at(chew));
        cue(WhaleCueKind::Spit, time.spit, 1.0f, 0.8f, at(time.spit));
        cue(WhaleCueKind::Splash, time.spit + 0.9f, 0.6f, 0.0f, at(time.spit + 0.9f));
    }
    attacks_.erase(std::remove_if(attacks_.begin(), attacks_.end(), [&](const WhaleAttack& a) {
                       if (a.target != localPlayer_) return a.age >= a.timeline.end;
                       return a.spat && a.age >= std::max(a.timeline.end, a.timeline.throwEnd);
                   }),
                   attacks_.end());
}

// Perto do limite, sobre o mar, a sombra da baleia ronda por baixo da agua, sobem bolhas, ela espirra
// pelo espiraculo e um ronco grave fica cada vez mais alto. Da tempo de virar e voltar.
void BoundaryWhale::updateWarning(float seconds, const PlayerBird& bird, const World& world) {
    const glm::vec3 p = bird.flight().position();
    const float radius = glm::length(glm::vec2(p.x, p.z));
    const bool sea = world.island().heightAt(p.x, p.z) < 0.0f;
    const float target = sea && !busy() ? glm::smoothstep(World::kWarningRadius, World::kBoundaryRadius, radius) : 0.0f;
    warning_ += (target - warning_) * (1.0f - std::exp(-4.0f * seconds));
    if (target == 0.0f && warning_ < 0.01f) warning_ = 0.0f;
    const float angle = std::atan2(p.z, p.x) + 0.03f * std::sin(0.35f * clock_);
    lurk_ = glm::vec3(std::cos(angle), 0, std::sin(angle)) * (World::kBoundaryRadius + 35.0f);

    if (spoutAge_ >= 0) spoutAge_ = spoutAge_ + seconds > 1.4f ? -1.0f : spoutAge_ + seconds;
    if (warning_ < 0.1f) {
        rumbleTimer_ = 0.3f;
        spoutTimer_ = 1.2f;
        return;
    }
    rumbleTimer_ -= seconds;
    if (rumbleTimer_ <= 0) {
        cues_.push_back({WhaleCueKind::Rumble, lurk_, 0.3f + 0.7f * warning_});
        rumbleTimer_ = 3.0f - 1.2f * warning_;
    }
    if (warning_ < 0.35f) return;
    spoutTimer_ -= seconds;
    if (spoutTimer_ <= 0) {
        spoutAge_ = 0;
        cues_.push_back({WhaleCueKind::Spout, lurk_, 0.5f + 0.5f * warning_});
        spoutTimer_ = 5.0f - 2.0f * warning_;
    }
}

void BoundaryWhale::requestBite(const PlayerBird& bird, const World& world, GameAuthority& authority, bool canTrigger) {
    if (!canTrigger || busy() || cooldown_ > 0 || waitTimer_ > 0) return;
    const glm::vec3 p = bird.flight().position();
    if (glm::length(glm::vec2(p.x, p.z)) <= World::kBoundaryRadius || world.island().heightAt(p.x, p.z) >= 0.0f) return;
    GameEvent event;
    event.type = GameEventType::WhaleBite;
    event.actor = event.target = localPlayer_;
    event.position = p;
    authority.request(event);
    waitTimer_ = kRetrySeconds;
}

bool BoundaryWhale::heldPose(PlayerId id, HeldBird& pose) const {
    for (const WhaleAttack& attack : attacks_) {
        if (attack.target != id || attack.age >= attack.timeline.spit) continue;
        pose = heldAt(attack.bite, attack.start, attack.velocity, attack.startYaw, attack.age);
        return true;
    }
    return false;
}

std::vector<WhaleCue> BoundaryWhale::drainCues() {
    std::vector<WhaleCue> cues;
    cues.swap(cues_);
    return cues;
}

WhaleScene BoundaryWhale::scene() const {
    WhaleScene scene;
    for (const WhaleAttack& attack : attacks_) {
        if (attack.age >= attack.timeline.end) continue;
        const whale::Pose pose = poseAt(attack.bite, attack.age);
        scene.whales.push_back({pose.model(), pose.jaw});
        appendAttackMarks(scene, attack);
    }
    appendWarningMarks(scene);
    return scene;
}

// Espuma onde ela rompe a agua, onde cai, enquanto mastiga e onde mergulha; gotas no salto, na queda e na cuspida.
// No salto escorre agua da cauda; o splash da queda cresce com a velocidade da pancada.
void BoundaryWhale::appendAttackMarks(WhaleScene& scene, const WhaleAttack& attack) const {
    const float age = attack.age;
    const WhaleTimeline& time = attack.timeline;
    const glm::vec3 out = outward(attack.bite), fall = lifted(poseAt(attack.bite, time.spit).mouth, 0.0f);
    const glm::vec3 breach = lifted(poseAt(attack.bite, time.breach).mouth, 0.0f);
    const auto during = [&](float from, float to) { return age >= from ? unit(from, to, age) : -1.0f; };
    appendFoam(scene, breach, during(time.breach, time.breach + 1.25f), 4.0f, 16.0f);
    if (age >= time.breach && age < time.breach + 1.25f) appendDroplets(scene, breach, glm::vec3(0), 6.0f, 14.0f, age - time.breach, 10, 1.3f);

    // Agua escorrendo do corpo: gotas soltas em instantes fixos, que caem sozinhas dali.
    if (age > time.breach && age < time.brake + 1.0f) {
        constexpr float kEvery = 0.08f;
        for (int i = 0; i < 12; i++) {
            const float dropped = (std::floor(age / kEvery) - i) * kEvery;
            if (dropped < time.breach || dropped > time.brake) continue;
            const whale::Pose from = poseAt(attack.bite, dropped);
            const float along = 18.0f + 3.0f * (i % 4), side = 2.5f * std::sin(i * 2.3f);
            glm::vec3 point = from.mouth - from.forward * (along * kScale) + from.right * side;
            const float since = age - dropped;
            point.y -= 0.5f * 9.8f * since * since;
            if (point.y < 0) continue;
            scene.marks.push_back(markAt(SeaMarkKind::Bubble, point, 0, glm::vec3(1.1f * (1.0f - 0.6f * since))));
        }
    }

    const float heavy = std::sqrt(std::clamp(time.impactSpeed / 50.0f, 1.0f, 4.5f));   // 1 numa queda baixa, ~2 do teto.
    const float splashSeconds = 1.2f * heavy;
    appendFoam(scene, fall, during(time.brake, time.brake + splashSeconds), 6.0f * heavy, 24.0f * heavy);
    if (age >= time.brake && age < time.brake + splashSeconds)
        appendDroplets(scene, fall + out * 6.0f, glm::vec3(0), 9.0f * heavy, 18.0f * heavy, age - time.brake,
                       14 + static_cast<int>(12.0f * (heavy - 1.0f)), 1.6f * heavy);
    if (heavy > 1.4f && age >= time.brake && age < time.brake + 1.4f) {   // Coluna de agua da pancada.
        const float u = (age - time.brake) / 1.4f;
        const float rise = glm::smoothstep(0.0f, 0.25f, u) * (1.0f - glm::smoothstep(0.55f, 1.0f, u));
        if (rise > 0.01f)
            scene.marks.push_back(markAt(SeaMarkKind::Spray, lifted(fall + out * 14.0f, 0.2f), 0,
                                         glm::vec3(2.2f * heavy, 8.0f * heavy * heavy * rise, 2.2f * heavy)));
    }
    if (age >= time.fallEnd && age < time.sinkStart) appendFoam(scene, fall + out * 20.0f, 0.5f, 12.0f, 12.0f);   // Onde o corpo entra na agua.
    if (age >= time.spit && age < time.spit + 0.7f) {
        const whale::Pose pose = poseAt(attack.bite, time.spit);
        appendDroplets(scene, pose.mouth, -out * 24.0f + glm::vec3(0, 3, 0), 2.5f, 4.0f, age - time.spit, 9, 0.7f);
    }
    appendFoam(scene, fall - out * 12.0f, during(time.spit + 0.8f, time.end), 4.0f, 14.0f);
}

void BoundaryWhale::appendWarningMarks(WhaleScene& scene) const {
    if (warning_ < 0.02f) return;
    const glm::vec3 out = outward(lurk_);
    const glm::vec3 along(-out.z, 0, out.x);   // A sombra nada ao longo do limite.
    const float yaw = std::atan2(along.x, -along.z);
    const float size = 0.5f + 0.5f * warning_;
    scene.marks.push_back(markAt(SeaMarkKind::Shadow, lifted(lurk_, 0.3f), yaw, glm::vec3(11.0f, 1.0f, 34.0f) * size));
    const glm::vec3 head = lurk_ + along * (24.0f * size);
    if (warning_ > 0.25f) {
        for (int i = 0; i < 7; i++) {
            const float phase = std::fmod(clock_ * 0.8f + i * 0.37f, 1.0f);
            const glm::vec3 point = head + along * (-4.0f * i) + out * (1.5f * std::sin(i * 2.1f)) + glm::vec3(0, 0.2f + 0.8f * phase, 0);
            scene.marks.push_back(markAt(SeaMarkKind::Bubble, point, 0, glm::vec3((0.4f + 0.5f * warning_) * (1.0f - 0.6f * phase))));
        }
    }
    if (spoutAge_ < 0) return;
    const float rise = glm::smoothstep(0.0f, 0.3f, spoutAge_) * (1.0f - glm::smoothstep(0.9f, 1.4f, spoutAge_));
    if (rise <= 0.01f) return;
    scene.marks.push_back(markAt(SeaMarkKind::Spray, lifted(head, 0.2f), yaw, glm::vec3(2.0f, 15.0f * rise, 2.0f)));
    appendDroplets(scene, lifted(head, 13.0f * rise), glm::vec3(0), 3.0f, 2.0f, std::fmod(spoutAge_, 0.7f), 8, 0.8f);
}

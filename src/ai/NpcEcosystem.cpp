#include "ai/NpcEcosystem.h"
#include "world/Rng.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kStep = 1.0f / 30.0f;
constexpr float kFruitRange = 240.0f, kPickupRange = 8.0f;
constexpr float kHuntRange = 210.0f, kHitRange = 7.0f;
constexpr uint8_t kHuntCargo = 2;

float lengthSquared(glm::vec3 value) { return glm::dot(value, value); }

glm::vec3 direction(glm::vec3 value) {
    const float distance = glm::length(value);
    return distance > 0.001f ? value / distance : glm::vec3(0);
}

glm::vec3 patrol(const NpcBird& bird, float radius) {
    const glm::vec3 offset = bird.position - bird.home;
    const glm::vec3 radial = direction(glm::vec3(offset.x, 0, offset.z));
    const glm::vec3 tangent(radial.z, 0, -radial.x);
    return tangent + radial * ((radius - glm::length(glm::vec2(offset.x, offset.z))) / radius)
           - glm::vec3(0, offset.y / radius, 0);
}

// Separacao, alinhamento e coesao usam as posicoes do mesmo instante.
glm::vec3 flock(const NpcBird& bird, const std::array<NpcBird, 9>& previous) {
    glm::vec3 separation(0), alignment(0), center(0);
    int neighbors = 0;
    for (const auto& other : previous) {
        if (other.role != NpcRole::Flock) continue;
        const glm::vec3 offset = other.position - bird.position;
        const float distance = glm::length(offset);
        if (distance > 40.0f || distance < 0.001f) continue;
        if (distance < 12.0f)
            separation -= (offset / distance) * (1.0f - distance / 12.0f);
        alignment += direction(other.velocity);
        center += other.position;
        ++neighbors;
    }
    const glm::vec3 course = patrol(bird, 38.0f);
    if (neighbors == 0) return course;
    return course + separation * 2.7f + direction(alignment) * 0.70f
           + direction(center / static_cast<float>(neighbors) - bird.position) * 0.45f;
}

const PlayerPublicState* choosePrey(glm::vec3 position, const std::vector<PlayerPublicState>& players) {
    const PlayerPublicState* best = nullptr;
    float bestDistance = kHuntRange * kHuntRange;
    for (const auto& player : players) {
        if (player.id == kNoPlayer || player.carried < kHuntCargo) continue;
        const float distance = lengthSquared(player.position - position);
        if (distance < bestDistance) {
            best = &player;
            bestDistance = distance;
        }
    }
    return best;
}

glm::vec3 forage(NpcBird& bird, const CashewQuery& cashews, GameAuthority& authority) {
    if (bird.cooldown == 0) {
        if (const auto id = cashews.nearest(bird.position, kFruitRange)) {
            const auto fruit = cashews.positionOf(*id);
            const auto offset = fruit - bird.position;
            if (lengthSquared(offset) > kPickupRange * kPickupRange) return direction(offset);
            authority.request({GameEventType::CashewCollected, kNoPlayer, kNoPlayer, *id, 1, fruit});
            bird.cooldown = 7.0f;
        }
    }
    return patrol(bird, 68.0f);
}

glm::vec3 hunt(NpcBird& bird, const PlayerPublicState& prey, GameAuthority& authority) {
    const auto offset = prey.position - bird.position;
    if (lengthSquared(offset) < kHitRange * kHitRange) {
        authority.request({GameEventType::CashewDropped, kNoPlayer, prey.id, kNoCashew,
                           prey.carried, prey.position});
        bird.cooldown = 9.0f;
        return patrol(bird, 95.0f);
    }
    return direction(offset);
}

void fly(NpcBird& bird, const World& world, glm::vec3 course, float speed) {
    glm::vec3 probe = bird.position + direction(course) * (speed * 0.3f);
    const auto ahead = world.resolve(probe, 3.5f);
    if (ahead.hit())
        course += ahead.normal * 1.3f + glm::vec3(0, 0.9f, 0);
    const glm::vec3 desired = direction(course) * speed;
    const glm::vec3 current = direction(bird.velocity);
    const glm::vec3 turn = glm::cross(current, direction(desired));
    bird.velocity = glm::mix(bird.velocity, desired, 1.8f * kStep);
    bird.position += bird.velocity * kStep;

    const auto contact = world.resolve(bird.position, 1.5f);
    if (contact.hit())
        bird.velocity -= contact.normal * std::min(0.0f, glm::dot(bird.velocity, contact.normal));
    bird.yaw = std::atan2(bird.velocity.x, -bird.velocity.z);
    bird.pitch = std::atan2(bird.velocity.y, glm::length(glm::vec2(bird.velocity.x, bird.velocity.z)));
    bird.roll = glm::mix(bird.roll, std::clamp(-turn.y, -0.65f, 0.65f), 4.0f * kStep);
}
}

NpcEcosystem::NpcEcosystem(const World& world, uint32_t seed) : world_(world) {
    Rng rng(seed ^ 0x9ddd466bu);
    const auto spawn = world.spawnPoint();
    for (size_t i = 0; i < birds_.size(); ++i) {
        const auto role = i < 6 ? NpcRole::Flock : i < 8 ? NpcRole::Forager : NpcRole::Hunter;
        // As familias do gerador usam seed % 3: arara, pelicano, bem-te-vi.
        const unsigned family = role == NpcRole::Flock ? 2 : role == NpcRole::Forager ? 0 : 1;
        const auto model = static_cast<uint32_t>(rng.next() % (UINT32_MAX / 3)) * 3 + family;
        const auto home = spawn + (role == NpcRole::Flock ? glm::vec3(-40, 9, -125)
                                 : role == NpcRole::Forager ? glm::vec3(37, -20, -140 - 50 * static_cast<float>(i - 6))
                                 : glm::vec3(72, 18, -155));
        auto position = home + glm::vec3(rng.range(-16.0f, 16.0f), rng.range(-5.0f, 5.0f),
                                         rng.range(-13.0f, 13.0f));
        world_.resolve(position, 1.5f);
        const glm::vec3 velocity(15, 0, -10);
        birds_[i] = {role, model, position, velocity, home, std::atan2(velocity.x, -velocity.z)};
    }
}

void NpcEcosystem::advance(float elapsed, const std::vector<PlayerPublicState>& players,
                          const CashewQuery& cashews, GameAuthority& authority) {
    accumulator_ += std::clamp(elapsed, 0.0f, 0.1f);
    while (accumulator_ >= kStep) {
        step(players, cashews, authority);
        accumulator_ -= kStep;
    }
}

void NpcEcosystem::step(const std::vector<PlayerPublicState>& players,
                       const CashewQuery& cashews, GameAuthority& authority) {
    const auto previous = birds_;
    for (auto& bird : birds_) {
        bird.cooldown = std::max(0.0f, bird.cooldown - kStep);
        switch (bird.role) {
        case NpcRole::Flock:
            bird.mode = FlightMode::Flap;
            fly(bird, world_, flock(bird, previous), 20.0f);
            break;
        case NpcRole::Forager:
            bird.mode = FlightMode::Glide;
            fly(bird, world_, forage(bird, cashews, authority), 23.0f);
            break;
        case NpcRole::Hunter:
            if (const auto* prey = bird.cooldown == 0.0f ? choosePrey(bird.position, players) : nullptr) {
                bird.mode = prey->position.y < bird.position.y - 4.0f ? FlightMode::Dive : FlightMode::Flap;
                fly(bird, world_, hunt(bird, *prey, authority), 42.0f);
            } else {
                bird.mode = FlightMode::Glide;
                fly(bird, world_, patrol(bird, 95.0f), 27.0f);
            }
            break;
        }
    }
}

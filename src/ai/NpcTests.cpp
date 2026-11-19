#include "ai/NpcTests.h"
#include "ai/NpcEcosystem.h"
#include "core/TestRun.h"
#include "game/CashewCollector.h"
#include "shared/LocalAuthority.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr uint32_t kSeed = 20261207;

bool boundedFlock(const NpcEcosystem& ecosystem, const World& world) {
    const auto& birds = ecosystem.birds();
    const auto flockCount = std::count_if(birds.begin(), birds.end(), [](const NpcBird& bird) {
        return bird.role == NpcRole::Flock;
    });
    if (flockCount < 2) return false;
    for (const auto& bird : birds) {
        const float distance = glm::length(bird.position - world.spawnPoint());
        if (!std::isfinite(distance) || distance > 500.0f) return false;
        if (bird.role != NpcRole::Flock) continue;
        if (glm::length(bird.position - bird.home) > 90.0f) return false;
        const bool hasNeighbor = std::any_of(birds.begin(), birds.end(), [&](const NpcBird& other) {
            return &other != &bird && other.role == NpcRole::Flock
                   && glm::length(other.position - bird.position) < 40.0f;
        });
        if (!hasNeighbor) return false;
    }
    return true;
}
}

int runNpcSelfTest() {
    TestRun test;
    const World world(kSeed);
    NpcEcosystem ecosystem(world, kSeed);
    const auto& birds = ecosystem.birds();
    const auto forager = std::find_if(birds.begin(), birds.end(), [](const NpcBird& bird) {
        return bird.role == NpcRole::Forager;
    });
    const auto hunter = std::find_if(birds.begin(), birds.end(), [](const NpcBird& bird) {
        return bird.role == NpcRole::Hunter;
    });
    const bool allRoles = forager != birds.end() && hunter != birds.end();
    test.check("NPCs: existem os tres portes", allRoles && boundedFlock(ecosystem, world));
    if (!allRoles) return test.failures;

    constexpr PlayerId playerId = 0;
    CashewField field({CashewSpot{forager->position}, CashewSpot{world.spawnPoint()},
                       CashewSpot{world.spawnPoint() + glm::vec3(8, 0, 0)}});
    LocalAuthority authority;
    CashewCollector cargo(field, authority, playerId);
    cargo.handle({GameEventType::CashewCollected, playerId, kNoPlayer, 1, 1});
    cargo.handle({GameEventType::CashewCollected, playerId, kNoPlayer, 2, 1});
    PlayerPublicState prey;
    prey.id = playerId;
    prey.position = hunter->position;
    prey.carried = 1;

    ecosystem.advance(0.04f, {prey}, field, authority);
    bool foraged = false, hunted = false;
    for (const auto& event : authority.pollEvents()) {
        foraged |= event.type == GameEventType::CashewCollected && event.actor == kNoPlayer;
        hunted |= event.type == GameEventType::CashewDropped;
        cargo.handle(event);
    }
    test.check("NPCs: o medio coleta um caju sem dar carga ao jogador",
               foraged && !field.isActive(0) && cargo.carried() == 2);
    test.check("NPCs: o cacador ignora um jogador com pouca carga", !hunted);

    prey.carried = 2;
    prey.position = hunter->position;
    ecosystem.advance(0.04f, {prey}, field, authority);
    for (const auto& event : authority.pollEvents()) {
        hunted |= event.type == GameEventType::CashewDropped && event.target == playerId && event.value == 2;
        cargo.handle(event);
    }
    test.check("NPCs: o cacador derruba a carga do jogador que alcanca", hunted && cargo.carried() == 0);
    ecosystem.advance(0.04f, {prey}, field, authority);
    test.check("NPCs: o cacador nao ataca de novo no quadro seguinte", authority.pollEvents().empty());

    for (int i = 0; i < 600; ++i) ecosystem.advance(0.1f, {}, field, authority);
    test.check("NPCs: o bando fica junto e perto da rota depois de um minuto", boundedFlock(ecosystem, world));
    return test.failures;
}

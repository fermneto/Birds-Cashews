#include "ai/NpcTests.h"
#include "ai/NpcEcosystem.h"
#include "core/TestRun.h"
#include "game/CashewCollector.h"
#include "gfx/BirdMotion.h"
#include "shared/BirdFactory.h"
#include "shared/LocalAuthority.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace {
constexpr uint32_t kSeed = 20261207;
constexpr float kFrame = 1.0f / 60.0f;

std::string withValue(const char* name, float value) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s (%.2f)", name, value);
    return text;
}

class NoCashews final : public CashewQuery {
public:
    std::optional<CashewId> nearest(glm::vec3, float) const override { return std::nullopt; }
    glm::vec3 positionOf(CashewId) const override { return glm::vec3(0); }
};

// Guarda cada pedido; o teste confere quem estava perto quando a carga caiu.
class SpyAuthority final : public GameAuthority {
public:
    void request(const GameEvent& event) override { requested.push_back(event); }
    std::vector<GameEvent> pollEvents() override { return std::exchange(requested, {}); }
    std::vector<PlayerPublicState> players() const override { return {}; }
    void publishLocalPlayer(const PlayerPublicState&) override {}
    std::vector<GameEvent> requested;
};

const NpcBird* firstOf(const NpcEcosystem& ecosystem, NpcRole role) {
    for (const auto& bird : ecosystem.birds())
        if (bird.role == role) return &bird;
    return nullptr;
}

PlayerPublicState playerAt(PlayerId id, glm::vec3 position, glm::vec3 velocity = glm::vec3(0), uint8_t carried = 0) {
    PlayerPublicState player;
    player.id = id;
    player.position = position;
    player.velocity = velocity;
    player.carried = carried;
    player.yaw = std::atan2(velocity.x, -velocity.z);
    return player;
}

float flatDistance(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

bool boundedFlock(const NpcEcosystem& ecosystem) {
    const auto& birds = ecosystem.birds();
    const auto flockCount = std::count_if(birds.begin(), birds.end(), [](const NpcBird& bird) {
        return bird.role == NpcRole::Flock;
    });
    if (flockCount < 2) return false;
    for (const auto& bird : birds) {
        const float edge = glm::length(glm::vec2(bird.position.x, bird.position.z));
        if (!std::isfinite(edge) || edge > World::kWarningRadius) return false;
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

// Nenhum NPC pode estar dentro de copa, tronco, chao ou mar depois de um passo.
int countPenetrations(const NpcEcosystem& ecosystem, const World& world) {
    int inside = 0;
    for (const auto& bird : ecosystem.birds()) {
        glm::vec3 probe = bird.position;
        if (world.resolve(probe, bird.radius * 0.9f).hit() || bird.position.y < bird.radius * 0.9f) ++inside;
    }
    return inside;
}

void testRosterAndLooks(TestRun& test, const World& world, const NpcEcosystem& ecosystem) {
    const auto& birds = ecosystem.birds();
    std::set<NpcRole> roles;
    std::set<uint32_t> seeds;
    for (const auto& bird : birds) {
        roles.insert(bird.role);
        seeds.insert(bird.seed);
    }
    test.check("NPCs: 22 passaros com os 8 papeis", birds.size() == NpcEcosystem::kCount && roles.size() == kNpcRoleCount);
    float closest = 10;
    for (size_t i = 0; i < birds.size(); ++i)
        for (size_t j = i + 1; j < birds.size(); ++j)
            closest = std::min(closest, birdLookDistance(describeBird(birds[i].seed), describeBird(birds[j].seed)));
    test.check(withValue("NPCs: dois NPCs nunca parecem iguais (menor distancia visual)", closest),
               seeds.size() == birds.size() && closest >= 0.40f);
    const auto* hunter = firstOf(ecosystem, NpcRole::Hunter);
    const auto* flock = firstOf(ecosystem, NpcRole::Flock);
    test.check("NPCs: o cacador e grande e o bando e pequeno",
               hunter && flock && describeBird(hunter->seed).length > 2.0f && describeBird(flock->seed).length < 1.5f);

    const NpcEcosystem again(world, kSeed);
    bool same = again.birds().size() == birds.size();
    for (size_t i = 0; same && i < birds.size(); ++i) same = again.birds()[i].seed == birds[i].seed;
    test.check("NPCs: a mesma semente do mundo escolhe os mesmos passaros", same);
}

void testOriginalRoles(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    const auto* forager = firstOf(ecosystem, NpcRole::Forager);
    const auto* hunter = firstOf(ecosystem, NpcRole::Hunter);
    test.check("NPCs: existem os tres portes originais", forager && hunter && boundedFlock(ecosystem));
    if (!forager || !hunter) return;

    constexpr PlayerId playerId = 0;
    CashewField field({CashewSpot{forager->position}, CashewSpot{world.spawnPoint()},
                       CashewSpot{world.spawnPoint() + glm::vec3(8, 0, 0)}});
    LocalAuthority authority;
    CashewCollector cargo(field, authority, playerId);
    // A coleta passa pela autoridade, que so deixa derrubar o que confirmou.
    authority.request({GameEventType::CashewCollected, playerId, kNoPlayer, 1, 1});
    authority.request({GameEventType::CashewCollected, playerId, kNoPlayer, 2, 1});
    for (const auto& event : authority.pollEvents()) cargo.handle(event);
    PlayerPublicState prey = playerAt(playerId, hunter->position, glm::vec3(0), 1);

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
    test.check("NPCs: o bando fica junto e perto da rota depois de um minuto", boundedFlock(ecosystem));
}

void testCurious(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    NoCashews cashews;
    SpyAuthority authority;
    const NpcBird* curious = firstOf(ecosystem, NpcRole::Curious);
    // O jogador passa reto, alto, perto de onde o curioso ronda.
    PlayerPublicState player = playerAt(0, curious->position + glm::vec3(40, 25, 30), glm::vec3(0, 0, -16));
    float closest = 1e9f, followed = 0, farthestLater = 0;
    bool joined = false;
    for (int frame = 0; frame < 60 * 30; ++frame) {
        player.position += player.velocity * kFrame;
        ecosystem.advance(kFrame, {player}, cashews, authority);
        const float distance = glm::length(curious->position - player.position);
        closest = std::min(closest, distance);
        if (distance < 30.0f) { followed += kFrame; joined = true; }
        if (joined && frame > 60 * 15) farthestLater = std::max(farthestLater, distance);
    }
    test.check(withValue("NPCs: o curioso acompanha o jogador de perto (segundos a menos de 30 m)", followed),
               followed > 4.0f);
    test.check(withValue("NPCs: o curioso nunca encosta no jogador (menor distancia em m)", closest), closest > 4.0f);
    test.check(withValue("NPCs: depois de alguns segundos o curioso se afasta (m)", farthestLater), farthestLater > 60.0f);
    test.check("NPCs: o curioso nunca mexe na carga", authority.requested.empty());
}

void testSkittish(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    NoCashews cashews;
    SpyAuthority authority;
    const NpcBird* bird = firstOf(ecosystem, NpcRole::Skittish);
    const glm::vec3 perch = bird->position;
    const bool startsPerched = bird->perched && glm::length(bird->velocity) < 0.01f;
    ecosystem.advance(1.0f / 30.0f + 0.001f, {}, cashews, authority);
    const bool staysPut = bird->perched && glm::length(bird->position - perch) < 0.01f;
    // Alguem chega a 35 m: ele sai voando e ganha distancia.
    const PlayerPublicState intruder = playerAt(0, perch + glm::vec3(30, 8, 15));
    for (int frame = 0; frame < 60 * 3; ++frame) ecosystem.advance(kFrame, {intruder}, cashews, authority);
    const float fled = glm::length(bird->position - intruder.position);
    const bool flew = !bird->perched && fled > 45.0f;
    // O jogador vai embora: o assustado volta a pousar no alto de uma copa.
    bool landed = false;
    for (int frame = 0; frame < 60 * 90 && !landed; ++frame) {
        ecosystem.advance(kFrame, {}, cashews, authority);
        landed = bird->perched;
    }
    test.check("NPCs: o assustado comeca pousado e parado no alto da copa", startsPerched && staysPut);
    test.check(withValue("NPCs: o assustado foge quando alguem chega perto (m)", fled), flew);
    bool onCanopy = false;
    for (size_t tree = 0; tree < world.trees().size(); ++tree)
        onCanopy |= glm::length(ecosystem.perchOn(tree, bird->radius) - bird->position) < 0.5f;
    test.check("NPCs: o assustado volta a pousar numa copa quando fica tranquilo", landed && onCanopy);
}

void testGlider(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    NoCashews cashews;
    SpyAuthority authority;
    const NpcBird* glider = firstOf(ecosystem, NpcRole::Glider);
    float lowest = 1e9f, highest = -1e9f, clearance = 1e9f, travelled = 0;
    bool alwaysGliding = true;
    glm::vec3 last = glider->position;
    for (int frame = 0; frame < 60 * 180; ++frame) {
        ecosystem.advance(kFrame, {}, cashews, authority);
        if (frame < 60 * 5) { last = glider->position; continue; }
        lowest = std::min(lowest, glider->position.y);
        highest = std::max(highest, glider->position.y);
        clearance = std::min(clearance, glider->position.y - std::max(0.0f, world.island().heightAt(glider->position.x, glider->position.z)));
        alwaysGliding &= glider->mode == FlightMode::Glide;
        travelled += flatDistance(glider->position, last);
        last = glider->position;
    }
    test.check(withValue("NPCs: o planador voa alto, acima das copas (folga minima sobre o chao em m)", clearance),
               clearance > 150.0f);
    test.check(withValue("NPCs: o planador sobe nas termicas (variacao de altura em m)", highest - lowest),
               highest - lowest > 60.0f);
    test.check("NPCs: o planador so plana e percorre a ilha", alwaysGliding && travelled > 1500.0f);
}

void testTerritorial(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    NoCashews cashews;
    SpyAuthority authority;
    const NpcBird* guard = firstOf(ecosystem, NpcRole::Territorial);
    const glm::vec3 tree = guard->home;
    // O jogador para ao lado da copa, dentro da area guardada.
    const auto& guarded = *std::min_element(world.trees().begin(), world.trees().end(), [&](const CashewTree& a, const CashewTree& b) {
        return flatDistance(a.base(), tree) < flatDistance(b.base(), tree);
    });
    const PlayerPublicState intruder = playerAt(0, tree + glm::vec3(guarded.reach() + 30.0f, guarded.height() * 0.9f, 0));
    float closest = 1e9f, nearTime = 0;
    for (int frame = 0; frame < 60 * 9; ++frame) {
        ecosystem.advance(kFrame, {intruder}, cashews, authority);
        const float distance = glm::length(guard->position - intruder.position);
        closest = std::min(closest, distance);
        if (distance < 25.0f) nearTime += kFrame;
    }
    test.check(withValue("NPCs: o territorial persegue quem entra perto da arvore (segundos a menos de 25 m)", nearTime),
               nearTime > 2.0f);
    test.check(withValue("NPCs: o territorial nao encosta no jogador (menor distancia em m)", closest), closest > 4.0f);
    for (int frame = 0; frame < 60 * 40; ++frame) ecosystem.advance(kFrame, {}, cashews, authority);
    const float back = flatDistance(guard->position, tree);
    test.check(withValue("NPCs: sem intruso, o territorial volta a rondar a arvore (m do tronco)", back),
               back < guarded.reach() + 80.0f);
    test.check("NPCs: o territorial nunca derruba nem coleta cajus", authority.requested.empty());
}

void testMigrants(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    NoCashews cashews;
    SpyAuthority authority;
    std::vector<const NpcBird*> flock;
    for (const auto& bird : ecosystem.birds())
        if (bird.role == NpcRole::Migrant) flock.push_back(&bird);
    const NpcBird* leader = *std::find_if(flock.begin(), flock.end(), [](const NpcBird* bird) { return bird->slot == 0; });
    const glm::vec3 start = leader->position;
    float worstSlot = 0, farthest = 0, edge = 0, slotSum = 0;
    int slotSamples = 0;
    for (int frame = 0; frame < 60 * 240; ++frame) {
        ecosystem.advance(kFrame, {}, cashews, authority);
        farthest = std::max(farthest, flatDistance(leader->position, start));
        for (const NpcBird* bird : flock) edge = std::max(edge, glm::length(glm::vec2(bird->position.x, bird->position.z)));
        if (frame < 60 * 20 || frame % 30) continue;
        // Cada seguidor perto do seu lugar no V: atras do lider e para um dos lados.
        const glm::vec3 forward = glm::normalize(glm::vec3(leader->velocity.x, 0, leader->velocity.z));
        const glm::vec3 right(-forward.z, 0, forward.x);
        for (const NpcBird* bird : flock) {
            if (bird == leader) continue;
            const float rank = static_cast<float>((bird->slot + 1) / 2), side = bird->slot % 2 ? 1.0f : -1.0f;
            const glm::vec3 place = leader->position - forward * (rank * 10.0f) + right * (side * rank * 8.5f);
            const float miss = flatDistance(bird->position, place);
            worstSlot = std::max(worstSlot, miss);
            slotSum += miss;
            ++slotSamples;
        }
    }
    // Nas curvas do fim da travessia o V abre um pouco; no resto do tempo cada um fica no seu lugar.
    test.check(withValue("NPCs: o bando migratorio segura a formacao em V (desvio medio em m)", slotSum / std::max(1, slotSamples)),
               slotSum / std::max(1, slotSamples) < 8.0f && worstSlot < 60.0f);
    test.check(withValue("NPCs: o bando migratorio atravessa a ilha (m)", farthest), farthest > 1200.0f);
    test.check(withValue("NPCs: o bando migratorio nao sai do mapa (m do centro)", edge), edge < World::kWarningRadius);
}

// Varios jogadores com carga cheia voando por todo canto: so o cacador derruba, ninguem atravessa nada.
void testWholeIsland(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    CashewField field(world, kSeed);
    SpyAuthority authority;
    const auto* hunter = firstOf(ecosystem, NpcRole::Hunter);
    const auto& trees = world.trees();
    int penetrations = countPenetrations(ecosystem, world), drops = 0, strayDrops = 0, otherEvents = 0;
    for (int frame = 0; frame < 60 * 180; ++frame) {
        const float t = frame * kFrame;
        std::vector<PlayerPublicState> players;
        // Um deles passa sempre perto do cacador, para haver ataques de verdade.
        players.push_back(playerAt(4, hunter->home + glm::vec3(70.0f * std::cos(t * 0.25f), -10, 70.0f * std::sin(t * 0.25f)),
                                   glm::vec3(-std::sin(t * 0.25f), 0, std::cos(t * 0.25f)) * 17.0f, 3));
        for (PlayerId id = 0; id < 4; ++id) {
            const auto& tree = trees[(id * 5 + static_cast<int>(t / 20.0f)) % trees.size()];
            const float angle = t * 0.4f + id * 1.6f;
            const glm::vec3 position = tree.base() + glm::vec3(std::cos(angle) * (tree.reach() + 25.0f), tree.height() * (0.5f + 0.15f * id),
                                                               std::sin(angle) * (tree.reach() + 25.0f));
            players.push_back(playerAt(id, position, glm::vec3(-std::sin(angle), 0, std::cos(angle)) * 18.0f, 3));
        }
        ecosystem.advance(kFrame, players, field, authority);
        for (const auto& event : authority.pollEvents()) {
            if (event.type == GameEventType::CashewDropped) {
                ++drops;
                if (glm::length(event.position - hunter->position) > 12.0f) ++strayDrops;
            } else if (event.type != GameEventType::CashewCollected) {
                ++otherEvents;
            }
        }
        penetrations += countPenetrations(ecosystem, world);
    }
    test.check(withValue("NPCs: em 3 minutos com 5 jogadores, nenhum NPC entra em copa, chao ou mar (casos)",
                         static_cast<float>(penetrations)), penetrations == 0);
    test.check(withValue("NPCs: so o cacador derruba carga (quedas longe dele)", static_cast<float>(strayDrops)),
               drops > 0 && strayDrops == 0 && otherEvents == 0);
}

// A formula antiga: asa fixa no planeio e no mergulho, seno so batendo.
float oldWingAngle(FlightMode mode, float time, float beatRate, uint32_t seed) {
    if (mode == FlightMode::Flap) return 0.08f + 0.78f * std::sin(time * beatRate + static_cast<float>(seed % 31));
    return mode == FlightMode::Dive ? 0.42f : 0.08f;
}

// O cacador atacando por 20 s: a asa precisa mexer em toda janela de 1 s e nunca saltar.
void testHunterAnimation(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    NoCashews cashews;
    SpyAuthority authority;
    const NpcBird* hunter = firstOf(ecosystem, NpcRole::Hunter);
    const float beatRate = 24.0f / std::sqrt(buildBirdFlightParts(hunter->seed).wingspan);
    BirdMotion motion;
    PlayerPublicState prey = playerAt(0, hunter->position + glm::vec3(90, -35, -60), glm::vec3(-6, 0, -18), 3);
    float worstWindow = 1e9f, oldWorstWindow = 1e9f, biggestJump = 0;
    float low = 1e9f, high = -1e9f, oldLow = 1e9f, oldHigh = -1e9f;
    WingPose last;
    std::set<FlightMode> modes;
    for (int frame = 0; frame < 60 * 20; ++frame) {
        const float time = frame * kFrame;
        prey.position += prey.velocity * kFrame;
        ecosystem.advance(kFrame, {prey}, cashews, authority);
        authority.pollEvents();
        modes.insert(hunter->mode);
        BirdInstance bird{hunter->seed, hunter->position, hunter->yaw, hunter->pitch, hunter->roll, kBirdScale, hunter->mode};
        bird.key = 8;
        bird.glideFlaps = npcGlideFlaps(NpcRole::Hunter);
        const WingPose pose = motion.advance(bird, beatRate, time);
        if (frame > 0) biggestJump = std::max({biggestJump, std::abs(pose.lift - last.lift), 3.0f * std::abs(pose.spread - last.spread)});
        last = pose;
        const float old = oldWingAngle(hunter->mode, time, beatRate, hunter->seed);
        low = std::min(low, pose.lift);
        high = std::max(high, pose.lift);
        oldLow = std::min(oldLow, old);
        oldHigh = std::max(oldHigh, old);
        if (frame % 60 == 59) {
            worstWindow = std::min(worstWindow, high - low);
            oldWorstWindow = std::min(oldWorstWindow, oldHigh - oldLow);
            low = oldLow = 1e9f;
            high = oldHigh = -1e9f;
        }
    }
    test.check(withValue("NPCs: com a formula antiga a asa do cacador ficava parada por 1 s (menor variacao em rad)", oldWorstWindow),
               oldWorstWindow < 0.01f);
    test.check(withValue("NPCs: a asa do cacador muda em toda janela de 1 s (menor variacao em rad)", worstWindow),
               worstWindow > 0.05f && modes.size() >= 2);
    test.check(withValue("NPCs: a asa do cacador nunca salta entre quadros (maior passo em rad)", biggestJump), biggestJump < 0.3f);
}

// Todo papel em voo mexe as asas a cada segundo; pousado, recolhe e fica quieto.
void testAllAnimations(TestRun& test, const World& world) {
    NpcEcosystem ecosystem(world, kSeed);
    CashewField field(world, kSeed);
    SpyAuthority authority;
    BirdMotion motion;
    const auto& birds = ecosystem.birds();
    std::vector<float> beat, low(birds.size(), 1e9f), high(birds.size(), -1e9f), worst(birds.size(), 1e9f);
    std::vector<bool> rested(birds.size(), false);
    for (const auto& bird : birds) beat.push_back(24.0f / std::sqrt(buildBirdFlightParts(bird.seed).wingspan));
    bool folded = true;
    for (int frame = 0; frame < 60 * 20; ++frame) {
        const float time = frame * kFrame;
        const PlayerPublicState player = playerAt(0, world.spawnPoint() + glm::vec3(30.0f * std::sin(time * 0.3f), 0, -40.0f * time),
                                                  glm::vec3(0, 0, -40), 1);
        ecosystem.advance(kFrame, {player}, field, authority);
        authority.pollEvents();
        for (size_t i = 0; i < birds.size(); ++i) {
            BirdInstance bird{birds[i].seed, birds[i].position, birds[i].yaw, birds[i].pitch, birds[i].roll, kBirdScale, birds[i].mode};
            bird.key = static_cast<uint32_t>(i);
            bird.perched = birds[i].perched;
            bird.glideFlaps = npcGlideFlaps(birds[i].role);
            const WingPose pose = motion.advance(bird, beat[i], time);
            if (birds[i].perched) {
                folded &= frame < 60 || pose.spread < 0.5f;
                rested[i] = true;   // Janela com pouso nao conta como asa parada.
            }
            low[i] = std::min(low[i], pose.lift);
            high[i] = std::max(high[i], pose.lift);
        }
        if (frame % 60 == 59) {
            for (size_t i = 0; i < birds.size(); ++i) {
                if (!rested[i]) worst[i] = std::min(worst[i], high[i] - low[i]);
                low[i] = 1e9f;
                high[i] = -1e9f;
                rested[i] = false;
            }
        }
    }
    float stillest = *std::min_element(worst.begin(), worst.end());
    test.check(withValue("NPCs: todos os papeis mexem as asas em voo (menor variacao em 1 s, rad)", stillest), stillest > 0.03f);
    test.check("NPCs: pousado, o passaro recolhe as asas", folded);
}
}

int runNpcSelfTest() {
    TestRun test;
    const World world(kSeed);
    const NpcEcosystem ecosystem(world, kSeed);
    testRosterAndLooks(test, world, ecosystem);
    testOriginalRoles(test, world);
    testCurious(test, world);
    testSkittish(test, world);
    testGlider(test, world);
    testTerritorial(test, world);
    testMigrants(test, world);
    testWholeIsland(test, world);
    testHunterAnimation(test, world);
    testAllAnimations(test, world);
    return test.failures;
}

#include "ai/NpcEcosystem.h"
#include "shared/BirdFactory.h"
#include "world/Rng.h"
#include "world/World.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace {
constexpr float kStep = 1.0f / 30.0f;
constexpr float kFruitRange = 240.0f, kPickupRange = 8.0f;
constexpr float kHuntRange = 210.0f, kHitRange = 7.0f;
constexpr uint8_t kHuntCargo = 2;
constexpr float kPi = 3.14159265f;

constexpr float kCuriousRange = 140.0f, kCuriousGap = 7.0f;
constexpr float kSkittishAlarm = 60.0f, kSkittishCalm = 75.0f;
constexpr float kGuardMargin = 70.0f, kGuardStandoff = 9.0f;
constexpr float kRouteRadius = 900.0f, kEdgeRadius = 980.0f;
constexpr float kMinLook = 0.42f;   // Distancia visual minima entre dois NPCs.

// Ordem fixa dos papeis: os 9 primeiros sao o ecossistema original.
constexpr std::array<NpcRole, NpcEcosystem::kCount> kRoster = {
    NpcRole::Flock, NpcRole::Flock, NpcRole::Flock, NpcRole::Flock, NpcRole::Flock, NpcRole::Flock,
    NpcRole::Forager, NpcRole::Forager, NpcRole::Hunter,
    NpcRole::Curious, NpcRole::Curious,
    NpcRole::Skittish, NpcRole::Skittish, NpcRole::Skittish,
    NpcRole::Glider, NpcRole::Glider,
    NpcRole::Territorial,
    NpcRole::Migrant, NpcRole::Migrant, NpcRole::Migrant, NpcRole::Migrant, NpcRole::Migrant};

// Porte de cada papel, pelo comprimento do corpo gerado (0,95 a 2,5 no modelo).
struct SizeClass { float shortest, longest, minSpan; };
SizeClass sizeOf(NpcRole role) {
    switch (role) {
        case NpcRole::Flock: return {0.95f, 1.40f, 0.0f};
        case NpcRole::Skittish: return {0.95f, 1.50f, 0.0f};
        case NpcRole::Curious: return {1.00f, 1.70f, 0.0f};
        case NpcRole::Forager: return {1.40f, 1.90f, 0.0f};
        case NpcRole::Territorial: return {1.50f, 2.05f, 0.0f};
        case NpcRole::Migrant: return {1.45f, 2.10f, 1.20f};
        case NpcRole::Glider: return {1.95f, 2.50f, 1.35f};
        case NpcRole::Hunter: return {2.10f, 2.50f, 1.10f};
        case NpcRole::Count: break;
    }
    return {0.95f, 2.5f, 0.0f};
}

float lengthSquared(glm::vec3 value) { return glm::dot(value, value); }

glm::vec3 direction(glm::vec3 value) {
    const float distance = glm::length(value);
    return distance > 0.001f ? value / distance : glm::vec3(0);
}

glm::vec3 flat(glm::vec3 value) { return {value.x, 0, value.z}; }

float flatDistance(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

// Frente do jogador pela velocidade, ou pela guinada quando ele esta quase parado.
glm::vec3 forwardOf(const PlayerPublicState& player) {
    const glm::vec3 moving = direction(flat(player.velocity));
    if (lengthSquared(flat(player.velocity)) > 4.0f) return moving;
    return {std::sin(player.yaw), 0, -std::cos(player.yaw)};
}

// Contorna um centro no plano, puxando para o raio e para a altura pedidos.
glm::vec3 orbit(glm::vec3 position, glm::vec3 center, float radius, float height) {
    const glm::vec3 offset = flat(position - center);
    const glm::vec3 radial = direction(offset);
    const glm::vec3 tangent(radial.z, 0, -radial.x);
    return tangent + radial * ((radius - glm::length(offset)) / radius)
           + glm::vec3(0, std::clamp((height - position.y) / 25.0f, -1.0f, 1.0f), 0);
}

glm::vec3 patrol(const NpcBird& bird, float radius) {
    const glm::vec3 offset = bird.position - bird.home;
    const glm::vec3 radial = direction(glm::vec3(offset.x, 0, offset.z));
    const glm::vec3 tangent(radial.z, 0, -radial.x);
    return tangent + radial * ((radius - glm::length(glm::vec2(offset.x, offset.z))) / radius)
           - glm::vec3(0, offset.y / radius, 0);
}

// Separacao, alinhamento e coesao usam as posicoes do mesmo instante.
glm::vec3 flock(const NpcBird& bird, const std::vector<NpcBird>& previous) {
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

const PlayerPublicState* nearestPlayer(glm::vec3 position, const std::vector<PlayerPublicState>& players,
                                       float range, uint8_t minimumCargo = 0) {
    const PlayerPublicState* best = nullptr;
    float bestDistance = range * range;
    for (const auto& player : players) {
        if (player.id == kNoPlayer || player.carried < minimumCargo) continue;
        const float distance = lengthSquared(player.position - position);
        if (distance < bestDistance) {
            best = &player;
            bestDistance = distance;
        }
    }
    return best;
}

const PlayerPublicState* findPlayer(PlayerId id, const std::vector<PlayerPublicState>& players) {
    for (const auto& player : players)
        if (player.id == id && id != kNoPlayer) return &player;
    return nullptr;
}

glm::vec3 forage(NpcBird& bird, const CashewQuery& cashews, GameAuthority& authority) {
    bird.stage = 0;
    if (bird.cooldown == 0) {
        if (const auto id = cashews.nearest(bird.position, kFruitRange)) {
            const auto fruit = cashews.positionOf(*id);
            const auto offset = fruit - bird.position;
            bird.stage = 1;
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
    // A borda do mundo puxa de volta para a ilha.
    const float edge = glm::length(glm::vec2(bird.position.x, bird.position.z));
    if (edge > kEdgeRadius) course += direction(-flat(bird.position)) * ((edge - kEdgeRadius) / 40.0f);
    glm::vec3 probe = bird.position + direction(course) * (speed * 0.3f);
    const auto ahead = world.resolve(probe, bird.radius + 2.0f);
    if (ahead.hit()) {
        // Desliza pela superficie em vez de empurrar contra ela: tira a parte do rumo que entra no obstaculo.
        glm::vec3 heading = direction(course);
        const float into = glm::dot(heading, ahead.normal);
        if (into < 0) heading -= ahead.normal * (into * 1.3f);
        course = heading + ahead.normal * 0.6f + glm::vec3(0, 0.7f, 0);
    }
    const glm::vec3 desired = direction(course) * speed;
    const glm::vec3 current = direction(bird.velocity);
    const glm::vec3 turn = glm::cross(current, direction(desired));
    bird.velocity = glm::mix(bird.velocity, desired, 1.8f * kStep);
    bird.position += bird.velocity * kStep;

    // Duas passadas: sair de uma copa nunca deixa o passaro dentro do chao ou de outra.
    for (int pass = 0; pass < 2; ++pass) {
        const auto contact = world.resolve(bird.position, bird.radius);
        if (contact.hit())
            bird.velocity -= contact.normal * std::min(0.0f, glm::dot(bird.velocity, contact.normal));
    }
    bird.perched = false;
    if (lengthSquared(bird.velocity) < 0.01f) return;
    bird.yaw = std::atan2(bird.velocity.x, -bird.velocity.z);
    bird.pitch = std::atan2(bird.velocity.y, glm::length(glm::vec2(bird.velocity.x, bird.velocity.z)));
    bird.roll = glm::mix(bird.roll, std::clamp(-turn.y, -0.65f, 0.65f), 4.0f * kStep);
}

float personal(uint32_t seed) { return static_cast<float>(seed % 1000u) / 1000.0f; }
}

const char* npcRoleName(NpcRole role) {
    switch (role) {
        case NpcRole::Flock: return "bando";
        case NpcRole::Forager: return "coletor";
        case NpcRole::Hunter: return "cacador";
        case NpcRole::Curious: return "curioso";
        case NpcRole::Skittish: return "assustado";
        case NpcRole::Glider: return "planador";
        case NpcRole::Territorial: return "territorial";
        case NpcRole::Migrant: return "migratorio";
        case NpcRole::Count: break;
    }
    return "?";
}

float npcGlideFlaps(NpcRole role) {
    switch (role) {
        case NpcRole::Glider: return 0.03f;
        case NpcRole::Hunter: return 0.30f;
        case NpcRole::Forager: case NpcRole::Territorial: return 0.40f;
        case NpcRole::Migrant: return 0.35f;
        default: return 0.55f;
    }
}

net::NpcPublicState npcPublicState(const NpcBird& bird) {
    return {bird.seed, bird.position, bird.yaw, bird.pitch, bird.roll, bird.mode,
            static_cast<uint8_t>(bird.role), bird.perched};
}

glm::vec3 NpcEcosystem::perchOn(size_t tree, float radius) const {
    const auto& canopy = world_.trees()[tree].canopy();
    const auto top = std::max_element(canopy.begin(), canopy.end(), [](const SphereCollider& a, const SphereCollider& b) {
        return a.center.y + a.radius * CashewTree::kCanopySquash < b.center.y + b.radius * CashewTree::kCanopySquash;
    });
    // As folhas desenhadas passam um pouco do colisor: o pouso fica por cima das duas.
    glm::vec3 perch = top->center + glm::vec3(0, 1.06f * top->radius * CashewTree::kCanopySquash + 0.3f * radius, 0);
    for (int pass = 0; pass < 2; ++pass) world_.resolve(perch, radius);
    return perch;
}

float NpcEcosystem::cruiseHeight(glm::vec2 from, glm::vec2 to) const {
    float highest = 0;
    for (int i = 0; i <= 24; ++i) {
        const glm::vec2 at = from + (to - from) * (static_cast<float>(i) / 24.0f);
        highest = std::max(highest, world_.island().heightAt(at.x, at.y));
    }
    for (const auto& tree : world_.trees()) {
        const glm::vec2 base(tree.base().x, tree.base().z);
        const glm::vec2 line = to - from;
        const float t = std::clamp(glm::dot(base - from, line) / std::max(glm::dot(line, line), 1.0f), 0.0f, 1.0f);
        if (glm::length(from + line * t - base) > tree.reach() + 120.0f) continue;
        for (const auto& sphere : tree.canopy())
            highest = std::max(highest, sphere.center.y + sphere.radius * CashewTree::kCanopySquash * 1.12f);
    }
    return highest + 45.0f;
}

NpcEcosystem::NpcEcosystem(const World& world, uint32_t seed) : world_(world) {
    Rng rng(seed ^ 0x9ddd466bu);
    const auto spawn = world.spawnPoint();

    // Arvores mais perto do inicio: 6 pousos (2 por assustado) e a do territorial.
    std::vector<size_t> byDistance(world.trees().size());
    std::iota(byDistance.begin(), byDistance.end(), size_t{0});
    std::sort(byDistance.begin(), byDistance.end(), [&](size_t a, size_t b) {
        return flatDistance(world.trees()[a].base(), spawn) < flatDistance(world.trees()[b].base(), spawn);
    });
    perchTrees_.assign(byDistance.begin(), byDistance.begin() + std::min<size_t>(6, byDistance.size()));
    guardedTree_ = byDistance[std::min<size_t>(6, byDistance.size() - 1)];
    // Termicas sobre quatro arvores espalhadas: o chao aquecido sobe em coluna.
    for (size_t k = 0; k < 4 && k < world.trees().size(); ++k) {
        const auto& tree = world.trees()[(k * 7 + 3) % world.trees().size()];
        const glm::vec2 c(tree.base().x, tree.base().z);
        thermals_.push_back({c.x, cruiseHeight(c - glm::vec2(70, 0), c + glm::vec2(70, 0)) + 25.0f, c.y});
    }

    // Sementes por papel: porte certo e nunca parecidas com as ja escolhidas.
    std::vector<BirdTraits> chosen;
    birds_.resize(kCount);
    const float routeAngle = rng.range(0.0f, 2.0f * kPi);
    for (size_t i = 0; i < kCount; ++i) {
        const NpcRole role = kRoster[i];
        const SizeClass size = sizeOf(role);
        uint32_t model = 0;
        BirdTraits traits{};
        for (int attempt = 0; attempt < 20000; ++attempt) {
            model = static_cast<uint32_t>(rng.next() >> 32);
            traits = describeBird(model);
            if (traits.length < size.shortest || traits.length > size.longest || traits.span < size.minSpan * traits.length)
                continue;
            const float look = attempt < 15000 ? kMinLook : 0.25f;
            if (std::all_of(chosen.begin(), chosen.end(), [&](const BirdTraits& other) {
                    return birdLookDistance(traits, other) >= look; }))
                break;
        }
        chosen.push_back(traits);

        NpcBird& bird = birds_[i];
        bird.role = role;
        bird.seed = model;
        bird.radius = birdCollisionRadius(traits);
        const glm::vec3 velocity(15, 0, -10);
        bird.velocity = velocity;
        bird.yaw = std::atan2(velocity.x, -velocity.z);
        switch (role) {
            case NpcRole::Flock: bird.home = spawn + glm::vec3(-40, 9, -125); break;
            case NpcRole::Forager: bird.home = spawn + glm::vec3(37, -20, -140 - 50 * static_cast<float>(i - 6)); break;
            case NpcRole::Hunter: bird.home = spawn + glm::vec3(72, 18, -155); break;
            case NpcRole::Curious: bird.home = spawn + glm::vec3(i % 2 ? -70.0f : 60.0f, -5, -60); bird.cooldown = 4.0f; break;
            case NpcRole::Skittish: bird.anchor = static_cast<int>(2 * (i - 11)); break;
            case NpcRole::Glider: bird.anchor = static_cast<int>((i - 14) * 2); break;
            case NpcRole::Territorial: bird.home = world.trees()[guardedTree_].base(); break;
            case NpcRole::Migrant: bird.slot = static_cast<int>(i - 17); bird.anchor = 0; break;
            case NpcRole::Count: break;
        }
        if (role == NpcRole::Skittish) {
            bird.position = bird.home = perchOn(perchTrees_[bird.anchor % perchTrees_.size()], bird.radius);
            bird.perched = true;
            bird.velocity = glm::vec3(0);
            bird.yaw = rng.range(-kPi, kPi);
            continue;
        }
        if (role == NpcRole::Glider && !thermals_.empty()) {
            const glm::vec3 thermal = thermals_[bird.anchor % thermals_.size()];
            bird.home = thermal;
            bird.timer = thermal.y + rng.range(0.0f, 60.0f);   // Altura que o planador quer alcancar.
            bird.position = thermal + glm::vec3(55, bird.timer - thermal.y, 0);
        } else if (role == NpcRole::Migrant) {
            // A rota cruza a ilha passando perto do centro; o bando comeca no primeiro terco dela.
            const glm::vec3 from(std::cos(routeAngle) * kRouteRadius, 0, std::sin(routeAngle) * kRouteRadius);
            bird.home = -from;   // Destino do lider.
            const glm::vec3 forward = direction(-from);
            const glm::vec3 right(-forward.z, 0, forward.x);
            const float rank = static_cast<float>((bird.slot + 1) / 2), side = bird.slot % 2 ? 1.0f : -1.0f;
            const float height = cruiseHeight(glm::vec2(from.x, from.z), glm::vec2(-from.x, -from.z));
            bird.position = from * 0.4f + glm::vec3(0, height, 0) - forward * rank * 10.0f + right * side * rank * 8.5f;
            bird.velocity = forward * 22.0f;
            bird.timer = height;
            bird.yaw = std::atan2(forward.x, -forward.z);
        } else if (role == NpcRole::Territorial) {
            const auto& tree = world.trees()[guardedTree_];
            bird.position = perchOn(guardedTree_, bird.radius) + glm::vec3(tree.reach() * 0.55f, 14.0f, 0);
        } else {
            bird.position = bird.home + glm::vec3(rng.range(-16.0f, 16.0f), rng.range(-5.0f, 5.0f), rng.range(-13.0f, 13.0f));
        }
        for (int pass = 0; pass < 2; ++pass) world_.resolve(bird.position, bird.radius);
    }
}

std::vector<net::NpcPublicState> NpcEcosystem::publicStates() const {
    std::vector<net::NpcPublicState> states;
    states.reserve(birds_.size());
    for (const NpcBird& bird : birds_) states.push_back(npcPublicState(bird));
    return states;
}

void NpcEcosystem::advance(float elapsed, const std::vector<PlayerPublicState>& players,
                          const CashewQuery& cashews, GameAuthority& authority) {
    accumulator_ += std::clamp(elapsed, 0.0f, 0.1f);
    while (accumulator_ >= kStep) {
        step(players, cashews, authority);
        accumulator_ -= kStep;
    }
}

// Pousado no alto da copa; alguem chegando perto faz o passaro sair voando para o lado oposto.
void NpcEcosystem::stepSkittish(NpcBird& bird, const std::vector<PlayerPublicState>& players) {
    enum { Perched, Fleeing, Returning, Landing };
    const glm::vec3 perch = perchOn(perchTrees_[bird.anchor % perchTrees_.size()], bird.radius);
    const PlayerPublicState* threat = nearestPlayer(bird.position, players, kSkittishAlarm);
    if (threat && bird.stage != Fleeing) {
        bird.stage = Fleeing;
        bird.timer = 7.0f + 3.0f * personal(bird.seed);
        bird.target = threat->id;
        const glm::vec3 away = direction(flat(bird.position - threat->position));
        bird.velocity = away * 9.0f + glm::vec3(0, 11.0f, 0);   // Arranque para cima, longe da copa.
    }
    switch (bird.stage) {
        case Perched:
            bird.position = perch;
            bird.velocity = glm::vec3(0);
            bird.perched = true;
            bird.pitch = 0;
            bird.roll = 0;
            bird.mode = FlightMode::Glide;
            return;
        case Fleeing: {
            const PlayerPublicState* from = findPlayer(bird.target, players);
            glm::vec3 away = from ? direction(flat(bird.position - from->position)) : direction(flat(bird.velocity));
            bird.mode = FlightMode::Flap;
            fly(bird, world_, away + glm::vec3(0, 0.45f, 0), 28.0f);
            if ((bird.timer -= kStep) <= 0) {
                bird.stage = Returning;
                bird.anchor = (bird.anchor % 2 == 0) ? bird.anchor + 1 : bird.anchor - 1;   // Troca de copa.
            }
            return;
        }
        case Returning: {
            const bool calm = nearestPlayer(perch, players, kSkittishCalm) == nullptr;
            const glm::vec3 above = perch + glm::vec3(0, 14.0f, 0);
            glm::vec3 course = calm ? above - bird.position : orbit(bird.position, perch, 45.0f, perch.y + 28.0f);
            bird.mode = course.y > 2.0f ? FlightMode::Flap : FlightMode::Glide;
            fly(bird, world_, course, 22.0f);
            if (calm && flatDistance(bird.position, perch) < 8.0f && std::abs(bird.position.y - above.y) < 10.0f)
                bird.stage = Landing;
            return;
        }
        default: {
            const glm::vec3 offset = perch - bird.position;
            const float distance = glm::length(offset);
            bird.mode = FlightMode::Glide;
            if (distance < 1.2f) {
                bird.stage = Perched;
                bird.position = perch;
                bird.velocity = glm::vec3(0);
                bird.perched = true;
                return;
            }
            fly(bird, world_, offset, std::clamp(distance * 1.5f, 3.0f, 14.0f));
            return;
        }
    }
}

// Sobe em espiral dentro da termica e depois plana ate a proxima, perdendo altura devagar.
void NpcEcosystem::stepGlider(NpcBird& bird) {
    enum { Climbing, Transit };
    bird.mode = FlightMode::Glide;
    if (thermals_.empty()) {
        fly(bird, world_, patrol(bird, 80.0f), 17.0f);
        return;
    }
    const glm::vec3 thermal = thermals_[bird.anchor % thermals_.size()];
    if (bird.stage == Climbing) {
        bird.timer = std::min(bird.timer + 1.3f * kStep, thermal.y + 120.0f);
        fly(bird, world_, orbit(bird.position, thermal, 55.0f, bird.timer), 17.0f);
        if (bird.timer >= thermal.y + 120.0f) {
            bird.stage = Transit;
            bird.anchor = (bird.anchor + 1) % static_cast<int>(thermals_.size());
        }
        return;
    }
    // Planando entre termicas nunca desce abaixo da altura segura da rota.
    const float floor = std::max(thermal.y, cruiseHeight(glm::vec2(bird.position.x, bird.position.z),
                                                         glm::vec2(thermal.x, thermal.z)));
    bird.timer = std::max(bird.timer - 1.0f * kStep, floor);
    const glm::vec3 toward = direction(flat(thermal - bird.position));
    fly(bird, world_, toward + glm::vec3(0, std::clamp((bird.timer - bird.position.y) / 20.0f, -0.6f, 1.0f), 0), 21.0f);
    if (flatDistance(bird.position, thermal) < 70.0f) bird.stage = Climbing;
}

// Ronda a propria arvore e persegue quem entra na area dela, sempre a alguns metros de distancia.
void NpcEcosystem::stepTerritorial(NpcBird& bird, const std::vector<PlayerPublicState>& players) {
    enum { Patrol, Chase, Return };
    const auto& tree = world_.trees()[guardedTree_];
    const glm::vec3 base = tree.base();
    // A ronda e por cima da propria copa: os vizinhos tem copas largas que invadem qualquer volta por fora.
    const glm::vec3 crown = perchOn(guardedTree_, bird.radius) + glm::vec3(0, 14.0f, 0);
    const float guard = tree.reach() + kGuardMargin, ring = tree.reach() * 0.55f;
    const float roof = crown.y + 90.0f;
    const auto intruder = [&]() -> const PlayerPublicState* {
        const PlayerPublicState* best = nullptr;
        float bestDistance = guard;
        for (const auto& player : players) {
            if (player.id == kNoPlayer || player.position.y > roof) continue;
            const float distance = flatDistance(player.position, base);
            if (distance < bestDistance) { best = &player; bestDistance = distance; }
        }
        return best;
    };
    if (bird.stage == Patrol) {
        if (const PlayerPublicState* player = bird.cooldown == 0 ? intruder() : nullptr) {
            bird.stage = Chase;
            bird.target = player->id;
            bird.timer = 10.0f;
        }
    }
    if (bird.stage == Chase) {
        const PlayerPublicState* player = findPlayer(bird.target, players);
        bird.timer -= kStep;
        if (!player || bird.timer <= 0 || flatDistance(player->position, base) > guard * 1.5f) {
            bird.stage = Return;
            bird.cooldown = 5.0f;
        } else {
            // Rodeia o intruso a poucos metros, cobrando a saida, mas sem encostar.
            const glm::vec3 offset = bird.position - player->position;
            const glm::vec3 around = glm::cross(direction(offset), glm::vec3(0, 1, 0));
            const glm::vec3 standoff = player->position + direction(offset) * kGuardStandoff;
            const glm::vec3 desired = (standoff - bird.position) * 1.2f + player->velocity + around * 10.0f;
            bird.mode = FlightMode::Flap;
            fly(bird, world_, desired, std::clamp(glm::length(desired), 12.0f, 38.0f));
            return;
        }
    }
    if (bird.stage == Return && flatDistance(bird.position, base) < ring + 25.0f) bird.stage = Patrol;
    bird.mode = bird.stage == Return ? FlightMode::Flap : FlightMode::Glide;
    fly(bird, world_, orbit(bird.position, crown, ring, crown.y), bird.stage == Return ? 26.0f : 19.0f);
}

// Acompanha um jogador de perto, ao lado e um pouco atras, e depois de alguns segundos vai embora.
void NpcEcosystem::stepCurious(NpcBird& bird, const std::vector<PlayerPublicState>& players) {
    enum { Wander, Follow, Leave };
    if (bird.stage == Wander) {
        if (const PlayerPublicState* player = bird.cooldown == 0 ? nearestPlayer(bird.position, players, kCuriousRange) : nullptr) {
            bird.stage = Follow;
            bird.target = player->id;
            bird.timer = 6.0f + 3.0f * personal(bird.seed);
        } else {
            bird.mode = FlightMode::Glide;
            fly(bird, world_, patrol(bird, 60.0f), 20.0f);
            return;
        }
    }
    const PlayerPublicState* player = findPlayer(bird.target, players);
    if (bird.stage == Follow) {
        bird.timer -= kStep;
        if (!player || bird.timer <= 0) {
            bird.stage = Leave;
            bird.timer = 8.0f;
            bird.cooldown = 15.0f;
        } else {
            const glm::vec3 forward = forwardOf(*player), right(-forward.z, 0, forward.x);
            const float side = bird.seed % 2 ? 1.0f : -1.0f;
            const glm::vec3 escort = player->position - forward * 9.0f + right * (side * 7.0f) + glm::vec3(0, 2, 0);
            glm::vec3 desired = player->velocity + (escort - bird.position) * 0.9f;
            const glm::vec3 gap = bird.position - player->position;
            if (glm::length(gap) < kCuriousGap) desired += direction(gap) * 20.0f;   // Nunca encosta no jogador.
            const float speed = std::clamp(glm::length(desired), 10.0f, 48.0f);
            bird.mode = speed > 22.0f || desired.y > 3.0f ? FlightMode::Flap : FlightMode::Glide;
            fly(bird, world_, desired, speed);
            return;
        }
    }
    const glm::vec3 away = player ? direction(flat(bird.position - player->position)) : direction(flat(bird.velocity));
    bird.mode = FlightMode::Flap;
    fly(bird, world_, away + glm::vec3(0, 0.35f, 0), 26.0f);
    if ((bird.timer -= kStep) <= 0) bird.stage = Wander;
}

// O lider cruza a ilha de ponta a ponta; os outros seguram as posicoes do V atras dele.
void NpcEcosystem::stepMigrant(NpcBird& bird, const NpcBird* leader) {
    bird.mode = FlightMode::Flap;
    if (!leader || leader == &bird) {
        if (flatDistance(bird.position, bird.home) < 60.0f) {
            // Nova travessia: volta pelo outro lado, girando a rota um pouco a cada vez.
            bird.anchor++;
            const float turn = (static_cast<float>(bird.anchor) * 0.618034f - std::floor(static_cast<float>(bird.anchor) * 0.618034f) - 0.5f) * 1.2f;
            const float angle = std::atan2(bird.position.z, bird.position.x) + kPi + turn;
            bird.home = glm::vec3(std::cos(angle) * kRouteRadius, 0, std::sin(angle) * kRouteRadius);
            bird.timer = cruiseHeight(glm::vec2(bird.position.x, bird.position.z), glm::vec2(bird.home.x, bird.home.z));
        }
        const glm::vec3 toward = direction(flat(bird.home - bird.position));
        fly(bird, world_, toward + glm::vec3(0, std::clamp((bird.timer - bird.position.y) / 20.0f, -0.5f, 1.0f), 0), 22.0f);
        return;
    }
    const glm::vec3 forward = direction(flat(leader->velocity));
    const glm::vec3 right(-forward.z, 0, forward.x);
    const float rank = static_cast<float>((bird.slot + 1) / 2), side = bird.slot % 2 ? 1.0f : -1.0f;
    const glm::vec3 place = leader->position - forward * (rank * 10.0f) + right * (side * rank * 8.5f) + glm::vec3(0, rank * 0.8f, 0);
    const glm::vec3 desired = leader->velocity + (place - bird.position) * 0.8f;
    fly(bird, world_, desired, std::clamp(glm::length(desired), 8.0f, 40.0f));
}

void NpcEcosystem::step(const std::vector<PlayerPublicState>& players,
                       const CashewQuery& cashews, GameAuthority& authority) {
    const auto previous = birds_;
    const NpcBird* leader = nullptr;
    for (const auto& bird : birds_)
        if (bird.role == NpcRole::Migrant && bird.slot == 0) leader = &bird;
    for (auto& bird : birds_) {
        bird.cooldown = std::max(0.0f, bird.cooldown - kStep);
        switch (bird.role) {
        case NpcRole::Flock:
            bird.mode = FlightMode::Flap;
            fly(bird, world_, flock(bird, previous), 20.0f);
            break;
        case NpcRole::Forager: {
            const glm::vec3 course = forage(bird, cashews, authority);
            bird.mode = bird.stage == 1 || bird.velocity.y > 1.5f ? FlightMode::Flap : FlightMode::Glide;
            fly(bird, world_, course, 23.0f);
            break;
        }
        case NpcRole::Hunter:
            if (const auto* prey = bird.cooldown == 0.0f ? nearestPlayer(bird.position, players, kHuntRange, kHuntCargo) : nullptr) {
                // Histerese: so mergulha com a presa bem abaixo e perto, e so sai do mergulho quando nivela.
                const float below = bird.position.y - prey->position.y;
                const float near = flatDistance(prey->position, bird.position);
                if (bird.mode == FlightMode::Dive) {
                    if (below < 2.0f) bird.mode = FlightMode::Flap;
                } else if (below > 8.0f && near < 3.0f * below + 20.0f) {
                    bird.mode = FlightMode::Dive;
                } else {
                    bird.mode = FlightMode::Flap;
                }
                fly(bird, world_, hunt(bird, *prey, authority), bird.mode == FlightMode::Dive ? 46.0f : 42.0f);
            } else {
                bird.mode = FlightMode::Glide;
                fly(bird, world_, patrol(bird, 95.0f), 27.0f);
            }
            break;
        case NpcRole::Curious: stepCurious(bird, players); break;
        case NpcRole::Skittish: stepSkittish(bird, players); break;
        case NpcRole::Glider: stepGlider(bird); break;
        case NpcRole::Territorial: stepTerritorial(bird, players); break;
        case NpcRole::Migrant: stepMigrant(bird, leader); break;
        case NpcRole::Count: break;
        }
    }
}

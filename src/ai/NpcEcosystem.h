#pragma once
#include "shared/CashewQuery.h"
#include "shared/GameAuthority.h"
#include <array>
#include <vector>

class World;

enum class NpcRole { Flock, Forager, Hunter };

struct NpcBird {
    NpcRole role;
    uint32_t seed;
    glm::vec3 position, velocity, home;
    float yaw = 0, pitch = 0, roll = 0;
    float cooldown = 0;
    FlightMode mode = FlightMode::Glide;
};

class NpcEcosystem {
public:
    NpcEcosystem(const World& world, uint32_t seed);

    void advance(float elapsed, const std::vector<PlayerPublicState>& players,
                 const CashewQuery& cashews, GameAuthority& authority);
    const std::array<NpcBird, 9>& birds() const { return birds_; }

private:
    void step(const std::vector<PlayerPublicState>& players,
              const CashewQuery& cashews, GameAuthority& authority);

    const World& world_;
    std::array<NpcBird, 9> birds_{};
    float accumulator_ = 0;
};

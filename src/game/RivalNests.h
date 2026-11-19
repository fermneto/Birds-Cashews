#pragma once
#include "game/NestSite.h"
#include <array>
#include <optional>
#include <vector>

// Ninho de outro jogador da sala, no lugar que todos os computadores calculam igual.
struct RivalNest {
    PlayerId owner = kNoPlayer;
    NestSite site;
    int sizeLevel = 0;
};

// Ninhos dos outros jogadores conectados. Um ninho so existe enquanto o dono esta na sala.
// Nao depende de OpenGL: o Game remonta a malha so quando revision() muda.
class RivalNests {
public:
    RivalNests(const World& world, uint32_t seed) : world_(world), seed_(seed) {}

    // Recebe a lista replicada de jogadores. Retorna true se algum ninho entrou, saiu ou mudou de tamanho.
    bool update(const std::vector<PlayerPublicState>& players, PlayerId localPlayer);

    const std::vector<RivalNest>& nests() const { return nests_; }   // Em ordem de PlayerId.
    const RivalNest* find(PlayerId owner) const;
    int revision() const { return revision_; }

private:
    const NestSite& siteOf(PlayerId owner);

    const World& world_;
    uint32_t seed_;
    std::array<std::optional<NestSite>, kMaxPlayers> sites_{};   // chooseNestSite e caro: calcula uma vez por jogador.
    std::vector<RivalNest> nests_;
    int revision_ = 0;
};

#pragma once
#include "events/SkyPlane.h"
#include "shared/CashewSpill.h"
#include "shared/GameAuthority.h"
#include <array>

// Autoridade do modo sem rede: confirma os pedidos na ordem em que chegaram. Como o host, cria os
// cajus que caem de uma carga derrubada e nunca derruba mais do que o jogador carrega.
class LocalAuthority : public GameAuthority {
public:
    void request(const GameEvent& event) override;
    std::vector<GameEvent> pollEvents() override;
    std::vector<PlayerPublicState> players() const override { return players_; }
    void publishLocalPlayer(const PlayerPublicState& state) override;

private:
    bool accept(GameEvent& event);
    void removePlayer(PlayerId id);

    std::vector<GameEvent> pending_;
    std::vector<PlayerPublicState> players_;
    std::array<int, kMaxPlayers> carried_{};   // Carga segundo os eventos confirmados.
    CashewSpill spill_;
    SkyPlaneLedger skyPlanes_;   // Sem relogio: o intervalo entre avioes fica com quem pede.
};

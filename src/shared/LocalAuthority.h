#pragma once
#include "shared/GameAuthority.h"

// Autoridade do modo sem rede: confirma todo pedido, na ordem em que chegou.
class LocalAuthority : public GameAuthority {
public:
    void request(const GameEvent& event) override;
    std::vector<GameEvent> pollEvents() override;
    std::vector<PlayerPublicState> players() const override { return players_; }
    void publishLocalPlayer(const PlayerPublicState& state) override;

private:
    void removePlayer(PlayerId id);

    std::vector<GameEvent> pending_;
    std::vector<PlayerPublicState> players_;
};

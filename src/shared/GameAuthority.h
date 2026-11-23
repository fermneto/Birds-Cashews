#pragma once
#include "shared/GameEvent.h"
#include <string>
#include <vector>

// Quem decide as regras. LocalAuthority joga sozinho; NetAuthority repassa os pedidos ao host.
class GameAuthority {
public:
    virtual ~GameAuthority() = default;
    virtual void request(const GameEvent& event) = 0;
    virtual std::vector<GameEvent> pollEvents() = 0;   // Entrega cada evento confirmado uma unica vez.
    virtual std::vector<PlayerPublicState> players() const = 0;
    virtual void publishLocalPlayer(const PlayerPublicState& state) = 0;
    // Nome do perfil de um jogador da sala, para aparecer acima do passaro. Vazio se nao conhecido.
    virtual std::string playerName(PlayerId) const { return {}; }
};

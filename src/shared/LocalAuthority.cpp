#include "shared/LocalAuthority.h"
#include <algorithm>
#include <utility>

void LocalAuthority::request(const GameEvent& event) {
    GameEvent confirmed = event;
    if (!accept(confirmed)) return;
    if (confirmed.type == GameEventType::PlayerLeft) removePlayer(confirmed.actor);
    pending_.push_back(confirmed);
    if (confirmed.type == GameEventType::CashewDropped)
        for (const GameEvent& spilled : spill_.spill(confirmed)) pending_.push_back(spilled);
}

// So acompanha a carga: as outras regras ficam com quem pede, como sempre foi no jogo sozinho.
bool LocalAuthority::accept(GameEvent& event) {
    const bool player = event.actor < kMaxPlayers;
    switch (event.type) {
        case GameEventType::CashewSpilled:
            return false;   // So a autoridade cria.
        case GameEventType::CashewCollected:
            if (isLooseCashew(event.cashew) && !spill_.take(event.cashew)) return false;
            if (player) carried_[event.actor]++;
            return true;
        case GameEventType::NestRobbed:
            if (player) carried_[event.actor]++;
            return true;
        case GameEventType::CashewDelivered:
            if (player) carried_[event.actor] = std::max(0, carried_[event.actor] - event.value);
            return true;
        case GameEventType::CashewDropped:
            if (event.target >= kMaxPlayers) return false;
            event.value = static_cast<uint8_t>(std::min<int>(event.value, carried_[event.target]));
            carried_[event.target] -= event.value;
            return event.value > 0;
        default:
            return true;
    }
}

std::vector<GameEvent> LocalAuthority::pollEvents() {
    std::vector<GameEvent> events;
    events.swap(pending_);
    return events;
}

void LocalAuthority::publishLocalPlayer(const PlayerPublicState& state) {
    const auto found = std::find_if(players_.begin(), players_.end(),
                                    [&](const PlayerPublicState& p) { return p.id == state.id; });
    if (found != players_.end()) *found = state;
    else players_.push_back(state);
}

void LocalAuthority::removePlayer(PlayerId id) {
    players_.erase(std::remove_if(players_.begin(), players_.end(),
                                  [&](const PlayerPublicState& p) { return p.id == id; }),
                   players_.end());
    if (id < kMaxPlayers) carried_[id] = 0;
}

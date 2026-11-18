#include "shared/LocalAuthority.h"
#include <algorithm>
#include <utility>

void LocalAuthority::request(const GameEvent& event) {
    if (event.type == GameEventType::PlayerLeft) removePlayer(event.actor);
    pending_.push_back(event);
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
}

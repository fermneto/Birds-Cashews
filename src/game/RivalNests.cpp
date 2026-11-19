#include "game/RivalNests.h"
#include "save/Profile.h"
#include <algorithm>
#include <utility>

bool RivalNests::update(const std::vector<PlayerPublicState>& players, PlayerId localPlayer) {
    std::array<int, kMaxPlayers> levels;
    levels.fill(-1);
    for (const PlayerPublicState& player : players)
        if (player.id < kMaxPlayers && player.id != localPlayer)
            levels[player.id] = std::min<int>(player.nestLevel, kMaxUpgradeLevel);   // O nivel vem da rede.

    std::vector<RivalNest> next;
    for (PlayerId id = 0; id < kMaxPlayers; id++)
        if (levels[id] >= 0) next.push_back({id, siteOf(id), levels[id]});

    const bool changed = next.size() != nests_.size() ||
                         !std::equal(next.begin(), next.end(), nests_.begin(), [](const RivalNest& a, const RivalNest& b) {
                             return a.owner == b.owner && a.sizeLevel == b.sizeLevel;
                         });
    if (!changed) return false;
    nests_ = std::move(next);
    revision_++;
    return true;
}

const RivalNest* RivalNests::find(PlayerId owner) const {
    for (const RivalNest& nest : nests_)
        if (nest.owner == owner) return &nest;
    return nullptr;
}

const NestSite& RivalNests::siteOf(PlayerId owner) {
    if (!sites_[owner]) sites_[owner] = chooseNestSite(world_, seed_, owner);
    return *sites_[owner];
}

#include "game/NameTags.h"
#include <algorithm>

std::vector<NameTag> visibleNameTags(glm::vec3 camera, const std::vector<PlayerPublicState>& players, PlayerId localPlayer,
                                     const std::function<std::string(PlayerId)>& nameOf, const NameTagRules& rules) {
    std::vector<NameTag> tags;
    for (const PlayerPublicState& player : players) {
        if (player.id == localPlayer || player.id >= kMaxPlayers) continue;
        const float distance = glm::length(player.position - camera);
        if (distance >= rules.hiddenFrom) continue;
        std::string name = nameOf(player.id);
        if (name.empty()) continue;
        NameTag tag;
        tag.player = player.id;
        tag.name = std::move(name);
        tag.anchor = player.position + glm::vec3(0, rules.height, 0);
        tag.distance = distance;
        const float fade = rules.hiddenFrom - rules.fullUntil;
        tag.opacity = fade > 0 ? std::clamp((rules.hiddenFrom - distance) / fade, 0.0f, 1.0f) : 1.0f;
        tags.push_back(std::move(tag));
    }
    std::sort(tags.begin(), tags.end(), [](const NameTag& a, const NameTag& b) { return a.distance > b.distance; });
    return tags;
}

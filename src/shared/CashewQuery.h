#pragma once
#include "shared/GameTypes.h"
#include <optional>

// Como os NPCs enxergam os cajus, sem depender de como eles sao guardados.
class CashewQuery {
public:
    virtual ~CashewQuery() = default;
    virtual std::optional<CashewId> nearest(glm::vec3 position, float radius) const = 0;
    virtual glm::vec3 positionOf(CashewId id) const = 0;
};

#include "shared/CashewSpill.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kSpreadRadius = 4.0f;   // Metros entre o passaro e cada caju que cai dele.
constexpr float kReleaseDrop = 3.0f;    // Os cajus saem um pouco abaixo do passaro, longe do bico.
constexpr float kSlotMargin = 2.0f;     // Folga para todo computador ja ter tirado o caju antes de reusar a vaga.
constexpr float kGoldenAngle = 2.3999632f;
}

int CashewSpill::freeSlot() const {
    int oldest = 0;
    for (int i = 0; i < kLooseCashewSlots; i++) {
        if (!slots_[i].active) return i;
        if (slots_[i].serial < slots_[oldest].serial) oldest = i;
    }
    return oldest;
}

std::vector<GameEvent> CashewSpill::spill(const GameEvent& dropped) {
    std::vector<GameEvent> spilled;
    for (int i = 0; i < dropped.value; i++) {
        const int index = freeSlot();
        const float angle = kGoldenAngle * static_cast<float>(i);
        const float reach = dropped.value > 1 ? kSpreadRadius : 0.0f;
        Slot& slot = slots_[index];
        slot.active = true;
        slot.spilledAt = clock_;
        slot.serial = ++serial_;
        slot.from = dropped.position + glm::vec3(std::cos(angle) * reach, -kReleaseDrop, std::sin(angle) * reach);

        GameEvent event;
        event.type = GameEventType::CashewSpilled;
        event.actor = dropped.actor;
        event.target = dropped.target;
        event.cashew = static_cast<CashewId>(kLooseCashewBase + index);
        event.value = 1;
        event.position = slot.from;
        spilled.push_back(event);
    }
    return spilled;
}

bool CashewSpill::take(CashewId id) {
    if (!isLooseCashew(id)) return false;
    Slot& slot = slots_[id - kLooseCashewBase];
    if (!slot.active) return false;
    slot.active = false;
    return true;
}

void CashewSpill::advance(float seconds) {
    clock_ += std::max(0.0f, seconds);
    for (Slot& slot : slots_)
        if (slot.active && clock_ - slot.spilledAt > kLooseCashewSeconds + kSlotMargin) slot.active = false;
}

std::vector<GameEvent> CashewSpill::activeSpills() const {
    std::vector<GameEvent> events;
    for (int i = 0; i < kLooseCashewSlots; i++) {
        if (!slots_[i].active) continue;
        GameEvent event;
        event.type = GameEventType::CashewSpilled;
        event.cashew = static_cast<CashewId>(kLooseCashewBase + i);
        event.value = 1;
        event.position = slots_[i].from;
        events.push_back(event);
    }
    return events;
}

int CashewSpill::activeCount() const {
    int count = 0;
    for (const Slot& slot : slots_) count += slot.active ? 1 : 0;
    return count;
}

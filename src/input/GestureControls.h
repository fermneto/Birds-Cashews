#pragma once
#include "entity/FlightControls.h"

// Traduz os gestos reconhecidos nos comandos de voo. As batidas continuam sendo contadas
// porque decolam do ninho, mesmo com o voo usando os valores continuos.
class GestureControls {
public:
    FlightControls read(const GestureState& gesture, bool newSample, double now);

private:
    uint32_t lastFlapCount_ = 0, lastMenuCount_ = 0;
    double lastSample_ = -1;
};

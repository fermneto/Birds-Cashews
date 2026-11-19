#pragma once
#include "vision/GestureRecognizer.h"
#include <cstdint>

// Comandos de voo de um quadro, vindos do corpo ou do teclado.
struct FlightControls {
    FlightMode mode = FlightMode::Glide;
    float bank = 0;
    uint32_t flaps = 0;
    float flapStrength = 0;
    ArmSide menuPressed = ArmSide::None;   // Lado confirmado neste quadro.
    ArmSide raisedArm = ArmSide::None;
    float menuProgress = 0;
    bool bodyControlled = false;           // Com o corpo, o voo usa os valores continuos abaixo.
    float dive = 0, pullUp = 0, stroke = 0;
    float leftWingHeight = 0, rightWingHeight = 0;
    float diveAmount() const { return bodyControlled ? dive : float(mode == FlightMode::Dive); }
};

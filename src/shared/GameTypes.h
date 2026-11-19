#pragma once
#include "vision/GestureRecognizer.h"
#include <glm/glm.hpp>
#include <cstdint>

using PlayerId = uint8_t;
using CashewId = uint16_t;

constexpr PlayerId kNoPlayer = 255;
constexpr CashewId kNoCashew = 65535;

// O que todos os computadores precisam saber sobre um jogador.
struct PlayerPublicState {
    PlayerId id = kNoPlayer;
    glm::vec3 position{0};
    glm::vec3 velocity{0};   // Metros por segundo: usado na interpolacao e no efeito Doppler da voz.
    float yaw = 0, pitch = 0, roll = 0;
    FlightMode mode = FlightMode::Glide;
    uint8_t carried = 0;
    bool speaking = false;
    uint32_t birdSeed = 0;
    uint8_t nestLevel = 0;
};

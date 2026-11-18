#pragma once
#include "entity/FlightModel.h"

// Pose das asas e do bico vistos em primeira pessoa.
struct BirdAnimation {
    float wingAngle = -4.0f;   // Graus: positivo levanta a asa.
    float wingSweep = 0.0f;    // Graus: positivo recolhe a asa para tras.
    float beakOpen = 0.0f;     // 0 a 1.
    float shake = 0.0f;
    float kick = 0.0f;         // Impulso visual de cada batida.
    float fov = 72.0f;         // Campo de visao, que abre com a velocidade.
};

// Liga o modelo de voo ao mundo: colisao, limites do mapa e animacao.
class PlayerBird {
public:
    static constexpr float kRadius = 1.5f;

    void spawn(glm::vec3 position, float yaw);
    void applyControls(const FlightControls& controls);
    void step(const World& world, float dt);
    void animate(float dt);

    FlightModel& flight() { return flight_; }
    const FlightModel& flight() const { return flight_; }
    const BirdAnimation& animation() const { return animation_; }
    bool nearBoundary() const { return nearBoundary_; }

private:
    void enforceBoundary(float dt);
    float targetFov() const;

    FlightModel flight_;
    FlightControls controls_;
    BirdAnimation animation_;
    float flapTimer_ = 0;
    bool nearBoundary_ = false;
};

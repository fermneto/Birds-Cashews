#pragma once
#include "entity/FlightModel.h"

// Pose das asas e do bico vistos em primeira pessoa.
struct BirdAnimation {
    float leftWingAngle = -4.0f, rightWingAngle = -4.0f;   // Graus: positivo levanta a asa.
    float wingSweep = 0.0f;    // Graus: positivo recolhe a asa para tras.
    float beakOpen = 0.0f;     // 0 a 1.
    float shake = 0.0f;
    float kick = 0.0f;         // Impulso visual de cada batida.
    float wind = 0, windPhase = 0, speedVibration = 0;
    float fov = 72.0f;         // Campo de visao, que abre com a velocidade.
};

// Motivos independentes para abrir o bico. Vale o maior pedido entre eles.
enum class BeakSource { Cashew, Voice, Count };

// Liga o modelo de voo ao mundo: colisao, limites do mapa e animacao.
class PlayerBird {
public:
    static constexpr float kRadius = 1.5f;

    void spawn(glm::vec3 position, float yaw);
    void applyControls(const FlightControls& controls);
    void step(const World& world, float dt);
    void animate(float dt);
    void setBeakTarget(BeakSource source, float openness);

    FlightModel& flight() { return flight_; }
    const FlightModel& flight() const { return flight_; }
    const BirdAnimation& animation() const { return animation_; }
    bool nearBoundary() const { return nearBoundary_; }

private:
    void enforceBoundary(float dt);
    float targetFov() const;
    void animateBeak(float dt);

    FlightModel flight_;
    FlightControls controls_;
    BirdAnimation animation_;
    float beakTargets_[static_cast<int>(BeakSource::Count)] = {};
    float flapTimer_ = 0;
    bool nearBoundary_ = false;
};

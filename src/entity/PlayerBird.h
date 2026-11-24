#pragma once
#include "entity/FlightModel.h"
#include <algorithm>

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
    void jolt(float strength) { animation_.shake = std::max(animation_.shake, strength); }   // Tremor da camera.

    FlightModel& flight() { return flight_; }
    const FlightModel& flight() const { return flight_; }
    const BirdAnimation& animation() const { return animation_; }
    bool nearBoundary() const { return nearBoundary_; }
    float takeSolidImpact();   // Maior impacto em copa ou tronco desde a ultima chamada, em m/s.
    float takeFlap();          // Forca (0 a 1) da batida de asa mais forte desde a ultima chamada; 0 sem batida.
    void shake(float amount) { animation_.shake = std::max(animation_.shake, amount); }   // Tremor da camera.

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
    float solidImpact_ = 0;
    float flapSound_ = 0, lastStroke_ = 0;
};

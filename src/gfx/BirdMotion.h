#pragma once
#include "vision/GestureRecognizer.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>

// Um passaro procedural a desenhar neste quadro.
struct BirdInstance {
    uint32_t seed;
    glm::vec3 position;
    float yaw = 0, pitch = 0, roll = 0, scale = 2.2f;
    FlightMode mode = FlightMode::Glide;
    uint32_t key = 0;          // Identidade estavel entre quadros: guarda o estado da batida de asa.
    bool perched = false;      // Pousado: asas recolhidas junto ao corpo.
    float glideFlaps = 0;      // Fracao do planeio gasta em rajadas curtas de batidas (0 = so plana).
    float flail = 0;           // 0 a 1: arremessado, as asas se debatem sem conseguir bater no ritmo.
};

// Pose das asas: angulo de elevacao, abertura (1 = aberta) e quanto a ponta recua.
struct WingPose {
    float lift = 0.08f;
    float spread = 1.0f;
    float sweep = 0.0f;
    float tilt = 0.0f;     // Diferenca entre as asas: a direita sobe tilt e a esquerda desce tilt.
};

// Anima as asas com estado proprio por passaro, para que a troca de modo nunca de salto
// e nenhum modo fique parado: planeio com flexao e rajadas, mergulho com asas recolhidas.
class BirdMotion {
public:
    WingPose advance(const BirdInstance& bird, float beatRate, float time);
    void forgetUnseen(float time);   // Descarta quem sumiu ha mais de um segundo.

private:
    struct State {
        float phase = 0, flap = 0, fold = 0, perch = 0, flail = 0, lastTime = 0;
        glm::vec3 lastPosition{0};
        bool started = false;
    };
    std::unordered_map<uint32_t, State> states_;
};

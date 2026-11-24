#pragma once
#include "events/SkyPlane.h"
#include <cstdint>
#include <vector>

// Som do aviao do teto: um motor continuo, que o SoundManager toca em loop com volume e velocidade de leitura
// (Doppler) escolhidos a cada quadro, e o estrondo da batida.

// Volume (0 a 1) e velocidade de leitura do loop do motor: acima de 1 o motor soa mais agudo.
struct PlaneEngineMix {
    float volume = 0;
    float rate = 1;
};

// O aviao mais alto que se ouve: volume pela distancia e Doppler pela velocidade relativa.
PlaneEngineMix planeEngineFor(glm::vec3 listener, glm::vec3 listenerVelocity, const std::vector<SkyPlane>& planes);
float planeImpactVolume(glm::vec3 listener, glm::vec3 impact);   // Cheio por perto, some a ~900 m.

// Ronco de motor a helice em loop sem emenda. Mono, na taxa do SoundManager.
std::vector<int16_t> synthesizePlaneEngineLoop(int sampleRate);
// Batida: pancada grave, estalos de lataria e um zunido metalico. Valores de -1 a 1, antes do volume geral.
std::vector<float> synthesizePlaneImpact(int sampleRate);

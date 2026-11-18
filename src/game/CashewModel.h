#pragma once
#include "game/CashewField.h"
#include "gfx/MeshBuilder.h"
#include <glm/glm.hpp>
#include <vector>

constexpr float kCashewDetailDistance = 220.0f;   // Alem daqui basta o modelo simples.
constexpr float kCashewDrawDistance = 1000.0f;

// Caju low-poly: pedunculo amarelo e vermelho com a castanha embaixo.
MeshBuilder buildCashewMesh();
MeshBuilder buildCashewFarMesh();

// Talos que prendem os cajus nas copas. Ficam na arvore mesmo depois da coleta.
MeshBuilder buildCashewStems(const CashewField& field);

// Transformacoes dos cajus ativos a frente da camera, com distancia em [minDistance, maxDistance).
std::vector<glm::mat4> visibleCashews(const CashewField& field, const glm::mat4& viewProjection, glm::vec3 camera,
                                      float minDistance, float maxDistance);

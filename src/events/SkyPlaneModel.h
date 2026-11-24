#pragma once
#include "events/SkyPlane.h"
#include "gfx/MeshBuilder.h"
#include <glm/glm.hpp>
#include <vector>

class Renderer;

// Aviao de helice low-poly, no estilo da ilha: fuselagem branca, faixas vermelhas e cabine azul.
// O nariz aponta para -Z, como os passaros. A malha tem cerca de 14 m
// de comprimento e 17 m de envergadura, desenhada 1,5 vez maior para aparecer de longe.
MeshBuilder buildSkyPlaneMesh();
MeshBuilder buildSkyPlanePropeller();   // Duas pas, centradas na origem, girando em torno de Z.

glm::mat4 skyPlaneTransform(const SkyPlane& plane);
glm::mat4 skyPlanePropellerTransform(const SkyPlane& plane, float time);

// Entrega ao Renderer os avioes em voo neste quadro.
void showSkyPlanes(Renderer& renderer, const std::vector<SkyPlane>& planes, float time);

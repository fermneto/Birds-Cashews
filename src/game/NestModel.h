#pragma once
#include "game/NestSite.h"
#include "gfx/MeshBuilder.h"
#include <glm/glm.hpp>

// Ninho low-poly em coordenadas do mundo: cresce com o nivel de Tamanho.
MeshBuilder buildNestMesh(const NestSite& site, int sizeLevel);

// Losango brilhante que marca o ninho de longe, centrado na origem.
MeshBuilder buildNestBeaconMesh();

// Gira o marcador e o aumenta com a distancia. Bem perto do ninho ele some.
glm::mat4 nestBeaconTransform(const NestSite& site, int sizeLevel, glm::vec3 camera, float time);

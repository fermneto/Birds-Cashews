#pragma once
#include "game/NestSite.h"
#include "game/RivalNests.h"
#include "gfx/MeshBuilder.h"
#include <glm/glm.hpp>

// Ninho low-poly em coordenadas do mundo: cresce com o nivel de Tamanho.
MeshBuilder buildNestMesh(const NestSite& site, int sizeLevel);

// Ninhos dos outros jogadores numa malha so, com bandeira azul em vez da vermelha.
MeshBuilder buildRivalNestsMesh(const std::vector<RivalNest>& nests);

// Losango brilhante que marca o ninho de longe, centrado na origem. O dos rivais e azul.
MeshBuilder buildNestBeaconMesh();
MeshBuilder buildRivalBeaconMesh();

// Gira o marcador e o aumenta com a distancia. Bem perto do ninho ele some.
glm::mat4 nestBeaconTransform(const NestSite& site, int sizeLevel, glm::vec3 camera, float time);

// Marcador menor, para o ninho de outro jogador nao competir com o proprio.
glm::mat4 rivalBeaconTransform(const NestSite& site, int sizeLevel, glm::vec3 camera, float time);

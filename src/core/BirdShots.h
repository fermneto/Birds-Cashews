#pragma once
#include "gfx/Renderer.h"
#include "world/World.h"
#include <string>

// Fotos de conferencia dos passaros: galerias lado a lado, cacador em acao, pouso e bando em V.
// Usa um ecossistema proprio para nao mexer na partida que o render-test fotografa depois.
void captureBirdShots(Renderer& renderer, const World& world, uint32_t seed, const std::string& outputDir);

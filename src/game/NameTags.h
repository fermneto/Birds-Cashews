#pragma once
#include "shared/GameTypes.h"
#include <functional>
#include <string>
#include <vector>

// Nome de outro jogador, flutuando acima do passaro dele.
struct NameTag {
    PlayerId player = kNoPlayer;
    std::string name;
    glm::vec3 anchor{0};   // Ponto no mundo logo acima do passaro, onde a etiqueta se apoia.
    float opacity = 1;     // Some aos poucos perto do limite de distancia.
    float distance = 0;
};

// O nome so aparece quando o outro jogador esta perto: de longe ele seria so ruido na tela,
// e a bussola e os marcadores ja indicam onde os outros estao.
struct NameTagRules {
    float fullUntil = 90.0f;     // Metros: ate aqui o nome aparece inteiro.
    float hiddenFrom = 130.0f;   // Metros: daqui em diante o nome some.
    float height = 5.5f;         // Metros acima do centro do passaro.
};

// Lista os nomes visiveis a partir da camera, do mais distante para o mais proximo, para o mais
// proximo ser desenhado por cima. Jogadores sem nome e o proprio jogador ficam de fora.
std::vector<NameTag> visibleNameTags(glm::vec3 camera, const std::vector<PlayerPublicState>& players, PlayerId localPlayer,
                                     const std::function<std::string(PlayerId)>& nameOf, const NameTagRules& rules = {});

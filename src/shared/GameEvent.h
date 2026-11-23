#pragma once
#include "shared/GameTypes.h"

enum class GameEventType { CashewCollected, CashewDelivered, CashewDropped, CashewRespawned,
                           NestRobbed, UpgradeBought, PlayerJoined, PlayerLeft, CashewSpilled };
constexpr GameEventType kLastGameEventType = GameEventType::CashewSpilled;

// NestRobbed: actor e o ladrao e target o dono do ninho; vale 1 caju.
// CashewDropped: target perdeu value cajus, e actor e quem bateu nele (kNoPlayer para NPC, ele mesmo para arvore).
// CashewSpilled: so a autoridade cria. Um caju solto (isLooseCashew) comeca a cair de position.

// Fato do jogo. O cliente pede com GameAuthority::request e so aplica o que voltar em pollEvents.
struct GameEvent {
    GameEventType type = GameEventType::CashewCollected;
    PlayerId actor = kNoPlayer;    // Quem causou o evento.
    PlayerId target = kNoPlayer;   // Quem sofreu o evento, como o dono do ninho roubado.
    CashewId cashew = kNoCashew;
    uint8_t value = 0;             // Quantidade de cajus ou codigo do upgrade.
    glm::vec3 position{0};
};

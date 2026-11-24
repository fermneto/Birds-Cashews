#pragma once
#include "shared/GameTypes.h"

enum class GameEventType { CashewCollected, CashewDelivered, CashewDropped, CashewRespawned,
                           NestRobbed, UpgradeBought, PlayerJoined, PlayerLeft, CashewSpilled,
                           WhaleBite };
constexpr GameEventType kLastGameEventType = GameEventType::WhaleBite;

// NestRobbed: actor e o ladrao e target o dono do ninho; vale 1 caju.
// CashewDropped: target perdeu value cajus, e actor e quem bateu nele (kNoPlayer para NPC, ele mesmo para arvore).
// CashewSpilled: so a autoridade cria. Um caju solto (isLooseCashew) comeca a cair de position.
// WhaleBite: actor e target sao quem passou do limite do mapa e pediu; position e onde o passaro estava.
//   A autoridade confirma (events/WhaleGate) e poe em value os cajus perdidos no mar. Cada computador anima
//   a baleia a partir do instante em que recebe o evento (events/BoundaryWhale).

// Fato do jogo. O cliente pede com GameAuthority::request e so aplica o que voltar em pollEvents.
struct GameEvent {
    GameEventType type = GameEventType::CashewCollected;
    PlayerId actor = kNoPlayer;    // Quem causou o evento.
    PlayerId target = kNoPlayer;   // Quem sofreu o evento, como o dono do ninho roubado.
    CashewId cashew = kNoCashew;
    uint8_t value = 0;             // Quantidade de cajus ou codigo do upgrade.
    glm::vec3 position{0};
};

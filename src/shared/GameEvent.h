#pragma once
#include "shared/GameTypes.h"

enum class GameEventType { CashewCollected, CashewDelivered, CashewDropped, CashewRespawned,
                           NestRobbed, UpgradeBought, PlayerJoined, PlayerLeft, CashewSpilled,
                           PlaneLaunched, PlaneStruck };
constexpr GameEventType kLastGameEventType = GameEventType::PlaneStruck;

// NestRobbed: actor e o ladrao e target o dono do ninho; vale 1 caju.
// CashewDropped: target perdeu value cajus, e actor e quem bateu nele (kNoPlayer para NPC, ele mesmo para arvore).
// CashewSpilled: so a autoridade cria. Um caju solto (isLooseCashew) comeca a cair de position.
// PlaneLaunched: o dono pede ao chegar ao teto; a autoridade completa. target e o perseguido, value o numero
//   do aviao e position onde ele aparece (events/SkyPlane.h).
// PlaneStruck: o dono avisa que o aviao value bateu em target, em position. A autoridade derruba a carga.

// Fato do jogo. O cliente pede com GameAuthority::request e so aplica o que voltar em pollEvents.
struct GameEvent {
    GameEventType type = GameEventType::CashewCollected;
    PlayerId actor = kNoPlayer;    // Quem causou o evento.
    PlayerId target = kNoPlayer;   // Quem sofreu o evento, como o dono do ninho roubado.
    CashewId cashew = kNoCashew;
    uint8_t value = 0;             // Quantidade de cajus ou codigo do upgrade.
    glm::vec3 position{0};
};

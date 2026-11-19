#pragma once
#include "game/CargoStats.h"
#include "game/CashewField.h"
#include "shared/GameAuthority.h"
#include <map>

// Coleta do jogador local: pede a autoridade e so aplica o que ela confirmar.
class CashewCollector {
public:
    CashewCollector(CashewField& field, GameAuthority& authority, PlayerId localPlayer);

    void handle(const GameEvent& event);   // Recebe cada evento vindo de GameAuthority::pollEvents.
    void update(float dt, glm::vec3 position);
    float beakTarget(glm::vec3 position) const;   // 1 com caju coletavel ao alcance do bico, senao 0.
    void setCollecting(bool collecting) { collecting_ = collecting; }   // Pousado no ninho, o passaro nao coleta.
    int unload(int amount);   // Tira cajus da carga e retorna quantos sairam de fato.

    int carried() const { return carried_; }
    CargoStats& stats() { return stats_; }
    const CargoStats& stats() const { return stats_; }

private:
    using PendingRequests = std::map<CashewId, float>;   // Segundos ate desistir de esperar a resposta.

    void applyCollected(const GameEvent& event);
    void requestNearby(glm::vec3 position);
    void requestRespawns();
    void send(GameEventType type, CashewId id);

    CashewField& field_;
    GameAuthority& authority_;
    PlayerId localPlayer_;
    CargoStats stats_;
    int carried_ = 0;
    bool collecting_ = true;
    PendingRequests pendingCollect_, pendingRespawn_;
};

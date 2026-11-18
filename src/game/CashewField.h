#pragma once
#include "shared/CashewQuery.h"
#include "world/World.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// Lugar fixo de um caju no mundo.
struct CashewSpot {
    glm::vec3 position{0};
    float yaw = 0;
    float respawnDelay = 30.0f;   // Segundos ate voltar depois de coletado.
    glm::vec3 stemAnchor{0};      // Ponto dentro da copa onde o talo se prende.
    size_t tree = 0;              // Indice do cajueiro em World::trees.
};

// Todos os cajus do mundo e quais estao disponiveis. Quem muda o estado e o fluxo de eventos da autoridade.
class CashewField : public CashewQuery {
public:
    CashewField(const World& world, uint32_t seed);
    explicit CashewField(std::vector<CashewSpot> spots);

    std::optional<CashewId> nearest(glm::vec3 position, float radius) const override;
    glm::vec3 positionOf(CashewId id) const override;   // Id invalido retorna a origem.

    size_t size() const { return spots_.size(); }
    bool contains(CashewId id) const { return id < spots_.size(); }
    bool isActive(CashewId id) const { return contains(id) && states_[id].active; }
    const std::vector<CashewSpot>& spots() const { return spots_; }

    void advance(float dt);
    bool markCollected(CashewId id);   // Retorna false se o caju ja nao estava disponivel.
    bool markRespawned(CashewId id);   // So aceita depois de vencido o tempo do caju.
    std::vector<CashewId> dueForRespawn() const;

private:
    struct State {
        bool active = true;
        double respawnAt = 0;
    };

    void growOnTree(const World& world, size_t treeIndex, uint64_t treeSeed);

    std::vector<CashewSpot> spots_;
    std::vector<State> states_;
    double clock_ = 0;
};

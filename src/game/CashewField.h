#pragma once
#include "save/WorldStore.h"
#include "shared/CashewQuery.h"
#include "shared/GameTypes.h"
#include <array>
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

// Caju derrubado: cai de onde saiu ate o chao ou uma copa e some depois de kLooseCashewSeconds.
struct LooseCashew {
    bool active = false;
    glm::vec3 from{0}, rest{0};
    float yaw = 0;
    double spilledAt = 0;
    PlayerId droppedBy = kNoPlayer;   // Quem perdeu o caju espera um pouco para pegar de volta.
};

// Todos os cajus do mundo e quais estao disponiveis. Quem muda o estado e o fluxo de eventos da autoridade.
// Os cajus fixos das copas tem ids de 0 a size() - 1; os derrubados usam os ids de isLooseCashew.
class CashewField : public CashewQuery {
public:
    static constexpr float kFallSpeed = 9.81f;   // Gravidade da queda, em m/s^2.
    static constexpr float kRegrabSeconds = 2.0f;   // Espera de quem derrubou para pegar o proprio caju.

    CashewField(const World& world, uint32_t seed);
    explicit CashewField(std::vector<CashewSpot> spots);   // Sem mundo: o caju derrubado para onde saiu.

    std::optional<CashewId> nearest(glm::vec3 position, float radius) const override;
    glm::vec3 positionOf(CashewId id) const override;   // Id invalido retorna a origem.

    size_t size() const { return spots_.size(); }   // So os cajus fixos.
    bool contains(CashewId id) const { return id < spots_.size() || isLooseCashew(id); }
    bool isActive(CashewId id) const;
    const std::vector<CashewSpot>& spots() const { return spots_; }
    std::vector<CashewId> activeLoose() const;
    float yawOf(CashewId id) const;

    void advance(float dt);
    bool markCollected(CashewId id);   // Retorna false se o caju ja nao estava disponivel.
    bool markRespawned(CashewId id);   // So aceita depois de vencido o tempo do caju.
    bool forceRespawn(CashewId id);    // Para quando outro computador ja decidiu que o caju voltou.
    std::vector<CashewId> dueForRespawn() const;
    // Estado salvo do mundo: os cajus fixos que ainda nao voltaram e quanto falta para cada um.
    std::vector<UnavailableCashew> unavailable() const;
    void restoreUnavailable(const std::vector<UnavailableCashew>& cashews);   // Ids invalidos sao ignorados.
    bool spill(CashewId id, glm::vec3 from, PlayerId droppedBy = kNoPlayer);   // Comeca a queda de um caju derrubado.
    bool collectableBy(CashewId id, PlayerId player) const;   // Ativo e, se acabou de cair deste jogador, ja liberado.

private:
    struct State {
        bool active = true;
        double respawnAt = 0;
    };

    void growOnTree(const World& world, size_t treeIndex, uint64_t treeSeed);
    glm::vec3 restingPoint(glm::vec3 from) const;
    const LooseCashew* loose(CashewId id) const;

    const World* world_ = nullptr;
    std::vector<CashewSpot> spots_;
    std::vector<State> states_;
    std::array<LooseCashew, kLooseCashewSlots> loose_{};
    double clock_ = 0;
};

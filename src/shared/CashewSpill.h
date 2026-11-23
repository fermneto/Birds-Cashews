#pragma once
#include "shared/GameEvent.h"
#include <array>
#include <cstdint>
#include <vector>


// Lado da autoridade: escolhe o id de cada caju derrubado e confere quem pega cada um.
// O host e o jogo sozinho usam a mesma regra; os outros computadores so recebem CashewSpilled.
class CashewSpill {
public:
    // Para um CashewDropped ja confirmado, devolve um CashewSpilled por caju, espalhados em volta da queda.
    // Sem vaga livre, reaproveita a do caju mais antigo.
    std::vector<GameEvent> spill(const GameEvent& dropped);

    bool take(CashewId id);                     // Primeiro pedido leva o caju; os seguintes sao recusados.
    void advance(float seconds);                // Libera as vagas dos cajus que ja sumiram em todo computador.
    std::vector<GameEvent> activeSpills() const;   // Para quem entra na sala com cajus no chao.
    int activeCount() const;

private:
    struct Slot {
        bool active = false;
        double spilledAt = 0;
        uint32_t serial = 0;
        glm::vec3 from{0};
    };
    int freeSlot() const;

    std::array<Slot, kLooseCashewSlots> slots_{};
    double clock_ = 0;
    uint32_t serial_ = 0;
};

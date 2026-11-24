#pragma once
#include "shared/GameEvent.h"
#include <array>
#include <optional>

constexpr float kWhaleSequenceSeconds = 4.8f;   // Do salto da baleia ate ela sumir de novo no mar.

// Regra da autoridade (host ou jogo sozinho) para o evento WhaleBite: so morde quem passou do limite
// do mapa sobre o mar, uma vez por saida. A baleia so volta a atacar o mesmo jogador depois que ele
// entra de novo na area da ilha, dentro do raio de aviso.
class WhaleGate {
public:
    WhaleGate();

    // Confere um pedido WhaleBite. known e o ultimo estado publicado do jogador, se a autoridade tiver um.
    // Quem chama ainda preenche value com a carga perdida e zera a carga do jogador.
    bool accept(const GameEvent& event, const std::optional<glm::vec3>& known);
    void observe(PlayerId id, glm::vec3 position);   // Rearma quem voltou para dentro do raio de aviso.
    void forget(PlayerId id);                        // Quem sai da sala volta armado.
    bool armed(PlayerId id) const { return id < kMaxPlayers && armed_[id]; }

private:
    std::array<bool, kMaxPlayers> armed_{};
};

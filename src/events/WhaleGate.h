#pragma once
#include "shared/GameEvent.h"
#include <array>
#include <optional>

// Linha do tempo de uma mordida, em segundos desde o evento. So depende da altura da mordida, entao a
// autoridade (que protege o passaro durante a sequencia) e todos os computadores que animam a baleia
// chegam aos mesmos instantes sem mais nada pela rede. Quanto mais alto o passaro, mais longo o salto.
struct WhaleTimeline {
    float target = 0;      // Altura em que a boca fecha no passaro (m).
    float apex = 0;        // Altura da ponta do focinho no apice do salto (m).
    float gravity = 0;     // Gravidade do salto (m/s2); estilizada, menor nos saltos altos para dar peso.
    float breach = 0;      // A boca rompe a agua.
    float bite = 0;        // Apice do salto: a boca fecha no passaro.
    float brake = 0;       // O corpo bate na agua e freia a queda: o splash grande.
    float impactSpeed = 0; // Velocidade da queda nesse instante (m/s); da o tamanho do splash.
    float fallEnd = 0;     // A cabeca assenta na altura de mastigar.
    float spit = 0;        // Mastiga ate aqui e cospe.
    float sinkStart = 0;   // Depois do tranco da cuspida, mergulha.
    float end = 0;         // Some debaixo da agua.
    float throwEnd = 0;    // Fim do arremesso do passaro cuspido.
};
WhaleTimeline whaleTimeline(float biteHeight);

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

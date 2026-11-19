#pragma once
#include "shared/GameTypes.h"
#include <deque>

namespace net {

inline constexpr double kInterpolationDelay = 0.1;   // Desenha os outros cerca de 100 ms no passado.
inline constexpr double kMaxExtrapolation = 0.25;    // Sem estado novo, avanca no maximo isso e para.

// Estados recebidos de um passaro remoto e a amostra suave para um instante local.
// O relogio de quem envia e diferente do local; a diferenca e estimada pelos proprios pacotes.
class RemoteTrack {
public:
    // senderTime vem no pacote; localTime e quando ele chegou. Pacotes fora de ordem sao descartados.
    void push(double senderTime, double localTime, const PlayerPublicState& state);
    // Interpola com Hermite usando a velocidade, para a curva nao ter quinas nem saltos.
    PlayerPublicState sample(double localTime) const;
    bool empty() const { return samples_.empty(); }
    const PlayerPublicState& latest() const { return samples_.back().state; }

private:
    struct Sample {
        double time;
        PlayerPublicState state;
    };
    std::deque<Sample> samples_;
    double offset_ = 0;   // localTime - senderTime da entrega mais rapida, seguindo devagar as mudancas.
};

} // namespace net

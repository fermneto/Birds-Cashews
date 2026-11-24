#pragma once
#include "entity/PlayerBird.h"
#include "events/WhaleGate.h"
#include "gfx/WhaleScene.h"
#include "shared/GameAuthority.h"
#include <vector>

// Baleia gigante do limite do mapa. Quem passa de World::kBoundaryRadius sobre o mar pede um WhaleBite a
// autoridade. Confirmado, todos os computadores animam a mesma baleia a partir do evento: ela salta do mar,
// abocanha o passaro, mastiga e cospe o passaro de volta para o centro da ilha. A animacao depende so da
// posicao do evento e do tempo desde que ele chegou, entao nada mais trafega pela rede. O dono do passaro
// controla o proprio voo durante a sequencia: preso na boca e, depois, arremessado para dentro do mapa.
namespace whale {
constexpr float kBiteTime = 1.0f;     // Salto ate a boca fechar no passaro.
constexpr float kFallEnd = 1.6f;      // A baleia cai de volta com o passaro na boca.
constexpr float kSpitTime = 3.0f;     // Mastiga ate aqui e cospe.
constexpr float kSinkStart = 3.35f;   // Depois do tranco da cuspida, mergulha.
constexpr float kEndTime = kWhaleSequenceSeconds;   // Some debaixo da agua.
constexpr float kCooldown = 8.0f;     // Segundos depois de cuspir sem outra mordida no mesmo passaro.
constexpr float kRetrySeconds = 2.0f; // Sem resposta da autoridade, pede de novo.
constexpr float kSpitSpeed = 70.0f;   // m/s para dentro do mapa.
constexpr float kSpitClimb = 14.0f;   // m/s para cima na saida da boca.
constexpr float kMaxLeap = 75.0f;     // A boca nunca sobe mais que isso acima do mar.
constexpr float kScale = 1.4f;       // O modelo (gfx/WhaleView) tem 46 m; a baleia do jogo tem uns 65 m.
constexpr float kJawOpen = 0.75f;     // Radianos com a boca toda aberta no salto.

// Pose da baleia: mouth e a ponta do focinho; forward aponta para onde a cabeca olha.
struct Pose {
    glm::vec3 mouth{0};
    glm::vec3 forward{0, 0, 1}, up{0, 1, 0}, right{1, 0, 0};
    float jaw = 0;
    glm::mat4 model() const;
    glm::vec3 holdPoint() const;   // Entre as mandibulas, onde o passaro fica preso.
};
Pose poseAt(glm::vec3 bite, float age);

// Pose do passaro preso, ate kSpitTime. start e de onde ele foi puxado (o dono usa a propria posicao).
struct HeldBird {
    glm::vec3 position{0};
    float yaw = 0, pitch = 0;
};
HeldBird heldAt(glm::vec3 bite, glm::vec3 start, float startYaw, float age);

float yawToCenter(glm::vec3 position);   // Guinada que aponta o passaro para o centro da ilha.
glm::vec3 outward(glm::vec3 position);   // Direcao horizontal do centro para a posicao.
}

enum class WhaleCueKind { Spout, Splash, Bite, Chew, Spit, Rumble };

// Som pedido pela baleia. volume ja vem de 0 a 1; a distancia ate quem ouve fica com quem toca.
struct WhaleCue {
    WhaleCueKind kind = WhaleCueKind::Splash;
    glm::vec3 position{0};
    float volume = 1;
};

// Uma baleia em andamento, vista por este computador.
struct WhaleAttack {
    PlayerId target = kNoPlayer;
    glm::vec3 bite{0};    // Posicao do evento.
    glm::vec3 start{0};   // De onde o passaro foi puxado; para os outros jogadores, a propria mordida.
    float startYaw = 0;
    float age = 0;
    bool spat = false;    // So o dono usa: o passaro ja foi arremessado.
};

class BoundaryWhale {
public:
    explicit BoundaryWhale(PlayerId localPlayer) : localPlayer_(localPlayer) {}

    // WhaleBite confirmado pela autoridade. Comeca a animacao de qualquer jogador; o local fica preso.
    void handle(const GameEvent& event, const FlightModel& localFlight);
    // A cada passo da fisica. Retorna true quando a baleia segura o passaro e a fisica normal nao deve rodar.
    // Na hora de cuspir, arremessa o passaro e devolve o controle no mesmo passo.
    bool stepBird(PlayerBird& bird);
    // Uma vez por quadro, depois dos eventos: avanca as animacoes, o aviso e pede a mordida quando preciso.
    void update(float seconds, PlayerBird& bird, const World& world, GameAuthority& authority, bool canTrigger);

    bool holdsLocal() const;            // O passaro local esta na boca da baleia.
    bool busy() const;                  // Ha uma baleia atras do passaro local (ate ela sumir).
    float warning() const { return warning_; }   // 0 a 1: quao perto do limite, sobre o mar.
    float cooldown() const { return cooldown_; }
    bool waiting() const { return waitTimer_ > 0; }   // Pedido enviado, sem resposta ainda.
    bool heldPose(PlayerId id, whale::HeldBird& pose) const;   // Para desenhar outro jogador preso na boca.
    const std::vector<WhaleAttack>& attacks() const { return attacks_; }

    std::vector<WhaleCue> drainCues();
    WhaleScene scene() const;

private:
    WhaleAttack* localAttack();
    const WhaleAttack* localAttack() const;
    void advanceAttacks(float seconds, PlayerBird& bird);
    void updateWarning(float seconds, const PlayerBird& bird, const World& world);
    void requestBite(const PlayerBird& bird, const World& world, GameAuthority& authority, bool canTrigger);
    void appendAttackMarks(WhaleScene& scene, const WhaleAttack& attack) const;
    void appendWarningMarks(WhaleScene& scene) const;

    PlayerId localPlayer_;
    std::vector<WhaleAttack> attacks_;
    std::vector<WhaleCue> cues_;
    float cooldown_ = 0, waitTimer_ = 0;
    float warning_ = 0, clock_ = 0;
    float spoutTimer_ = 0, rumbleTimer_ = 0, spoutAge_ = -1;
    glm::vec3 lurk_{0};   // Onde a sombra da baleia ronda durante o aviso.
};

#pragma once
#include "entity/PlayerBird.h"
#include "events/WhaleGate.h"
#include "gfx/WhaleScene.h"
#include "shared/GameAuthority.h"
#include <vector>

// Baleia gigante do limite do mapa. Quem passa de World::kBoundaryRadius sobre o mar pede um WhaleBite a
// autoridade. Confirmado, todos os computadores animam a mesma baleia a partir do evento: ela salta do mar
// ate a altura do passaro, abocanha no apice, cai de volta com ele, mastiga e cospe o passaro para o centro
// da ilha. A animacao depende so da posicao do evento e do tempo desde que ele chegou (os instantes vem de
// whaleTimeline, em events/WhaleGate), entao nada mais trafega pela rede. O dono do passaro controla o
// proprio voo durante a sequencia: preso na boca e, depois, arremessado para dentro do mapa.
namespace whale {
constexpr float kCooldown = 8.0f;     // Segundos depois de cuspir sem outra mordida no mesmo passaro.
constexpr float kRetrySeconds = 2.0f; // Sem resposta da autoridade, pede de novo.
constexpr float kSpitSpeed = 115.0f;  // m/s para dentro do mapa.
constexpr float kSpitClimb = 18.0f;   // m/s para cima na saida da boca.
constexpr float kCoastSeconds = 0.45f;   // O passaro perde o embalo assim, enquanto a baleia sobe ate ele.
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

// Pose do passaro preso, ate a cuspida. start e velocity sao de onde e como ele voava quando o evento chegou
// (o dono usa o proprio voo; os outros, a posicao do evento parada).
struct HeldBird {
    glm::vec3 position{0};
    float yaw = 0, pitch = 0;
};
HeldBird heldAt(glm::vec3 bite, glm::vec3 start, glm::vec3 velocity, float startYaw, float age);

float yawToCenter(glm::vec3 position);   // Guinada que aponta o passaro para o centro da ilha.
glm::vec3 outward(glm::vec3 position);   // Direcao horizontal do centro para a posicao.
}

enum class WhaleCueKind { Spout, Splash, Bite, Chew, Spit, Rumble, Song };

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
    glm::vec3 start{0};   // Onde o passaro estava; para os outros jogadores, a propria mordida.
    glm::vec3 velocity{0};   // Embalo do passaro no evento; zero para os outros jogadores.
    float startYaw = 0;
    WhaleTimeline timeline;   // Instantes da sequencia, pela altura da mordida.
    float age = 0;
    bool spat = false;    // So o dono usa: o passaro ja foi arremessado.
};

class BoundaryWhale {
public:
    explicit BoundaryWhale(PlayerId localPlayer) : localPlayer_(localPlayer) {}

    // WhaleBite confirmado pela autoridade. Comeca a animacao de qualquer jogador; o local fica preso.
    void handle(const GameEvent& event, const FlightModel& localFlight);
    // A cada passo da fisica. Retorna true quando a baleia segura o passaro e a fisica normal nao deve rodar.
    // Na hora de cuspir, arremessa o passaro; durante o arremesso a fisica roda, mas a velocidade e mantida.
    bool stepBird(PlayerBird& bird);
    // Uma vez por quadro, depois dos eventos: avanca as animacoes, o aviso e pede a mordida quando preciso.
    void update(float seconds, PlayerBird& bird, const World& world, GameAuthority& authority, bool canTrigger);

    bool holdsLocal() const;            // O passaro local esta na boca da baleia.
    bool throwsLocal() const;           // O passaro local acabou de ser cuspido e ainda voa arremessado.
    bool busy() const;                  // Ha uma baleia atras do passaro local (ate ela sumir e o arremesso acabar).
    float warning() const { return warning_; }   // 0 a 1: quao perto do limite, sobre o mar.
    float cooldown() const { return cooldown_; }
    bool waiting() const { return waitTimer_ > 0; }   // Pedido enviado, sem resposta ainda.
    bool heldPose(PlayerId id, whale::HeldBird& pose) const;   // Para desenhar outro jogador preso na boca.
    float flailOf(PlayerId id) const;   // 0 a 1: asas se debatendo, preso na boca ou arremessado pela cuspida.
    const std::vector<WhaleAttack>& attacks() const { return attacks_; }
    const WhaleAttack* localAttack() const;   // A baleia atras do passaro local, se houver.

    std::vector<WhaleCue> drainCues();
    WhaleScene scene() const;

private:
    WhaleAttack* localAttack();
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

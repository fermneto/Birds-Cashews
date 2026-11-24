#pragma once
#include "entity/FlightControls.h"
#include "entity/FlightModel.h"
#include "shared/GameAuthority.h"
#include <array>
#include <vector>

class PlayerBird;

// Evento do teto: quem sobe ate a altura maxima chama um aviao, que aparece ao longe, persegue o passaro e
// bate nele. A batida joga o passaro para baixo, deixa ele tonto e derruba a carga, como o cacador.
//
// Replicacao: o dono do passaro pede o aviao (PlaneLaunched) quando chega ao teto. A autoridade confere a
// altura e o intervalo, escolhe o numero e o ponto de partida e confirma para todos. Cada computador simula o
// aviao a partir desse ponto, perseguindo a posicao que ele conhece do alvo. So o dono decide a batida, com a
// posicao real do proprio passaro: aplica o tranco na hora e avisa a autoridade (PlaneStruck), que confirma a
// batida para todos e derruba a carga do alvo.

constexpr float kPlaneTriggerHeight = FlightModel::kCeiling - 10.0f;   // A partir daqui o aviao vem.
constexpr float kPlaneFloor = FlightModel::kCeiling - 60.0f;   // O aviao nao desce disso: quem desce mais escapa.
constexpr float kPlaneSpawnDistance = 700.0f;   // Metros entre o alvo e o ponto onde o aviao aparece.
constexpr float kPlaneSpawnRise = 30.0f;        // O aviao aparece um pouco acima do alvo.
constexpr float kPlaneSpeed = 95.0f;            // m/s: o dobro do passaro mais rapido, para sempre alcancar.
constexpr float kPlaneHitRadius = 10.0f;        // Metros entre o aviao e o passaro para a batida.
constexpr float kPlaneApproachLimit = 25.0f;    // Segundos de perseguicao antes de desistir.
constexpr float kPlaneDepartSeconds = 9.0f;     // Depois de passar, segue reto ate sumir.
constexpr float kPlaneCooldown = 25.0f;         // O dono espera isso depois de um aviao para chamar outro.
constexpr float kPlaneRelaunchInterval = 20.0f; // A autoridade nao aceita dois avioes para o mesmo alvo nesse tempo.
constexpr float kPlaneLaunchSlack = 40.0f;      // Folga de altura no host, que ve o cliente um pouco atrasado.
constexpr float kPlaneStrikeWindow = kPlaneApproachLimit + 5.0f;
constexpr float kPlaneStunSeconds = 2.6f;       // Tontura depois da batida: sem controle do voo.
constexpr float kPlaneKnockDown = 95.0f;        // m/s para baixo logo depois da batida; cai cerca de 130 m tonto.
constexpr float kPlaneKnockPush = 20.0f;        // m/s na direcao em que o aviao voava.

// Autor do CashewDropped causado pelo aviao, para a interface dizer o motivo.
constexpr PlayerId kSkyPlaneActor = 254;

enum class SkyPlanePhase { Approach, Departing };

// Um aviao em voo. A mesma conta roda em todo computador.
struct SkyPlane {
    uint8_t serial = 0;
    PlayerId target = kNoPlayer;
    SkyPlanePhase phase = SkyPlanePhase::Approach;
    glm::vec3 position{0};
    glm::vec3 forward{0, 0, -1};   // Direcao do voo, ja suavizada para o desenho.
    float roll = 0;                // Inclinacao nas curvas, so para o desenho.
    float age = 0;                 // Segundos na fase atual.
    bool struck = false;           // Ja bateu no alvo.

    static SkyPlane fromLaunch(const GameEvent& launch);
    glm::vec3 velocity() const { return forward * kPlaneSpeed; }
    // Persegue o alvo. Sem alvo conhecido, desiste. Retorna true quando chega ao ponto mirado e passa.
    bool chase(const glm::vec3* target, float dt);
    void depart();
    void fly(float dt);                    // Avanca na fase atual.
    bool gone() const { return phase == SkyPlanePhase::Departing && age >= kPlaneDepartSeconds; }
};

// Para onde o aviao mira: o alvo, mas nunca abaixo de kPlaneFloor.
glm::vec3 planeAimFor(glm::vec3 target);

// Lado da autoridade (host ou jogo sozinho): numera os avioes, escolhe de onde saem e confere as batidas.
class SkyPlaneLedger {
public:
    // Completa um PlaneLaunched pedido pelo dono de target. Com checkTiming, confere altura e intervalo pelo
    // estado que a autoridade conhece; sem isso (jogo sozinho) confia em quem pediu, como as outras regras locais.
    bool acceptLaunch(GameEvent& event, double now, const PlayerPublicState* target, bool checkTiming);
    // Confere um PlaneStruck: o aviao existe, mira esse alvo e ainda nao bateu.
    bool acceptStrike(GameEvent& event, double now, bool checkTiming);
    void forget(PlayerId player);
    // CashewDropped que a autoridade confirma junto com a batida (a carga e cortada depois na validacao).
    static GameEvent cargoDropFor(const GameEvent& strike);

private:
    struct Entry {
        bool active = false, struck = false;
        uint8_t serial = 0;
        double launchedAt = -1.0e9;
    };
    std::array<Entry, kMaxPlayers> entries_{};
    uint8_t serial_ = 0;
};

// Lado de cada computador: simula os avioes confirmados, pede o aviao quando o jogador local chega ao teto e,
// se o alvo for o jogador local, decide a batida e aplica o tranco no passaro.
class SkyPlaneEvent {
public:
    explicit SkyPlaneEvent(PlayerId localPlayer = 0) : localPlayer_(localPlayer) {}

    void handle(const GameEvent& event);   // Todo evento confirmado passa aqui.
    // Uma vez por quadro, depois da fisica. players e a lista replicada da autoridade.
    void update(float dt, PlayerBird& bird, const std::vector<PlayerPublicState>& players, GameAuthority& authority,
                bool perched);

    bool stunned() const { return stun_ > 0; }
    FlightControls stunControls() const;     // Comandos enquanto o passaro esta tonto.
    bool incoming() const;                   // Um aviao persegue o jogador local.
    bool hitRecently() const { return hitNotice_ > 0; }
    float cooldown() const { return cooldown_; }
    const std::vector<SkyPlane>& planes() const { return planes_; }
    std::vector<glm::vec3> takeImpacts();    // Onde houve batida desde a ultima chamada, para o som.

private:
    SkyPlane* find(uint8_t serial);
    void requestPlane(const PlayerBird& bird, GameAuthority& authority, bool perched, float dt);
    void strike(SkyPlane& plane, PlayerBird& bird, GameAuthority& authority);
    void tumble(PlayerBird& bird, float dt);

    PlayerId localPlayer_;
    std::vector<SkyPlane> planes_;
    std::vector<glm::vec3> impacts_;
    float cooldown_ = 0, requestWait_ = 0, stun_ = 0, hitNotice_ = 0;
};

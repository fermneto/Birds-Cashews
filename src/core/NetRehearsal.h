#pragma once
#include "ai/NpcEcosystem.h"
#include "entity/PlayerBird.h"
#include "game/CashewField.h"
#include "game/GameplaySession.h"
#include "net/NetMatch.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Um jogador sem janela nem webcam, com as regras reais da partida e um roteiro simples:
// voa ate os cajus, coleta, pousa no ninho e entrega. Usado nos testes de rede e no --rede-teste.
class RehearsalPeer {
public:
    // host e opcional: quando informado, este jogador simula os NPCs e os envia nos snapshots.
    RehearsalPeer(const World& world, uint32_t seed, CashewField field, const std::filesystem::path& saveDir,
                  std::shared_ptr<GameAuthority> authority, PlayerId localPlayer, bool simulatesNpcs,
                  net::HostAuthority* host = nullptr);

    void startProfile(const std::string& name) { session_->startProfile(name); }
    void setGoal(int cashews) { goal_ = cashews; }   // 0 so paira no ponto de partida.
    void step(float dt);                             // Um quadro: roteiro, fisica, regras e NPCs.

    const std::vector<GameEvent>& events() const { return events_; }   // Tudo que a autoridade confirmou.
    int count(GameEventType type, PlayerId actor) const;
    bool delivered() const { return session_->view().totalDelivered >= goal_ && goal_ > 0; }

    GameplaySession& session() { return *session_; }
    CashewField& field() { return field_; }
    PlayerBird& bird() { return bird_; }
    GameAuthority& authority() { return *authority_; }

private:
    class Recorder;
    void drive();

    const World& world_;
    CashewField field_;
    PlayerBird bird_;
    std::shared_ptr<GameAuthority> authority_;
    std::unique_ptr<GameplaySession> session_;
    NpcEcosystem npcs_;
    bool simulatesNpcs_;
    net::HostAuthority* host_;
    std::vector<GameEvent> events_;
    int goal_ = 0;
};

// ./birds --rede-teste host [segundos] e ./birds --rede-teste entrar IP [semente].
int runNetworkRehearsal(const std::vector<std::string>& args, const std::filesystem::path& saveDir);

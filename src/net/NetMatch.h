#pragma once
#include "net/ClientAuthority.h"
#include "net/HostAuthority.h"
#include "net/UdpSession.h"
#include "shared/GameAuthority.h"
#include "shared/NetSession.h"
#include "shared/VoiceLink.h"
#include <memory>
#include <string>

namespace net {

enum class MatchMode { Solo, Host, Client };

// A sala em uso no jogo: e a NetSession da tela de salas e cria a autoridade de rede.
// O Game chama update a todo quadro e, quando consumeChange retorna true, recria a GameplaySession
// com authority() e localPlayer().
//
// Ponto de integracao da voz (D5): voice() e o VoiceLink da sala ativa, ou nullptr jogando sozinho.
// sendVoice manda a voz do jogador local (o PlayerId de quem fala e preenchido pela autoridade);
// receiveVoice entrega a voz dos outros, ja filtrada pelo host a 80 m. Para posicionar o som, use
// authority()->players() e o PlayerId local de localPlayer(). Enviar voz tambem liga o "speaking" do
// jogador local por kSpeakingHold segundos no estado replicado.
class NetMatch final : public NetSession {
public:
    explicit NetMatch(uint32_t worldSeed, uint16_t port = kLanPort);
    ~NetMatch() override;

    bool host(const std::string& roomName) override;
    bool join(const std::string& address) override;   // Bloqueia ate ~3 s esperando o host.
    std::vector<RoomInfo> discoverRooms() override;   // Espera ~0,25 s pelas respostas.
    void leave() override;
    std::string lastError() const override { return error_; }

    // Nome e passaro enviados no Hello. O Game atualiza a todo quadro.
    void setLocalIdentity(const std::string& name, uint32_t birdSeed);
    void setCashewCount(size_t count) { cashewCount_ = count; }

    void update(float seconds);
    bool consumeChange();            // Verdadeiro uma vez depois de entrar, sair ou perder a sala.

    MatchMode mode() const { return mode_; }
    bool online() const { return mode_ != MatchMode::Solo; }
    PlayerId localPlayer() const;
    std::shared_ptr<GameAuthority> authority() const;   // nullptr jogando sozinho.
    VoiceLink* voice() const;
    HostAuthority* hostAuthority() const { return host_.get(); }
    ClientAuthority* clientAuthority() const { return client_.get(); }

    // Os testes usam prazos curtos para nao esperar segundos.
    void setTimeouts(std::chrono::milliseconds peerTimeout, std::chrono::milliseconds joinTimeout);

private:
    void becomeSolo(const std::string& error);

    UdpSession udp_;
    uint32_t worldSeed_;
    size_t cashewCount_ = kNoCashew;
    std::string name_;
    uint32_t birdSeed_ = 0;
    MatchMode mode_ = MatchMode::Solo;
    std::shared_ptr<HostAuthority> host_;
    std::shared_ptr<ClientAuthority> client_;
    std::string error_;
    bool changed_ = false;
};

// Repassa tudo a uma autoridade compartilhada, para a GameplaySession nao ser dona da rede.
class SharedAuthority final : public GameAuthority {
public:
    explicit SharedAuthority(std::shared_ptr<GameAuthority> target) : target_(std::move(target)) {}
    void request(const GameEvent& event) override { target_->request(event); }
    std::vector<GameEvent> pollEvents() override { return target_->pollEvents(); }
    std::vector<PlayerPublicState> players() const override { return target_->players(); }
    void publishLocalPlayer(const PlayerPublicState& state) override { target_->publishLocalPlayer(state); }

private:
    std::shared_ptr<GameAuthority> target_;
};

std::string joinErrorMessage(JoinStatus status);

} // namespace net

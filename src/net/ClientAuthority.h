#pragma once
#include "net/HostAuthority.h"
#include "net/Protocol.h"
#include "net/RemoteTrack.h"
#include "net/UdpSession.h"
#include "shared/GameAuthority.h"
#include "shared/VoiceLink.h"
#include <array>
#include <deque>
#include <string>
#include <vector>

namespace net {

struct ClientConfig {
    uint32_t worldSeed = 0;   // Semente local: precisa ser igual a do host.
    std::string name;
    uint32_t birdSeed = 0;
    uint8_t version = kProtocolVersion;
};

enum class ClientStatus { Connecting, Ready, Refused, Disconnected };

// Autoridade de quem entrou em uma sala. Envia os pedidos e o proprio estado ao host e so aplica
// o que o host confirmar. Desenha os outros jogadores e os NPCs ~100 ms no passado, sem simular nada.
// Tambem e o VoiceLink do cliente: a voz vai ao host, que a repassa a quem estiver perto.
class ClientAuthority final : public GameAuthority, public VoiceLink {
public:
    // O UdpSession ja precisa estar conectado; o Hello sai aqui.
    ClientAuthority(UdpSession& udp, ClientConfig config);

    void request(const GameEvent& event) override;
    std::vector<GameEvent> pollEvents() override;
    std::vector<PlayerPublicState> players() const override;
    void publishLocalPlayer(const PlayerPublicState& state) override;
    std::string playerName(PlayerId id) const override;

    void sendVoice(const VoiceFrame& frame) override;
    std::vector<VoiceFrame> receiveVoice() override;

    void update(float seconds);   // Uma vez por quadro, inclusive nos menus.

    ClientStatus status() const { return status_; }
    const std::string& error() const { return error_; }   // Motivo da recusa ou da queda, para a tela.
    PlayerId localPlayer() const { return localPlayer_; }
    uint32_t hostWorldSeed() const { return hostWorldSeed_; }
    std::vector<NpcPublicState> npcs() const;             // Interpolados, como os jogadores.
    double clock() const { return now_; }

private:
    void handleReliable(const Message& message);
    void handleSnapshot(const SnapshotMessage& snapshot);
    void refuse(const std::string& reason);
    void sendState();

    UdpSession& udp_;
    ClientConfig config_;
    ClientStatus status_ = ClientStatus::Connecting;
    std::string error_;
    PlayerId localPlayer_ = kNoPlayer;
    uint32_t hostWorldSeed_ = 0;
    double now_ = 0;
    float stateTimer_ = 0;
    bool hasLocal_ = false;
    PlayerPublicState local_;
    double speakingUntil_ = -1;
    std::array<RemoteTrack, kMaxPlayers> players_{};
    std::array<bool, kMaxPlayers> present_{};
    std::array<std::string, kMaxPlayers> names_{};   // Ultima lista de nomes enviada pelo host.
    std::vector<RemoteTrack> npcs_;
    std::vector<NpcPublicState> lastNpcs_;
    float lastSnapshotTime_ = -1;
    int unreadableSnapshots_ = 0;
    bool readSnapshot_ = false;
    std::vector<GameEvent> pending_, requests_;
    std::deque<VoiceFrame> voice_;
};

std::string refuseMessage(RefuseReason reason);

} // namespace net

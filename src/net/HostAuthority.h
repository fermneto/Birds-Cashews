#pragma once
#include "net/Protocol.h"
#include "net/RemoteTrack.h"
#include "net/UdpSession.h"
#include "shared/CashewSpill.h"
#include "shared/GameAuthority.h"
#include "shared/VoiceLink.h"
#include <array>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace net {

inline constexpr float kStateInterval = 1.0f / 20.0f;   // Estado e snapshots a 20 Hz.
inline constexpr float kVoiceRange = 300.0f;            // Alem disso a voz ja e silencio (PLANO 3.6).
inline constexpr float kSpeakingHold = 0.3f;            // Segundos de "falando" depois do ultimo quadro.
inline constexpr size_t kMaxVoiceQueue = 64;
inline constexpr float kDiveHitRange = 6.0f;        // Metros entre dois passaros para o mergulho derrubar a carga.
inline constexpr float kKnockImmunity = 2.0f;       // Segundos sem perder a carga de novo depois de um mergulho.
inline constexpr float kMinRobInterval = 3.0f;      // Mesmo sem Defesa, o roubo leva 4 s; a folga cobre a rede.
inline constexpr float kRobRange = 30.0f;           // O ladrao precisa estar pousado neste raio do ninho.

struct HostConfig {
    uint32_t worldSeed = 0;
    size_t cashewCount = kNoCashew;   // Ids de caju acima disso sao recusados.
    uint8_t version = kProtocolVersion;
    // Poleiro do ninho de cada jogador, para conferir se o ladrao esta mesmo la. Vazio pula essa conferencia.
    std::function<std::optional<glm::vec3>(PlayerId)> nestPerch;
};

// Autoridade do host. Confirma, na ordem de chegada, os pedidos locais e os dos clientes, e
// transmite os eventos confirmados a todos pelo canal confiavel. O jogador local do host e o 0.
// Tambem e o VoiceLink do host: repassa cada quadro so a quem esta a ate kVoiceRange de quem fala.
class HostAuthority final : public GameAuthority, public VoiceLink {
public:
    HostAuthority(UdpSession& udp, HostConfig config);

    void request(const GameEvent& event) override;
    std::vector<GameEvent> pollEvents() override;
    std::vector<PlayerPublicState> players() const override;   // O local e os remotos, ~100 ms no passado.
    void publishLocalPlayer(const PlayerPublicState& state) override;

    void sendVoice(const VoiceFrame& frame) override;
    std::vector<VoiceFrame> receiveVoice() override;

    // Uma vez por quadro, inclusive nos menus: le a rede, detecta saidas e envia snapshots.
    void update(float seconds);
    void setNpcs(std::vector<NpcPublicState> npcs) { npcs_ = std::move(npcs); }
    // Mundo salvo de outra partida: esses cajus comecam colhidos, inclusive para quem entrar depois.
    void restoreCollected(const std::vector<CashewId>& cashews);

    std::vector<PlayerId> remotePlayers() const;   // Clientes que ja se apresentaram.
    std::string playerName(PlayerId id) const override;   // O 0 e o proprio host.
    void setLocalName(const std::string& name);           // Avisa a sala quando o host troca de perfil.
    double clock() const { return now_; }

private:
    struct Remote {
        bool joined = false;
        std::string name;
        uint32_t birdSeed = 0;
        bool hasState = false;
        PlayerPublicState latest;
        double latestAt = 0;
        double speakingUntil = -1;
        RemoteTrack track;
    };

    void handleReliable(const Message& message);
    void handleDatagram(const Message& message);
    void welcome(PeerId peer, const HelloMessage& hello);
    void dropPlayer(PlayerId id);
    void confirm(GameEvent event, bool remote, PlayerId sender);
    bool validate(GameEvent& event, bool remote, PlayerId sender);
    bool validateRobbery(const GameEvent& event);
    bool stateOf(PlayerId id, PlayerPublicState& state) const;   // Local ou remoto ja com estado.
    void checkDiveHits();
    void flushConfirmed();
    void sendRoster();
    void sendSnapshot();
    void relayVoice(const VoiceFrame& frame);
    bool positionOf(PlayerId id, glm::vec3& position) const;
    PlayerPublicState current(PlayerId id) const;   // Ultimo estado avancado ate agora pela velocidade.

    UdpSession& udp_;
    HostConfig config_;
    double now_ = 0;
    float snapshotTimer_ = 0;
    bool hasLocal_ = false;
    PlayerPublicState local_;
    std::string localName_;
    bool rosterDirty_ = false;
    double localSpeakingUntil_ = -1;
    std::array<Remote, kMaxPlayers> remotes_{};
    std::array<uint8_t, kMaxPlayers> carried_{};   // Carga segundo os eventos confirmados.
    std::set<CashewId> collected_;
    CashewSpill spill_;
    std::array<double, kMaxPlayers> lastRobAt_{}, knockableAt_{};
    std::vector<GameEvent> pending_, outgoing_;
    std::vector<NpcPublicState> npcs_;
    std::deque<VoiceFrame> voice_;
};

} // namespace net

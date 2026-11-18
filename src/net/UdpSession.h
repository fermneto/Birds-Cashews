#pragma once
#include "shared/NetSession.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace net {

using PeerId = uint8_t;
inline constexpr PeerId kHostPeer = 0;
inline constexpr uint16_t kLanPort = 42917;

struct Message {
    PeerId from;
    std::vector<uint8_t> payload;
};

// Cada instancia pertence a uma thread. Chame poll regularmente para atender
// descobertas, conexoes, retransmissoes e timeouts.
class UdpSession final : public NetSession {
public:
    explicit UdpSession(uint16_t port = kLanPort);
    ~UdpSession() override;

    bool host(const std::string& roomName) override;
    bool join(const std::string& address) override;
    // Envia uma consulta sem bloquear e retorna as respostas recentes.
    std::vector<RoomInfo> discoverRooms() override;
    void leave() override;

    void poll();
    bool connected() const;
    std::optional<PeerId> localPeer() const;
    std::vector<PeerId> peers() const;
    // As mensagens chegam em ordem, uma vez por conexao. Retorna false se
    // o destino saiu, o pacote excede o limite ou a fila esta cheia.
    bool sendReliable(PeerId to, const std::vector<uint8_t>& payload);
    std::vector<Message> pollMessages();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace net

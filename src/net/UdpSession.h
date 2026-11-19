#pragma once
#include "shared/NetSession.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace net {

using PeerId = uint8_t;
inline constexpr PeerId kHostPeer = 0;
inline constexpr uint16_t kLanPort = 42917;
inline constexpr size_t kMaxPayload = 1024;   // Bytes uteis por datagrama, abaixo do MTU de 1500 com folga.

// Resultado da ultima tentativa de entrar em uma sala.
enum class JoinStatus { Idle, Pending, Connected, Refused, NoAnswer, InvalidAddress, SocketError };

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
    bool join(const std::string& address) override;   // Bloqueia ate a resposta, no maximo 1,5 s.
    // Comeca a entrar sem bloquear; poll avanca a tentativa e joinStatus conta o resultado.
    bool startJoin(const std::string& address);
    JoinStatus joinStatus() const;
    // Os testes encurtam os prazos para nao esperar segundos por uma queda.
    void setTimeouts(std::chrono::milliseconds peerTimeout, std::chrono::milliseconds joinTimeout);
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
    // Sem confirmacao nem ordem: pode perder, duplicar ou trocar a ordem. Para estado e voz.
    bool sendUnreliable(PeerId to, const std::vector<uint8_t>& payload);
    std::vector<Message> pollDatagrams();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace net

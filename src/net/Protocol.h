#pragma once
#include "shared/GameEvent.h"
#include "shared/VoiceLink.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Mensagens do jogo em rede, carregadas pelo UdpSession.
// Todo pacote comeca com [versao][tipo]. Hello, Welcome e Refuse nunca mudam de formato,
// para que versoes diferentes ainda consigam recusar a entrada com uma mensagem clara.
namespace net {

// Mude a versao sempre que o formato de qualquer mensagem mudar: assim versoes diferentes do jogo
// sao recusadas com uma mensagem clara, em vez de conectar pela metade. O teste "formato fixo da
// versao" falha se o formato mudar sem a versao mudar.
// Versao 2: o NPC ganhou o byte de comportamento (papel e pouso).
// Versao 3: o jogador publica a Defesa e as moedas roubaveis do ninho, e existe o evento CashewSpilled.
// Versao 4: o host envia a lista de nomes dos jogadores (Roster).
// Versao 5: o cliente avisa quando troca de perfil dentro da sala (Identity).
// Versao 6: existe o evento WhaleBite (baleia no limite do mapa).
inline constexpr uint8_t kProtocolVersion = 6;
inline constexpr size_t kMaxNameLength = 32;
inline constexpr size_t kMaxNpcs = 24;
inline constexpr uint8_t kNpcRoleLimit = 16;   // Papel vai nos 4 bits baixos do byte de comportamento.
inline constexpr size_t kMaxEventsPerMessage = 48;

enum class MessageType : uint8_t {
    Hello = 1,           // Cliente -> host: versao, semente do mundo, nome do perfil e birdSeed.
    Welcome,             // Host -> cliente: versao, semente do mundo e PlayerId atribuido.
    Refuse,              // Host -> cliente: motivo da recusa.
    EventRequest,        // Cliente -> host, confiavel: pedidos de evento.
    EventConfirmed,      // Host -> clientes, confiavel: eventos confirmados, na ordem da autoridade.
    PlayerState,         // Cliente -> host, nao confiavel: estado do proprio passaro.
    Snapshot,            // Host -> clientes, nao confiavel: todos os jogadores e os NPCs.
    Voice,               // Ida e volta pelo host, nao confiavel: um quadro de voz.
    Roster,              // Host -> clientes, confiavel: o nome de cada jogador na sala.
    Identity,            // Cliente -> host, confiavel: novo nome e birdSeed depois de trocar de perfil.
};

enum class RefuseReason : uint8_t { Full = 1, Version, WorldSeed };

// O que os clientes precisam para desenhar e animar um NPC: so o host simula.
struct NpcPublicState {
    uint32_t seed = 0;
    glm::vec3 position{0};
    float yaw = 0, pitch = 0, roll = 0;
    FlightMode mode = FlightMode::Glide;
    uint8_t role = 0;       // NpcRole: define o ritmo de batidas no planeio.
    bool perched = false;   // Pousado numa copa: asas recolhidas.
};

struct HelloMessage {
    uint8_t version = kProtocolVersion;
    uint32_t worldSeed = 0;
    std::string name;
    uint32_t birdSeed = 0;
};

struct WelcomeMessage {
    uint8_t version = kProtocolVersion;
    uint32_t worldSeed = 0;
    PlayerId player = kNoPlayer;
};

struct StateMessage {
    float time = 0;   // Relogio de quem enviou, em segundos.
    PlayerPublicState state;
};

// Perfil atual de um cliente que ja esta na sala.
struct IdentityMessage {
    std::string name;
    uint32_t birdSeed = 0;
};

// Nome de cada jogador na sala, para aparecer acima do passaro dele.
struct RosterEntry {
    PlayerId player = kNoPlayer;
    std::string name;
};
using RosterMessage = std::vector<RosterEntry>;

struct SnapshotMessage {
    float time = 0;   // Relogio do host, em segundos.
    std::vector<PlayerPublicState> players;
    std::vector<NpcPublicState> npcs;
};

// Le so o cabecalho. Falha em pacote vazio ou de tipo desconhecido.
struct MessageHeader {
    uint8_t version = 0;
    MessageType type = MessageType::Hello;
};
std::optional<MessageHeader> peekHeader(const std::vector<uint8_t>& bytes);

std::vector<uint8_t> encodeHello(const HelloMessage& message);
std::vector<uint8_t> encodeWelcome(const WelcomeMessage& message);
std::vector<uint8_t> encodeRefuse(RefuseReason reason);
std::vector<uint8_t> encodeEvents(MessageType type, const std::vector<GameEvent>& events);   // Ate kMaxEventsPerMessage.
std::vector<uint8_t> encodeState(const StateMessage& message);
std::vector<uint8_t> encodeSnapshot(const SnapshotMessage& message);
std::vector<uint8_t> encodeVoice(const VoiceFrame& frame);
std::vector<uint8_t> encodeRoster(const RosterMessage& roster);
std::vector<uint8_t> encodeIdentity(const IdentityMessage& identity);   // Ate kMaxPlayers nomes, cortados em kMaxNameLength.

// Cada decodificador confere versao, tipo, tamanho exato e faixas validas, e falha em qualquer desvio.
// Hello, Welcome e Refuse aceitam qualquer versao, para que quem recebe possa comparar.
std::optional<HelloMessage> decodeHello(const std::vector<uint8_t>& bytes);
std::optional<WelcomeMessage> decodeWelcome(const std::vector<uint8_t>& bytes);
std::optional<RefuseReason> decodeRefuse(const std::vector<uint8_t>& bytes);
std::optional<std::vector<GameEvent>> decodeEvents(MessageType type, const std::vector<uint8_t>& bytes);
std::optional<StateMessage> decodeState(const std::vector<uint8_t>& bytes);
std::optional<SnapshotMessage> decodeSnapshot(const std::vector<uint8_t>& bytes);
std::optional<VoiceFrame> decodeVoice(const std::vector<uint8_t>& bytes);
std::optional<RosterMessage> decodeRoster(const std::vector<uint8_t>& bytes);
std::optional<IdentityMessage> decodeIdentity(const std::vector<uint8_t>& bytes);

// Divide uma lista longa em mensagens que cabem no limite do UdpSession.
std::vector<std::vector<uint8_t>> encodeEventBatches(MessageType type, const std::vector<GameEvent>& events);

} // namespace net

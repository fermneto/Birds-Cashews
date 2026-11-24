#include "net/MatchTests.h"
#include "ai/NpcEcosystem.h"
#include "core/NetRehearsal.h"
#include "core/TestRun.h"
#include "net/ClientAuthority.h"
#include "net/HostAuthority.h"
#include "net/NetMatch.h"
#include "net/Protocol.h"
#include "net/RemoteTrack.h"
#include "save/ProfileStore.h"
#include "voice/VoiceChat.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <set>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace net;
using Bytes = std::vector<uint8_t>;
constexpr uint32_t kWorld = 20261207;
constexpr float kDt = 1.0f / 60.0f;

// ---- Montagem de salas reais em loopback, todas na mesma thread ----

uint16_t freePort() {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    uint16_t port = 0;
    if (fd >= 0 && bind(fd, reinterpret_cast<const sockaddr*>(&address), size) == 0 &&
        getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0) port = ntohs(address.sin_port);
    if (fd >= 0) close(fd);
    return port;
}

struct Host {
    explicit Host(uint16_t port, size_t cashews = 100, std::function<std::optional<glm::vec3>(PlayerId)> perch = {})
        : udp(port), authority(std::make_shared<HostAuthority>(udp, HostConfig{kWorld, cashews, kProtocolVersion, std::move(perch)})) {
        ok = udp.host("Sala de teste");
    }
    std::vector<GameEvent> drain() {
        auto events = authority->pollEvents();
        log.insert(log.end(), events.begin(), events.end());
        return events;
    }
    UdpSession udp;
    std::shared_ptr<HostAuthority> authority;
    std::vector<GameEvent> log;
    bool ok = false;
};

struct Client {
    explicit Client(uint16_t port) : udp(port) {}
    std::vector<GameEvent> drain() {
        if (!authority) return {};
        auto events = authority->pollEvents();
        log.insert(log.end(), events.begin(), events.end());
        return events;
    }
    UdpSession udp;
    std::shared_ptr<ClientAuthority> authority;
    std::vector<GameEvent> log;
    bool paused = false;   // Parado, simula um computador que sumiu sem avisar.
};

struct Room {
    explicit Room(size_t cashews = 100, std::function<std::optional<glm::vec3>(PlayerId)> perch = {})
        : port(freePort()), host(port, cashews, std::move(perch)) {}

    // Um quadro para todos: o tempo do jogo anda dt, e a rede de verdade leva uma fracao de milissegundo.
    void step(float dt = kDt) {
        host.authority->update(dt);
        host.drain();
        for (auto& client : clients) {
            if (client->paused) continue;
            if (client->authority) client->authority->update(dt);
            else client->udp.poll();
            client->drain();
        }
        std::this_thread::sleep_for(500us);
    }
    bool pumpUntil(const std::function<bool()>& done, int maxSteps = 600) {
        for (int i = 0; i < maxSteps; i++) {
            if (done()) return true;
            step();
        }
        return done();
    }
    void pump(int steps) { for (int i = 0; i < steps; i++) step(); }

    // Entra pelo UdpSession e depois se apresenta. Retorna o cliente mesmo se for recusado.
    Client& join(ClientConfig config = {}) {
        if (config.worldSeed == 0) config.worldSeed = kWorld;
        clients.push_back(std::make_unique<Client>(port));
        Client& client = *clients.back();
        client.udp.startJoin("127.0.0.1:" + std::to_string(port));
        pumpUntil([&] { return client.udp.joinStatus() != JoinStatus::Pending; });
        if (client.udp.joinStatus() != JoinStatus::Connected) return client;
        client.authority = std::make_shared<ClientAuthority>(client.udp, config);
        pumpUntil([&] { return client.authority->status() != ClientStatus::Connecting; });
        return client;
    }

    uint16_t port;
    Host host;
    std::vector<std::unique_ptr<Client>> clients;
};

bool ready(const Client& client) { return client.authority && client.authority->status() == ClientStatus::Ready; }

GameEvent eventOf(GameEventType type, PlayerId actor, CashewId cashew = kNoCashew, uint8_t value = 1) {
    GameEvent event;
    event.type = type;
    event.actor = actor;
    event.cashew = cashew;
    event.value = value;
    event.position = glm::vec3(1, 2, 3);
    return event;
}

int countOf(const std::vector<GameEvent>& log, GameEventType type, PlayerId actor) {
    return static_cast<int>(std::count_if(log.begin(), log.end(), [&](const GameEvent& e) {
        return e.type == type && e.actor == actor;
    }));
}

bool sameEvent(const GameEvent& a, const GameEvent& b) {
    return a.type == b.type && a.actor == b.actor && a.target == b.target && a.cashew == b.cashew && a.value == b.value;
}

// Eventos de jogo, sem entradas e saidas, que cada computador ve em momentos diferentes.
std::vector<GameEvent> gameplayOnly(const std::vector<GameEvent>& log) {
    std::vector<GameEvent> result;
    for (const GameEvent& e : log)
        if (e.type != GameEventType::PlayerJoined && e.type != GameEventType::PlayerLeft) result.push_back(e);
    return result;
}

bool sameSequence(const std::vector<GameEvent>& a, const std::vector<GameEvent>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), sameEvent);
}

PlayerPublicState stateAt(PlayerId id, glm::vec3 position, glm::vec3 velocity = glm::vec3(0)) {
    PlayerPublicState state;
    state.id = id;
    state.position = position;
    state.velocity = velocity;
    state.yaw = 0.5f;
    state.mode = FlightMode::Flap;
    state.carried = 2;
    state.birdSeed = 1234u + id;
    state.nestLevel = 3;
    return state;
}

// ---- Protocolo ----

// Todo prefixo de uma mensagem valida precisa ser recusado.
template <class Decode>
bool rejectsTruncated(const Bytes& bytes, Decode decode) {
    for (size_t size = 0; size < bytes.size(); size++)
        if (decode(Bytes(bytes.begin(), bytes.begin() + static_cast<long>(size)))) return false;
    Bytes longer = bytes;
    longer.push_back(0);
    return !decode(longer);
}

// Embaralha bytes ao acaso: o decodificador pode aceitar ou recusar, mas nunca devolver lixo fora das faixas.
template <class Decode, class Valid>
bool survivesNoise(const Bytes& bytes, Decode decode, Valid valid) {
    std::mt19937 random(7);
    for (int round = 0; round < 3000; round++) {
        Bytes noisy = bytes;
        const int flips = 1 + static_cast<int>(random() % 4);
        for (int i = 0; i < flips; i++) noisy[2 + random() % (noisy.size() - 2)] = static_cast<uint8_t>(random());
        if (const auto decoded = decode(noisy); decoded && !valid(*decoded)) return false;
    }
    return true;
}

bool validPlayer(const PlayerPublicState& p) {
    return p.id < kMaxPlayers && std::isfinite(p.position.x) && std::isfinite(p.velocity.y) && std::isfinite(p.yaw) &&
           static_cast<int>(p.mode) <= static_cast<int>(FlightMode::Dive);
}

void testProtocol(TestRun& t) {
    HelloMessage hello{kProtocolVersion, kWorld, "Ana", 99};
    const auto helloBytes = encodeHello(hello);
    const auto helloBack = decodeHello(helloBytes);
    t.check("protocolo: Hello ida e volta", helloBack && helloBack->name == "Ana" && helloBack->birdSeed == 99 &&
                                            helloBack->worldSeed == kWorld && helloBack->version == kProtocolVersion);
    t.check("protocolo: Hello truncado recusado", rejectsTruncated(helloBytes, decodeHello));
    Bytes badName = helloBytes;
    badName[7] = '\n';
    t.check("protocolo: Hello com caractere de controle recusado", !decodeHello(badName));
    Bytes longName = encodeHello({kProtocolVersion, kWorld, std::string(60, 'x'), 1});
    t.check("protocolo: nome longo e cortado em 32", decodeHello(longName) && decodeHello(longName)->name.size() == kMaxNameLength);
    Bytes otherVersion = encodeHello({kProtocolVersion + 1, kWorld, "Bia", 1});
    t.check("protocolo: Hello de outra versao ainda e lido para ser recusado",
            decodeHello(otherVersion) && decodeHello(otherVersion)->version == kProtocolVersion + 1);

    const auto welcome = encodeWelcome({kProtocolVersion, kWorld, 3});
    t.check("protocolo: Welcome ida e volta", decodeWelcome(welcome) && decodeWelcome(welcome)->player == 3);
    t.check("protocolo: Welcome truncado recusado", rejectsTruncated(welcome, decodeWelcome));
    t.check("protocolo: Welcome com id do host ou fora da sala recusado",
            !decodeWelcome(encodeWelcome({kProtocolVersion, kWorld, 0})) &&
            !decodeWelcome(encodeWelcome({kProtocolVersion, kWorld, kMaxPlayers})));

    const auto refuse = encodeRefuse(RefuseReason::WorldSeed);
    Bytes badReason = refuse;
    badReason[2] = 9;
    t.check("protocolo: Refuse ida e volta e motivo invalido recusado",
            decodeRefuse(refuse) == RefuseReason::WorldSeed && !decodeRefuse(badReason) && rejectsTruncated(refuse, decodeRefuse));

    std::vector<GameEvent> events = {eventOf(GameEventType::CashewCollected, 2, 40), eventOf(GameEventType::CashewDelivered, 1, kNoCashew, 3)};
    events[1].target = 4;
    const auto request = encodeEvents(MessageType::EventRequest, events);
    const auto decodedEvents = decodeEvents(MessageType::EventRequest, request);
    t.check("protocolo: eventos ida e volta", decodedEvents && sameSequence(*decodedEvents, events) &&
                                              decodedEvents->at(0).position == glm::vec3(1, 2, 3));
    t.check("protocolo: pedido nao passa por confirmado",
            !decodeEvents(MessageType::EventConfirmed, request) && !decodeEvents(MessageType::Snapshot, request));
    const auto decodeRequest = [](const Bytes& b) { return decodeEvents(MessageType::EventRequest, b); };
    t.check("protocolo: eventos truncados recusados", rejectsTruncated(request, decodeRequest));
    Bytes badType = request;
    badType[3] = 200;
    Bytes badCount = request;
    badCount[2] = 3;
    Bytes nanPosition = request;
    for (int i = 0; i < 4; i++) nanPosition[3 + 6 + i] = 0xFF;
    t.check("protocolo: tipo, contagem ou posicao invalidos recusados",
            !decodeRequest(badType) && !decodeRequest(badCount) && !decodeRequest(nanPosition));
    Bytes wrongVersion = request;
    wrongVersion[0] = kProtocolVersion + 1;
    t.check("protocolo: mensagem de jogo de outra versao recusada", !decodeRequest(wrongVersion));
    std::vector<GameEvent> many(130, eventOf(GameEventType::CashewCollected, 1, 5));
    const auto batches = encodeEventBatches(MessageType::EventConfirmed, many);
    size_t total = 0;
    bool batchesFit = true;
    for (const auto& batch : batches) {
        batchesFit = batchesFit && batch.size() <= kMaxPayload;
        if (const auto part = decodeEvents(MessageType::EventConfirmed, batch)) total += part->size();
    }
    t.check("protocolo: lista longa vira lotes que cabem no datagrama", batches.size() == 3 && total == 130 && batchesFit);

    StateMessage state{12.5f, stateAt(2, {10, 20, 30}, {1, 2, 3})};
    state.state.speaking = true;
    const auto stateBytes = encodeState(state);
    const auto stateBack = decodeState(stateBytes);
    t.check("protocolo: estado ida e volta", stateBack && stateBack->time == 12.5f && stateBack->state.position == glm::vec3(10, 20, 30) &&
                                             stateBack->state.velocity == glm::vec3(1, 2, 3) && stateBack->state.speaking &&
                                             stateBack->state.birdSeed == 1236u && stateBack->state.nestLevel == 3 &&
                                             stateBack->state.mode == FlightMode::Flap && stateBack->state.carried == 2);
    t.check("protocolo: estado truncado recusado", rejectsTruncated(stateBytes, decodeState));
    t.check("protocolo: estado embaralhado nunca sai fora das faixas", survivesNoise(stateBytes, decodeState,
            [](const StateMessage& m) { return validPlayer(m.state) && std::isfinite(m.time); }));

    SnapshotMessage full;
    full.time = 3.0f;
    for (PlayerId id = 0; id < kMaxPlayers; id++) full.players.push_back(stateAt(id, {id * 10.0f, 50, 0}));
    // Todos os NPCs do ecossistema, com papel e pouso variando.
    const uint32_t npcCount = static_cast<uint32_t>(NpcEcosystem::kCount);
    for (uint32_t i = 0; i < npcCount; i++)
        full.npcs.push_back({i * 3u, {1, 2, 3}, 0.1f, 0.2f, 0.3f, FlightMode::Dive,
                             static_cast<uint8_t>(i % kNpcRoleCount), i % 3 == 0});
    const auto snapshotBytes = encodeSnapshot(full);
    const auto snapshotBack = decodeSnapshot(snapshotBytes);
    bool behaviors = snapshotBack && snapshotBack->npcs.size() == npcCount;
    for (uint32_t i = 0; behaviors && i < npcCount; i++)
        behaviors = snapshotBack->npcs[i].role == i % kNpcRoleCount && snapshotBack->npcs[i].perched == (i % 3 == 0);
    t.check("protocolo: snapshot ida e volta com 6 jogadores e todos os NPCs",
            snapshotBack && snapshotBack->players.size() == kMaxPlayers && snapshotBack->npcs.size() == npcCount &&
            snapshotBack->players[5].position.x == 50.0f && snapshotBack->npcs[8].seed == 24u &&
            snapshotBack->npcs[8].mode == FlightMode::Dive);
    t.check("protocolo: snapshot leva o papel e o pouso de cada NPC", behaviors);
    // 13 bytes do UdpSession + 8 do UDP + 20 do IPv4. 1200 deixa folga ate em VPN e IPv6 sobre MTU de 1500.
    t.check("protocolo: snapshot cheio (6 jogadores, " + std::to_string(npcCount) + " NPCs, " +
            std::to_string(snapshotBytes.size()) + " bytes) cabe em um datagrama sem fragmentar",
            snapshotBytes.size() + 13 + 8 + 20 <= 1200 && snapshotBytes.size() <= kMaxPayload);
    SnapshotMessage worst = full;
    worst.npcs.assign(kMaxNpcs, full.npcs[0]);
    const auto worstBytes = encodeSnapshot(worst);
    t.check("protocolo: ate o snapshot no limite de NPCs (" + std::to_string(kMaxNpcs) + ", " + std::to_string(worstBytes.size()) +
            " bytes) cabe no limite do UdpSession", worstBytes.size() <= kMaxPayload && decodeSnapshot(worstBytes) &&
            decodeSnapshot(worstBytes)->npcs.size() == kMaxNpcs);
    Bytes badBehavior = snapshotBytes;
    badBehavior.back() |= 0x40;
    t.check("protocolo: byte de comportamento invalido recusado", !decodeSnapshot(badBehavior));
    t.check("protocolo: snapshot truncado recusado", rejectsTruncated(snapshotBytes, decodeSnapshot));
    SnapshotMessage repeated = full;
    repeated.players[1].id = 0;
    t.check("protocolo: snapshot com jogador repetido recusado", !decodeSnapshot(encodeSnapshot(repeated)));
    Bytes tooMany = snapshotBytes;
    tooMany[6] = kMaxPlayers + 1;
    t.check("protocolo: snapshot com jogadores demais recusado", !decodeSnapshot(tooMany));
    t.check("protocolo: snapshot embaralhado nunca sai fora das faixas", survivesNoise(snapshotBytes, decodeSnapshot,
            [](const SnapshotMessage& m) {
                bool ok = m.players.size() <= kMaxPlayers && m.npcs.size() <= kMaxNpcs;
                for (const auto& p : m.players) ok = ok && validPlayer(p);
                return ok;
            }));

    VoiceFrame frame{4, 777, Bytes(kMaxVoicePayload, 0x5A)};
    const auto voiceBytes = encodeVoice(frame);
    const auto voiceBack = decodeVoice(voiceBytes);
    t.check("protocolo: voz ida e volta", voiceBack && voiceBack->speaker == 4 && voiceBack->sequence == 777 &&
                                          voiceBack->payload == frame.payload);
    t.check("protocolo: voz truncada recusada", rejectsTruncated(voiceBytes, decodeVoice));
    Bytes bigVoice = voiceBytes;
    bigVoice[5] = 0x02;   // Tamanho declarado 512+.
    Bytes badSpeaker = voiceBytes;
    badSpeaker[2] = kMaxPlayers;
    t.check("protocolo: voz grande demais ou de jogador invalido recusada", !decodeVoice(bigVoice) && !decodeVoice(badSpeaker));

    t.check("protocolo: cabecalho vazio ou de tipo desconhecido recusado",
            !peekHeader({}) && !peekHeader({kProtocolVersion}) && !peekHeader({kProtocolVersion, 0}) && !peekHeader({kProtocolVersion, 99}));
}

// ---- Entrada, lotacao e recusas ----

void testJoinAndCapacity(TestRun& t) {
    Room room;
    if (!room.host.ok) {
        t.check("sala: abre o host em loopback", false);
        return;
    }
    bool allReady = true;
    for (int i = 0; i < kMaxPlayers - 1; i++) allReady = allReady && ready(room.join({kWorld, "Jogador" + std::to_string(i), 100u + i}));
    t.check("sala: 5 clientes entram", allReady && room.host.authority->remotePlayers().size() == 5);
    bool idsDistinct = true;
    for (size_t i = 0; i < room.clients.size(); i++) idsDistinct = idsDistinct && room.clients[i]->authority->localPlayer() == i + 1;
    t.check("sala: cada cliente recebe o proprio PlayerId", idsDistinct);
    t.check("sala: host emite PlayerJoined para cada um", countOf(room.host.log, GameEventType::PlayerJoined, 5) == 1 &&
                                                         room.host.log.size() == 5);
    t.check("sala: host guarda o nome da apresentacao", room.host.authority->playerName(3) == "Jogador2");

    Client& sixth = room.join();
    t.check("sala: o 6o cliente e recusado com sala cheia",
            sixth.udp.joinStatus() == JoinStatus::Refused && !sixth.authority &&
            joinErrorMessage(JoinStatus::Refused).find("cheia") != std::string::npos);
    room.pump(10);
    t.check("sala: a recusa nao muda a sala", room.host.authority->remotePlayers().size() == 5);

    // Quem entrou por ultimo conhece todos que ja estavam.
    const Client& last = *room.clients[4];
    t.check("sala: o recem-chegado recebe quem ja estava", countOf(last.log, GameEventType::PlayerJoined, 0) == 1 &&
                                                          countOf(last.log, GameEventType::PlayerJoined, 1) == 1 &&
                                                          countOf(last.log, GameEventType::PlayerJoined, 5) == 1);
}

void testRefusals(TestRun& t) {
    Room room;
    Client& seed = room.join({kWorld + 1, "Outra ilha", 1});
    t.check("recusa: semente do mundo diferente", seed.authority && seed.authority->status() == ClientStatus::Refused &&
                                                  seed.authority->error().find("semente") != std::string::npos);
    ClientConfig oldVersion{kWorld, "Antigo", 1, static_cast<uint8_t>(kProtocolVersion + 1)};
    Client& version = room.join(oldVersion);
    t.check("recusa: versao diferente", version.authority && version.authority->status() == ClientStatus::Refused &&
                                        version.authority->error().find("versão") != std::string::npos);
    t.check("recusa: ninguem recusado vira jogador", room.host.authority->remotePlayers().empty() &&
                                                    countOf(room.host.log, GameEventType::PlayerJoined, 1) == 0);

    UdpSession lost(freePort());
    lost.setTimeouts(4000ms, 200ms);
    lost.startJoin("127.0.0.1:" + std::to_string(freePort()));
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (lost.joinStatus() == JoinStatus::Pending && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
        lost.poll();
    }
    t.check("recusa: host inexistente vira 'host nao encontrado'", lost.joinStatus() == JoinStatus::NoAnswer &&
            joinErrorMessage(lost.joinStatus()).find("não encontrado") != std::string::npos);
    t.check("recusa: endereco invalido", !lost.startJoin("999.1.1.1") && lost.joinStatus() == JoinStatus::InvalidAddress);
}

// ---- Ordem dos eventos e disputa pelos cajus ----

void testOrderAndDispute(TestRun& t) {
    Room room;
    Client& a = room.join({kWorld, "A", 1});
    Client& b = room.join({kWorld, "B", 2});
    room.pump(5);
    for (auto* log : {&room.host.log, &a.log, &b.log}) log->clear();

    // Os dois pedem o caju 7 no mesmo quadro; o host tambem pede um caju e os NPCs outro.
    a.authority->request(eventOf(GameEventType::CashewCollected, 1, 7));
    b.authority->request(eventOf(GameEventType::CashewCollected, 2, 7));
    b.authority->request(eventOf(GameEventType::CashewCollected, 2, 8));
    room.host.authority->request(eventOf(GameEventType::CashewCollected, 0, 9));
    room.host.authority->request(eventOf(GameEventType::CashewCollected, kNoPlayer, 10));
    room.pumpUntil([&] { return gameplayOnly(a.log).size() >= 4 && gameplayOnly(b.log).size() >= 4; });
    room.pump(20);

    const auto hostEvents = gameplayOnly(room.host.log);
    t.check("ordem: o mesmo caju disputado so tem um dono",
            std::count_if(hostEvents.begin(), hostEvents.end(), [](const GameEvent& e) { return e.cashew == 7; }) == 1);
    t.check("ordem: todos veem a mesma sequencia", hostEvents.size() == 4 && sameSequence(hostEvents, gameplayOnly(a.log)) &&
                                                   sameSequence(hostEvents, gameplayOnly(b.log)));
    t.check("ordem: os pedidos do host sao confirmados na hora, antes dos que vem pela rede",
            hostEvents.size() == 4 && hostEvents[0].cashew == 9 && hostEvents[1].cashew == 10 && hostEvents[1].actor == kNoPlayer);

    // O cliente nunca fala em nome de outro: o autor e trocado por quem enviou.
    a.authority->request(eventOf(GameEventType::CashewCollected, 2, 20));
    room.pumpUntil([&] { return gameplayOnly(b.log).size() >= 5; });
    t.check("ordem: autor forjado vira quem enviou", gameplayOnly(b.log).size() == 5 && gameplayOnly(b.log).back().actor == 1);

    // Entrega: ninguem entrega mais do que coletou.
    const PlayerId winner = hostEvents[2].actor;
    Client& w = winner == 1 ? a : b;
    w.authority->request(eventOf(GameEventType::CashewDelivered, winner, kNoCashew, 50));
    room.pumpUntil([&] { return countOf(room.host.log, GameEventType::CashewDelivered, winner) == 1; });
    const auto delivered = std::find_if(room.host.log.begin(), room.host.log.end(), [](const GameEvent& e) {
        return e.type == GameEventType::CashewDelivered;
    });
    const int expected = 2;   // O vencedor do 7 ficou com mais um caju: o 8 ou o 20.
    t.check("ordem: entrega limitada ao que foi coletado", delivered != room.host.log.end() && delivered->value == expected);

    // Reaparecimento: so o host agenda.
    const size_t before = room.host.log.size();
    a.authority->request(eventOf(GameEventType::CashewRespawned, 1, 7));
    a.authority->request(eventOf(GameEventType::PlayerLeft, 1));
    room.pump(30);
    t.check("respawn: pedido de cliente e ignorado", room.host.log.size() == before &&
                                                    countOf(a.log, GameEventType::CashewRespawned, 0) == 0);
    room.host.authority->request(eventOf(GameEventType::CashewRespawned, 0, 7));
    room.pumpUntil([&] { return countOf(a.log, GameEventType::CashewRespawned, 0) == 1 && countOf(b.log, GameEventType::CashewRespawned, 0) == 1; });
    t.check("respawn: o do host chega a todos", countOf(a.log, GameEventType::CashewRespawned, 0) == 1 &&
                                               countOf(b.log, GameEventType::CashewRespawned, 0) == 1);
    const int collectedByB = countOf(a.log, GameEventType::CashewCollected, 2);
    b.authority->request(eventOf(GameEventType::CashewCollected, 2, 7));
    room.pumpUntil([&] { return countOf(a.log, GameEventType::CashewCollected, 2) > collectedByB; });
    t.check("respawn: o caju que voltou pode ser coletado de novo", countOf(a.log, GameEventType::CashewCollected, 2) == collectedByB + 1);

    // Quem entra depois recebe os cajus ja coletados.
    Client& late = room.join({kWorld, "C", 3});
    room.pump(10);
    int synced = 0;
    for (const GameEvent& e : late.log)
        if (e.type == GameEventType::CashewCollected && e.actor == kNoPlayer) synced++;
    t.check("ordem: quem chega depois recebe os cajus ja coletados", synced == 5);   // 7, 8, 9, 10 e 20.
}

// ---- Interpolacao ----

glm::vec3 circleAt(double time) {
    const float angle = static_cast<float>(time) * 0.8f;
    return {std::cos(angle) * 40.0f, 60.0f + std::sin(angle * 2.0f) * 5.0f, std::sin(angle) * 40.0f};
}

glm::vec3 circleVelocity(double time) {
    const float angle = static_cast<float>(time) * 0.8f;
    return {-std::sin(angle) * 32.0f, std::cos(angle * 2.0f) * 8.0f, std::cos(angle) * 32.0f};
}

void testInterpolation(TestRun& t) {
    RemoteTrack track;
    std::mt19937 random(3);
    double maxError = 0, maxJump = 0;
    glm::vec3 previous(0);
    bool first = true;
    const double latency = 0.73;   // Relogios diferentes: a diferenca vem dos proprios pacotes.
    double nextSend = 0;
    for (double local = latency; local < 6.0 + latency; local += 1.0 / 60.0) {
        while (nextSend <= local - latency) {
            const double jitter = std::uniform_real_distribution<double>(0.0, 0.02)(random);
            if (random() % 10 != 0)   // Perde um pacote em cada dez.
                track.push(nextSend, local + jitter, stateAt(1, circleAt(nextSend), circleVelocity(nextSend)));
            nextSend += 0.05;
        }
        const glm::vec3 shown = track.sample(local).position;
        if (local > latency + 1.0) {
            maxError = std::max<double>(maxError, glm::length(shown - circleAt(local - latency - kInterpolationDelay)));
            maxJump = std::max<double>(maxJump, glm::length(shown - previous) - 33.0 / 60.0);
        }
        previous = shown;
        first = false;
    }
    t.check("interpolacao: segue o caminho real cerca de 100 ms atras", !first && maxError < 1.5);
    t.check("interpolacao: sem saltos entre quadros, mesmo perdendo pacotes", maxJump < 0.15);

    RemoteTrack stopped;
    stopped.push(0.0, 0.0, stateAt(1, {0, 0, 0}, {10, 0, 0}));
    stopped.push(0.05, 0.05, stateAt(1, {0.5f, 0, 0}, {10, 0, 0}));
    const float soon = stopped.sample(0.25).position.x, late = stopped.sample(5.0).position.x;
    t.check("interpolacao: sem estado novo, extrapola pela velocidade e para", soon > 0.5f && soon < 2.0f &&
            std::fabs(late - (0.5f + 10.0f * static_cast<float>(kMaxExtrapolation))) < 0.01f);
    RemoteTrack turning;
    PlayerPublicState a = stateAt(1, {0, 0, 0}), b = stateAt(1, {0, 0, 0});
    a.yaw = 3.1f;
    b.yaw = -3.1f;
    turning.push(0.0, 0.0, a);
    turning.push(0.1, 0.1, b);
    const float middle = turning.sample(0.15).yaw;
    t.check("interpolacao: o giro passa pelo lado curto do angulo", std::fabs(std::fabs(middle) - 3.14159f) < 0.05f);
    RemoteTrack ordered;
    ordered.push(1.0, 1.0, stateAt(1, {5, 0, 0}));
    ordered.push(0.5, 1.01, stateAt(1, {-100, 0, 0}));
    t.check("interpolacao: pacote atrasado e fora de ordem e descartado", ordered.sample(3.0).position.x == 5.0f);
}

void testSnapshotFlow(TestRun& t) {
    Room room;
    Client& a = room.join({kWorld, "A", 11});
    Client& b = room.join({kWorld, "B", 22});
    std::vector<NpcPublicState> npcs;
    for (uint32_t i = 0; i < 9; i++)
        npcs.push_back({i * 3u + 1, {i * 10.0f, 30, 0}, 0.2f, 0, 0, FlightMode::Glide, static_cast<uint8_t>(i % 8), i == 4});

    double maxJump = 0, error = 1e9;
    glm::vec3 previous(0);
    bool seenHost = false;
    for (int frame = 0; frame < 120; frame++) {
        const double time = room.host.authority->clock();
        room.host.authority->publishLocalPlayer(stateAt(0, circleAt(time), circleVelocity(time)));
        a.authority->publishLocalPlayer(stateAt(1, {100, 40, 100}, {0, 0, 0}));
        b.authority->publishLocalPlayer(stateAt(2, {-100 + frame * 0.5f, 40, 0}, {30, 0, 0}));
        for (auto& npc : npcs) npc.position.z += 0.3f;
        room.host.authority->setNpcs(npcs);
        room.step();
        for (const PlayerPublicState& p : a.authority->players()) {
            if (p.id != 0) continue;
            if (seenHost) maxJump = std::max<double>(maxJump, glm::length(p.position - previous) - 33.0 / 60.0);
            previous = p.position;
            seenHost = true;
            error = glm::length(p.position - circleAt(room.host.authority->clock() - kInterpolationDelay));
        }
    }
    const auto seenByA = a.authority->players();
    const auto findPlayer = [&](PlayerId id) {
        return std::find_if(seenByA.begin(), seenByA.end(), [id](const PlayerPublicState& p) { return p.id == id; });
    };
    t.check("snapshot: o cliente ve o host e o outro cliente", seenByA.size() == 3 && findPlayer(0) != seenByA.end() &&
                                                              findPlayer(2) != seenByA.end());
    t.check("snapshot: leva passaro, carga, modo e ninho", findPlayer(2) != seenByA.end() && findPlayer(2)->birdSeed == 1236u &&
            findPlayer(2)->carried == 2 && findPlayer(2)->mode == FlightMode::Flap && findPlayer(2)->nestLevel == 3);
    t.check("snapshot: o host aparece no cliente sem saltos", seenHost && maxJump < 0.2);
    t.check("snapshot: o host aparece onde estava ~100 ms antes", error < 3.0);
    const auto hostView = room.host.authority->players();
    t.check("snapshot: o host tambem ve os clientes", hostView.size() == 3);

    const auto replicated = b.authority->npcs();
    bool npcsMatch = replicated.size() == npcs.size();
    for (size_t i = 0; npcsMatch && i < npcs.size(); i++)
        npcsMatch = replicated[i].seed == npcs[i].seed && std::fabs(replicated[i].position.x - npcs[i].position.x) < 0.01f &&
                    std::fabs(replicated[i].position.z - npcs[i].position.z) < 5.0f && replicated[i].yaw == 0.2f &&
                    replicated[i].role == npcs[i].role && replicated[i].perched == npcs[i].perched;
    t.check("npcs: o cliente recebe os NPCs do host, interpolados, com papel e pouso", npcsMatch);
}

// ---- Queda e saida ----

void testPlayerLeft(TestRun& t) {
    Room room;
    Client& stays = room.join({kWorld, "Fica", 1});
    Client& drops = room.join({kWorld, "Cai", 2});
    Client& leaves = room.join({kWorld, "Sai", 3});
    // Prazo curto para o teste: o estado a 20 Hz mantem viva a conexao de quem continua.
    room.host.udp.setTimeouts(300ms, 1500ms);
    for (auto& client : room.clients) client->authority->publishLocalPlayer(stateAt(1, {0, 50, 0}));
    room.pump(10);
    leaves.udp.leave();
    room.pumpUntil([&] { return countOf(room.host.log, GameEventType::PlayerLeft, 3) == 1; }, 200);
    t.check("saida: sair da sala emite PlayerLeft", countOf(room.host.log, GameEventType::PlayerLeft, 3) == 1);

    drops.paused = true;   // Some sem avisar, como um Wi-Fi que caiu.
    const auto start = std::chrono::steady_clock::now();
    room.pumpUntil([&] { return countOf(stays.log, GameEventType::PlayerLeft, 2) == 1; }, 3000);
    const auto waited = std::chrono::steady_clock::now() - start;
    t.check("queda: o host emite PlayerLeft quando a conexao cai", countOf(room.host.log, GameEventType::PlayerLeft, 2) == 1);
    t.check("queda: os outros clientes tambem ficam sabendo", countOf(stays.log, GameEventType::PlayerLeft, 2) == 1 && waited < 2s);
    room.pump(10);
    const auto view = stays.authority->players();
    t.check("queda: quem caiu some da lista", std::none_of(view.begin(), view.end(), [](const PlayerPublicState& p) {
        return p.id == 2 || p.id == 3;
    }));
    t.check("queda: os outros seguem na sala", room.host.authority->remotePlayers() == std::vector<PlayerId>{1});

    room.host.udp.leave();
    stays.udp.setTimeouts(300ms, 1500ms);
    room.pumpUntil([&] { return stays.authority->status() == ClientStatus::Disconnected; }, 300);
    t.check("queda: o cliente percebe que o host saiu", stays.authority->status() == ClientStatus::Disconnected &&
                                                       !stays.authority->error().empty());
}

// ---- Voz ----

void testVoiceRelay(TestRun& t) {
    Room room;
    for (int i = 1; i < kMaxPlayers; i++) room.join({kWorld, "V" + std::to_string(i), 0});
    // Do jogador 1: host a 10 m, 2 a 299 m, 3 a 301 m; 4 e 5 longe de todos, mas perto um do outro.
    const glm::vec3 spots[] = {{0, 50, 0}, {10, 50, 0}, {10, 50, 299}, {10, 50, -301}, {900, 50, 900}, {930, 50, 900}};
    const auto place = [&] {
        room.host.authority->publishLocalPlayer(stateAt(0, spots[0]));
        for (int i = 1; i < kMaxPlayers; i++) room.clients[i - 1]->authority->publishLocalPlayer(stateAt(i, spots[i]));
    };
    for (int i = 0; i < 12; i++) {
        place();
        room.step();
    }
    std::array<std::vector<VoiceFrame>, kMaxPlayers> heard;
    const auto listen = [&] {
        for (auto& frame : room.host.authority->receiveVoice()) heard[0].push_back(frame);
        for (int i = 1; i < kMaxPlayers; i++)
            for (auto& frame : room.clients[i - 1]->authority->receiveVoice()) heard[i].push_back(frame);
    };
    room.clients[0]->authority->sendVoice({kNoPlayer, 1, Bytes(80, 1)});
    for (int i = 0; i < 5; i++) { place(); room.step(); listen(); }
    t.check("voz: o host recebe e repassa a quem esta ao alcance da voz", heard[0].size() == 1 && heard[2].size() == 1 &&
                                                                  heard[0][0].speaker == 1 && heard[0][0].sequence == 1);
    t.check("voz: quem esta longe ou fala nao recebe", heard[1].empty() && heard[3].empty() && heard[4].empty() && heard[5].empty());

    for (auto& list : heard) list.clear();
    room.host.authority->sendVoice({7, 2, Bytes(40, 2)});
    for (int i = 0; i < 5; i++) { place(); room.step(); listen(); }
    t.check("voz: a fala do host segue o mesmo filtro", heard[1].size() == 1 && heard[2].size() == 1 && heard[3].empty() &&
                                                       heard[0].empty() && heard[1][0].speaker == 0);
    for (int i = 0; i < 9; i++) { place(); room.step(); }
    const auto states = room.clients[3]->authority->players();
    t.check("voz: quem fala aparece como falando", std::any_of(states.begin(), states.end(), [](const PlayerPublicState& p) {
        return p.id == 0 && p.speaking;
    }));

    // Todos juntos e todos falando: cada um ouve no maximo os outros 5, um fluxo por pessoa.
    for (auto& list : heard) list.clear();
    for (int frame = 0; frame < 6; frame++) {
        for (int i = 0; i < kMaxPlayers; i++) {
            room.host.authority->publishLocalPlayer(stateAt(0, {0, 50, 0}));
            if (i > 0) room.clients[i - 1]->authority->publishLocalPlayer(stateAt(i, {i * 5.0f, 50, 0}));
        }
        room.step();
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        const VoiceFrame frame{static_cast<PlayerId>(i), 9, Bytes(30, static_cast<uint8_t>(i))};
        if (i == 0) room.host.authority->sendVoice(frame);
        else room.clients[i - 1]->authority->sendVoice(frame);
    }
    for (int i = 0; i < 5; i++) { room.step(); listen(); }
    bool atMostFive = true, neverSelf = true, everyone = true;
    for (int i = 0; i < kMaxPlayers; i++) {
        std::set<PlayerId> speakers;
        for (const VoiceFrame& frame : heard[i]) {
            speakers.insert(frame.speaker);
            neverSelf = neverSelf && frame.speaker != i;
        }
        atMostFive = atMostFive && heard[i].size() <= kMaxPlayers - 1;
        everyone = everyone && speakers.size() == kMaxPlayers - 1;
    }
    t.check("voz: com 6 jogadores cada um recebe no maximo 5 fluxos", atMostFive && everyone);
    t.check("voz: ninguem recebe a propria voz", neverSelf);
}

// ---- Partida simulada com as regras reais ----

class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path_(std::filesystem::temp_directory_path() / ("birds-match-" + name + "-" + std::to_string(::getpid()))) {
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::error_code error; std::filesystem::remove_all(path_, error); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void testSimulatedMatch(TestRun& t) {
    const World world(kWorld);
    const NestSite nest = chooseNestSite(world, kWorld, 1);
    // Cajus de mentira no ceu, longe dos NPCs, para o resultado nao depender deles.
    std::vector<CashewSpot> spots;
    for (int i = 0; i < 4; i++) {
        CashewSpot spot;
        spot.position = nest.perch + glm::vec3(200.0f + i * 30.0f, 120.0f, 0);
        spots.push_back(spot);
    }
    TempDir hostDir("host"), clientDir("cliente");
    Room room(spots.size());
    Client& client = room.join({kWorld, "Cliente", 0});
    if (!ready(client)) {
        t.check("partida: cliente entra", false);
        return;
    }
    RehearsalPeer host(world, kWorld, CashewField(spots), hostDir.path(), room.host.authority, 0, false, room.host.authority.get());
    RehearsalPeer guest(world, kWorld, CashewField(spots), clientDir.path(), client.authority, 1, false);
    host.startProfile("Host");
    guest.startProfile("Cliente");
    guest.setGoal(3);
    for (int frame = 0; frame < 900 && !(guest.delivered() && frame > 30); frame++) {
        room.host.authority->update(kDt);
        client.authority->update(kDt);
        host.step(kDt);
        guest.step(kDt);
        std::this_thread::sleep_for(300us);
    }
    for (int frame = 0; frame < 20; frame++) {
        room.host.authority->update(kDt);
        client.authority->update(kDt);
        std::this_thread::sleep_for(300us);
        host.step(kDt);
        guest.step(kDt);
    }
    const int delivered = guest.session().view().totalDelivered;
    t.check("partida: o cliente coleta e entrega 3 cajus", delivered == 3 && guest.session().profile()->coins() >= 3);
    t.check("partida: o host confirmou as coletas do cliente", host.count(GameEventType::CashewCollected, 1) == 3 &&
                                                                guest.count(GameEventType::CashewCollected, 1) == 3);
    const auto deliveries = [](const RehearsalPeer& peer) {
        int total = 0;
        for (const GameEvent& e : peer.events())
            if (e.type == GameEventType::CashewDelivered && e.actor == 1) total += e.value;
        return total;
    };
    t.check("partida: o host ve a mesma entrega", deliveries(host) == 3 && deliveries(guest) == 3);
    bool sameField = true;
    for (CashewId id = 0; id < spots.size(); id++) sameField = sameField && host.field().isActive(id) == guest.field().isActive(id);
    t.check("partida: os cajus ficam iguais nos dois computadores", sameField && !host.field().isActive(0));
    t.check("partida: o host ve o cliente pousado no proprio ninho", [&] {
        for (const PlayerPublicState& p : host.authority().players())
            if (p.id == 1) return glm::length(p.position - nest.perch) < 1.0f && p.carried == 0 &&
                                  p.birdSeed == guest.session().profile()->birdSeed();
        return false;
    }());
    t.check("partida: o ninho do cliente nao e o do host", host.session().nest().tree != guest.session().nest().tree);
}

bool sameSite(const NestSite& a, const NestSite& b) {
    return a.tree == b.tree && a.base == b.base && a.perch == b.perch && a.yaw == b.yaw;
}

// Host e cliente em loopback, cada um com o proprio Tamanho: cada lado sabe onde e de que tamanho e o ninho do outro.
void testRivalNestsOverNetwork(TestRun& t) {
    const World world(kWorld);
    TempDir hostDir("ninho-host"), clientDir("ninho-cliente");
    ProfileStore(hostDir.path()).save(Profile::restored("Host", 11, 0, 0, {}, {0, 0, 0, 1}));
    ProfileStore(clientDir.path()).save(Profile::restored("Cliente", 22, 0, 0, {}, {0, 0, 0, 2}));
    Room room;
    Client& client = room.join({kWorld, "Cliente", 22});
    if (!ready(client)) {
        t.check("ninhos em rede: cliente entra", false);
        return;
    }
    RehearsalPeer host(world, kWorld, CashewField(world, kWorld), hostDir.path(), room.host.authority, 0, false,
                       room.host.authority.get());
    RehearsalPeer guest(world, kWorld, CashewField(world, kWorld), clientDir.path(), client.authority, 1, false);
    host.startProfile("Host");
    guest.startProfile("Cliente");
    const auto frame = [&](bool withGuest) {
        room.host.authority->update(kDt);
        if (withGuest) client.authority->update(kDt);
        else client.udp.poll();
        host.step(kDt);
        if (withGuest) guest.step(kDt);
        std::this_thread::sleep_for(300us);
    };
    for (int i = 0; i < 400 && !(host.session().rivalNests().find(1) && guest.session().rivalNests().find(0)); i++) frame(true);
    for (int i = 0; i < 20; i++) frame(true);

    const RivalNest* seenByHost = host.session().rivalNests().find(1);
    const RivalNest* seenByGuest = guest.session().rivalNests().find(0);
    t.check("ninhos em rede: o host sabe onde esta o ninho do cliente",
            seenByHost && sameSite(seenByHost->site, guest.session().nest()) && seenByHost->sizeLevel == 2);
    t.check("ninhos em rede: o cliente sabe onde esta o ninho do host",
            seenByGuest && sameSite(seenByGuest->site, host.session().nest()) && seenByGuest->sizeLevel == 1);
    t.check("ninhos em rede: cada lado so lista o ninho do outro",
            host.session().rivalNests().nests().size() == 1 && guest.session().rivalNests().nests().size() == 1);

    client.udp.leave();
    for (int i = 0; i < 300 && host.session().rivalNests().find(1); i++) frame(false);
    t.check("ninhos em rede: o ninho do cliente some quando ele sai", host.session().rivalNests().nests().empty());
}

GameEvent robOf(PlayerId owner) {
    GameEvent event = eventOf(GameEventType::NestRobbed, kNoPlayer);
    event.target = owner;
    return event;
}

void testRobberyOverNetwork(TestRun& t) {
    Room room;
    Client& thief = room.join({kWorld, "Ladra", 5});
    if (!ready(thief)) {
        t.check("roubo em rede: cliente entra", false);
        return;
    }
    const PlayerId me = thief.authority->localPlayer();
    PlayerPublicState owner = stateAt(0, glm::vec3(0, 100, 0));
    owner.nestStock = 3;
    owner.nestDefense = 1;
    room.host.authority->publishLocalPlayer(owner);
    thief.authority->publishLocalPlayer(stateAt(me, glm::vec3(5, 100, 0)));
    room.pump(20);

    thief.authority->request(robOf(0));
    const bool robbed = room.pumpUntil([&] { return countOf(thief.log, GameEventType::NestRobbed, me) == 1; }, 200);
    const auto& confirmed = room.host.log.back();
    t.check("roubo em rede: o host confirma e todos recebem",
            robbed && countOf(room.host.log, GameEventType::NestRobbed, me) == 1 && confirmed.target == 0 && confirmed.value == 1);
    t.check("roubo em rede: o host passa a contar o caju roubado na carga", [&] {
        thief.authority->request(eventOf(GameEventType::CashewDelivered, me, kNoCashew, 9));
        room.pump(20);
        return !thief.log.empty() && thief.log.back().type == GameEventType::CashewDelivered && thief.log.back().value == 1;
    }());

    thief.authority->request(robOf(0));
    room.pump(30);
    t.check("roubo em rede: roubar rapido demais e recusado", countOf(room.host.log, GameEventType::NestRobbed, me) == 1);
    room.pump(static_cast<int>(kMinRobInterval / kDt));
    thief.authority->request(robOf(me));
    room.pump(20);
    t.check("roubo em rede: ninguem rouba o proprio ninho", countOf(room.host.log, GameEventType::NestRobbed, me) == 1);
    owner.nestStock = 0;
    room.host.authority->publishLocalPlayer(owner);
    thief.authority->request(robOf(0));
    room.pump(20);
    t.check("roubo em rede: ninho vazio ou no Cofre nao e roubado", countOf(room.host.log, GameEventType::NestRobbed, me) == 1);
    owner.nestStock = 3;
    room.host.authority->publishLocalPlayer(owner);
    thief.authority->request(robOf(0));
    room.pump(20);
    t.check("roubo em rede: com moedas e tempo, rouba de novo", countOf(room.host.log, GameEventType::NestRobbed, me) == 2);

    // Pedidos que so o host pode fazer.
    GameEvent knock = eventOf(GameEventType::CashewDropped, me);
    knock.target = 0;
    thief.authority->request(knock);
    thief.authority->request(eventOf(GameEventType::CashewSpilled, me, kLooseCashewBase, 1));
    room.pump(20);
    t.check("roubo em rede: cliente nao derruba a carga dos outros nem cria cajus soltos",
            countOf(room.host.log, GameEventType::CashewDropped, me) == 0 &&
                countOf(room.host.log, GameEventType::CashewSpilled, me) == 0);
}

void testRobberyNeedsPerch(TestRun& t) {
    const glm::vec3 perch(500, 80, 500);
    Room room(100, [&](PlayerId) { return std::optional<glm::vec3>(perch); });
    Client& thief = room.join({kWorld, "Ladra", 5});
    if (!ready(thief)) {
        t.check("roubo em rede: cliente entra no teste do poleiro", false);
        return;
    }
    const PlayerId me = thief.authority->localPlayer();
    PlayerPublicState owner = stateAt(0, glm::vec3(0, 100, 0));
    owner.nestStock = 3;
    room.host.authority->publishLocalPlayer(owner);
    thief.authority->publishLocalPlayer(stateAt(me, perch + glm::vec3(200, 0, 0)));
    room.pump(20);
    thief.authority->request(robOf(0));
    room.pump(20);
    const bool farRefused = countOf(room.host.log, GameEventType::NestRobbed, me) == 0;
    thief.authority->publishLocalPlayer(stateAt(me, perch));
    room.pump(20);
    thief.authority->request(robOf(0));
    room.pump(20);
    t.check("roubo em rede: so rouba quem esta pousado no ninho",
            farRefused && countOf(room.host.log, GameEventType::NestRobbed, me) == 1);
}

void testRestoredWorld(TestRun& t) {
    Room room;
    room.host.authority->restoreCollected({4, 7, 5000});
    Client& guest = room.join({kWorld, "Ana", 1});
    room.pump(20);
    const auto synced = [&](CashewId id) {
        return std::any_of(guest.log.begin(), guest.log.end(), [&](const GameEvent& e) {
            return e.type == GameEventType::CashewCollected && e.cashew == id;
        });
    };
    t.check("mundo salvo: quem entra recebe os cajus ja colhidos", ready(guest) && synced(4) && synced(7) && !synced(5000));
    const PlayerId me = guest.authority ? guest.authority->localPlayer() : kNoPlayer;
    if (guest.authority) guest.authority->request(eventOf(GameEventType::CashewCollected, me, 4));
    room.host.authority->request(eventOf(GameEventType::CashewRespawned, 0, 7));
    room.pump(20);
    t.check("mundo salvo: caju colhido antes nao e colhido de novo, e volta quando o host decide",
            countOf(room.host.log, GameEventType::CashewCollected, me) == 0 && countOf(room.host.log, GameEventType::CashewRespawned, 0) == 1);
}

void testRosterProtocol(TestRun& t) {
    const Bytes bytes = encodeRoster({{0, "Ana"}, {4, "Bruno Ávila"}});
    const auto back = decodeRoster(bytes);
    t.check("nomes: a lista vai e volta igual", back && back->size() == 2 && (*back)[0].player == 0 &&
                                                   (*back)[0].name == "Ana" && (*back)[1].player == 4 && (*back)[1].name == "Bruno Ávila");
    const auto longName = decodeRoster(encodeRoster({{1, std::string(80, 'x')}}));
    t.check("nomes: nome longo chega cortado", longName && (*longName)[0].name.size() == kMaxNameLength);
    t.check("nomes: lista vazia e valida", decodeRoster(encodeRoster({})) && decodeRoster(encodeRoster({}))->empty());
    t.check("nomes: recusa mensagem cortada ou com sobra", rejectsTruncated(bytes, [](const Bytes& b) { return decodeRoster(b); }));
    t.check("nomes: recusa jogador fora da sala", !decodeRoster(encodeRoster({{kMaxPlayers, "X"}})));
    Bytes control = encodeRoster({{1, "Ana"}});
    control.back() = '\n';
    t.check("nomes: recusa caractere de controle", !decodeRoster(control));
    t.check("nomes: ruido nunca vira nome invalido", survivesNoise(bytes, [](const Bytes& b) { return decodeRoster(b); },
                                                                   [](const RosterMessage& r) {
                                                                       return r.size() <= kMaxPlayers && std::all_of(r.begin(), r.end(), [](const RosterEntry& e) {
                                                                           return e.player < kMaxPlayers && e.name.size() <= kMaxNameLength;
                                                                       });
                                                                   }));
}

void testNamesOverNetwork(TestRun& t) {
    Room room;
    room.host.authority->setLocalName("Anfitriã");
    Client& ana = room.join({kWorld, "Ana", 1});
    Client& bia = room.join({kWorld, "Bia", 2});
    if (!ready(ana) || !ready(bia)) {
        t.check("nomes em rede: clientes entram", false);
        return;
    }
    const PlayerId idAna = ana.authority->localPlayer(), idBia = bia.authority->localPlayer();
    const auto knowsAll = [&] {
        return ana.authority->playerName(0) == "Anfitriã" && ana.authority->playerName(idBia) == "Bia" &&
               bia.authority->playerName(idAna) == "Ana";
    };
    t.check("nomes em rede: cada um sabe o nome dos outros", room.pumpUntil(knowsAll, 200));
    t.check("nomes em rede: o host sabe o nome de todos",
            room.host.authority->playerName(0) == "Anfitriã" && room.host.authority->playerName(idAna) == "Ana" &&
                ana.authority->playerName(idAna) == "Ana");
    SharedAuthority shared(ana.authority);
    t.check("nomes em rede: a sessao ve os nomes pela autoridade compartilhada", shared.playerName(idBia) == "Bia");

    const auto identity = decodeIdentity(encodeIdentity({"Ana Clara", 99}));
    t.check("nomes: a troca de perfil vai e volta igual", identity && identity->name == "Ana Clara" && identity->birdSeed == 99 &&
                                                            !decodeIdentity(encodeRoster({})));
    ana.authority->setIdentity("Ana Clara", 99);
    t.check("nomes em rede: cliente que troca de perfil aparece com o nome novo para todos",
            room.pumpUntil([&] { return bia.authority->playerName(idAna) == "Ana Clara"; }, 200) &&
                room.host.authority->playerName(idAna) == "Ana Clara" && ana.authority->playerName(idAna) == "Ana Clara");

    room.host.authority->setLocalName("Host novo");
    t.check("nomes em rede: trocar de perfil atualiza o nome",
            room.pumpUntil([&] { return bia.authority->playerName(0) == "Host novo"; }, 200));
    bia.udp.leave();
    bia.paused = true;
    t.check("nomes em rede: quem sai perde o nome na lista",
            room.pumpUntil([&] { return ana.authority->playerName(idBia).empty(); }, 600));
}

void testDiveKnock(TestRun& t) {
    Room room;
    Client& prey = room.join({kWorld, "Presa", 7});
    if (!ready(prey)) {
        t.check("mergulho: cliente entra", false);
        return;
    }
    const PlayerId me = prey.authority->localPlayer();
    prey.authority->request(eventOf(GameEventType::CashewCollected, me, 10));
    prey.authority->request(eventOf(GameEventType::CashewCollected, me, 11));
    const glm::vec3 where(0, 120, 0);
    PlayerPublicState hunter = stateAt(0, where + glm::vec3(30, 0, 0));
    hunter.mode = FlightMode::Dive;
    room.host.authority->publishLocalPlayer(hunter);
    prey.authority->publishLocalPlayer(stateAt(me, where));   // Carrega 2 cajus.
    room.pump(40);
    t.check("mergulho: longe nao derruba ninguem", countOf(room.host.log, GameEventType::CashewDropped, 0) == 0);

    hunter.mode = FlightMode::Glide;
    hunter.position = where + glm::vec3(2, 0, 0);
    room.host.authority->publishLocalPlayer(hunter);
    room.pump(20);
    t.check("mergulho: encostar sem mergulhar nao derruba", countOf(room.host.log, GameEventType::CashewDropped, 0) == 0);

    hunter.mode = FlightMode::Dive;
    room.host.authority->publishLocalPlayer(hunter);
    room.pumpUntil([&] { return countOf(prey.log, GameEventType::CashewSpilled, 0) == 2; }, 200);
    const auto drop = std::find_if(prey.log.begin(), prey.log.end(), [](const GameEvent& e) { return e.type == GameEventType::CashewDropped; });
    t.check("mergulho: derruba toda a carga de quem leva cajus",
            drop != prey.log.end() && drop->actor == 0 && drop->target == me && drop->value == 2);
    t.check("mergulho: os cajus caem no mundo para todos",
            countOf(prey.log, GameEventType::CashewSpilled, 0) == 2 && countOf(room.host.log, GameEventType::CashewSpilled, 0) == 2);
    room.pump(60);
    t.check("mergulho: a carga cai uma unica vez", countOf(room.host.log, GameEventType::CashewDropped, 0) == 1);

    Client& late = room.join({kWorld, "Atrasado", 8});
    room.pump(20);
    t.check("caju solto: quem entra depois ve os cajus no chao",
            ready(late) && std::count_if(late.log.begin(), late.log.end(), [](const GameEvent& e) {
                return e.type == GameEventType::CashewSpilled;
            }) == 2);

    const CashewId loose = drop != prey.log.end() ? std::next(drop)->cashew : kNoCashew;
    late.authority->request(eventOf(GameEventType::CashewCollected, late.authority->localPlayer(), loose));
    prey.authority->request(eventOf(GameEventType::CashewCollected, me, loose));
    room.pump(30);
    const auto takers = std::count_if(room.host.log.begin(), room.host.log.end(), [&](const GameEvent& e) {
        return e.type == GameEventType::CashewCollected && e.cashew == loose;
    });
    t.check("caju solto: o host entrega cada caju a um so jogador", isLooseCashew(loose) && takers == 1);
}
} // namespace

// Codifica uma amostra fixa de cada mensagem e resume os bytes. Se o formato mudar, o resumo muda.
uint32_t wireFingerprint() {
    PlayerPublicState player;
    player.id = 2;
    player.position = {1.5f, 2.25f, -3.0f};
    player.velocity = {4.0f, -0.5f, 0.25f};
    player.yaw = 0.5f;
    player.pitch = -0.25f;
    player.roll = 0.125f;
    player.mode = FlightMode::Flap;
    player.carried = 3;
    player.speaking = true;
    player.birdSeed = 123456789u;
    player.nestLevel = 2;
    player.nestDefense = 4;
    player.nestStock = 17;
    NpcPublicState npc;
    npc.seed = 987654321u;
    npc.position = {10.0f, 20.0f, 30.0f};
    npc.yaw = 1.0f;
    npc.mode = FlightMode::Dive;
    npc.role = 3;
    npc.perched = true;
    SnapshotMessage snapshot;
    snapshot.time = 12.5f;
    snapshot.players = {player};
    snapshot.npcs = {npc};
    GameEvent event = eventOf(GameEventType::CashewDelivered, 1, 40, 3);
    event.target = 2;
    event.position = {5.0f, 6.0f, 7.0f};
    std::vector<Bytes> all = {encodeHello({kProtocolVersion, kWorld, "Ana", 99}), encodeWelcome({kProtocolVersion, kWorld, 3}),
                              encodeRefuse(RefuseReason::Full), encodeEvents(MessageType::EventConfirmed, {event}),
                              encodeState({2.0f, player}), encodeSnapshot(snapshot), encodeVoice({2, 7, Bytes(5, 9)}),
                              encodeRoster({{0, "Ana"}, {3, "Bia"}}), encodeIdentity({"Caio", 77})};
    uint32_t hash = 2166136261u;
    for (const Bytes& bytes : all)
        for (uint8_t b : bytes) hash = (hash ^ b) * 16777619u;
    return hash;
}

void testWireFormatLocked(TestRun& t) {
    constexpr uint32_t kVersion6Fingerprint = 2961360089u;
    const uint32_t fingerprint = wireFingerprint();
    if (fingerprint != kVersion6Fingerprint) std::cerr << "Resumo do formato atual: " << fingerprint << "\n";
    t.check("protocolo: formato fixo da versao 6 (mudou o formato? aumente kProtocolVersion e atualize o resumo)",
            kProtocolVersion == 6 && fingerprint == kVersion6Fingerprint);
}

// Um host de outra versao que conecta, mas manda um estado que o cliente nao consegue ler.
void testUnreadableHost(TestRun& t) {
    const uint16_t port = freePort();
    UdpSession hostUdp(port), clientUdp(port);
    hostUdp.host("Host antigo");
    clientUdp.startJoin("127.0.0.1:" + std::to_string(port));
    for (int i = 0; i < 300 && clientUdp.joinStatus() == JoinStatus::Pending; i++) {
        hostUdp.poll();
        clientUdp.poll();
        std::this_thread::sleep_for(1ms);
    }
    ClientAuthority client(clientUdp, {kWorld, "Novo", 1});
    bool welcomed = false;
    Bytes oldSnapshot = {kProtocolVersion, static_cast<uint8_t>(MessageType::Snapshot), 1, 2, 3};
    for (int i = 0; i < 400 && client.status() != ClientStatus::Refused; i++) {
        hostUdp.poll();
        for (const auto& message : hostUdp.pollMessages())
            if (!welcomed && decodeHello(message.payload)) {
                hostUdp.sendReliable(message.from, encodeWelcome({kProtocolVersion, kWorld, 1}));
                welcomed = true;
            }
        if (welcomed) hostUdp.sendUnreliable(1, oldSnapshot);
        client.update(kDt);
        std::this_thread::sleep_for(1ms);
    }
    t.check("rede: host com estado ilegivel e recusado com aviso de versao",
            welcomed && client.status() == ClientStatus::Refused && client.error().find("versão") != std::string::npos);
}

// O host desta maquina responde pelo broadcast e pelo loopback; a lista deve mostrar uma sala so.
void testDiscoveryDedup(TestRun& t) {
    Room room;
    UdpSession finder(room.port);
    std::vector<RoomInfo> found;
    for (int i = 0; i < 120; i++) {
        found = finder.discoverRooms();
        room.step();
    }
    const auto same = std::count_if(found.begin(), found.end(), [](const RoomInfo& r) { return r.name == "Sala de teste"; });
    t.check("salas: o host deste computador aparece uma unica vez", same == 1);
    t.check("salas: a sala local usa o endereco 127.0.0.1",
            same == 1 && std::find_if(found.begin(), found.end(), [](const RoomInfo& r) {
                return r.name == "Sala de teste" && r.address.rfind("127.0.0.1:", 0) == 0; }) != found.end());
}

// Voz de ponta a ponta: microfone simulado no host, rede real em loopback e o som saindo no cliente.
float voiceEnergy(Room& room, Client& client, VoiceChat& speaker, VoiceChat& listener, glm::vec3 listenerAt, int frames) {
    const glm::vec3 speakerAt(0, 100, 0);
    std::vector<float> stereo(2 * kVoiceFrame);
    double energy = 0;
    size_t sample = 0;
    uint32_t noise = 12345;
    for (int i = 0; i < frames; i++) {
        std::vector<float> mic(kVoiceFrame);
        // Meio segundo de ruido baixo para a deteccao aprender o fundo, e depois a fala.
        for (float& value : mic) {
            noise = noise * 1664525u + 1013904223u;
            value = 0.002f * (static_cast<float>(noise >> 8) / 8388608.0f - 1.0f);
            if (i >= 25) value += 0.3f * static_cast<float>(std::sin(2.0 * M_PI * 300.0 * sample / kVoiceRate));
            sample++;
        }
        speaker.feedMicrophone(mic.data(), mic.size());
        room.host.authority->publishLocalPlayer(stateAt(0, speakerAt));
        client.authority->publishLocalPlayer(stateAt(client.authority->localPlayer(), listenerAt));
        std::vector<PlayerPublicState> heardFrom;
        for (const auto& player : client.authority->players())
            if (player.id != client.authority->localPlayer()) heardFrom.push_back(player);
        speaker.update(0.02f, {speakerAt, glm::vec3(0), 0.0f}, {});
        listener.update(0.02f, {listenerAt, glm::vec3(0), 0.0f}, heardFrom);
        listener.renderOutput(stereo.data());
        if (i >= frames / 2)
            for (float value : stereo) energy += value * value;
        room.step(0.02f);
    }
    return static_cast<float>(std::sqrt(energy / (frames / 2 * stereo.size())));
}

void testVoiceOverNetwork(TestRun& t) {
    Room room;
    Client& client = room.join({kWorld, "Ouvinte", 9});
    if (!ready(client)) {
        t.check("voz em rede: cliente entra na sala", false);
        return;
    }
    VoiceSettings settings;
    settings.useDevices = false;
    VoiceChat speaker(settings), listener(settings);
    speaker.start();
    listener.start();
    speaker.attach(room.host.authority.get(), 0);
    listener.attach(client.authority.get(), client.authority->localPlayer());

    const float near = voiceEnergy(room, client, speaker, listener, {10, 100, 0}, 150);
    const bool heardSpeaking = listener.speaking(0);
    t.check("voz em rede: quem esta perto ouve o host", near > 0.02f && heardSpeaking);
    t.check("voz em rede: falar marca o host como falando", speaker.localSpeaking() && speaker.stats().framesSent > 50);
    const float far = voiceEnergy(room, client, speaker, listener, {600, 100, 0}, 150);
    t.check("voz em rede: longe a voz some", far < near * 0.05f);
}

int runMatchSelfTest() {
    TestRun test;
    testProtocol(test);
    testWireFormatLocked(test);
    testUnreadableHost(test);
    testInterpolation(test);
    testJoinAndCapacity(test);
    testRefusals(test);
    testOrderAndDispute(test);
    testSnapshotFlow(test);
    testPlayerLeft(test);
    testVoiceRelay(test);
    testDiscoveryDedup(test);
    testVoiceOverNetwork(test);
    testSimulatedMatch(test);
    testRivalNestsOverNetwork(test);
    testRobberyOverNetwork(test);
    testRobberyNeedsPerch(test);
    testDiveKnock(test);
    testRosterProtocol(test);
    testRestoredWorld(test);
    testNamesOverNetwork(test);
    return test.failures;
}

#include "net/HostAuthority.h"
#include <algorithm>
#include <iostream>

namespace net {
namespace {
constexpr PlayerId kHostPlayer = 0;

GameEvent eventOf(GameEventType type, PlayerId actor) {
    GameEvent event;
    event.type = type;
    event.actor = actor;
    return event;
}
}

HostAuthority::HostAuthority(UdpSession& udp, HostConfig config) : udp_(udp), config_(std::move(config)) {
    lastRobAt_.fill(-1.0e9);
}

// ---- GameAuthority ----

void HostAuthority::request(const GameEvent& event) { confirm(event, false, kHostPlayer); }

std::vector<GameEvent> HostAuthority::pollEvents() {
    std::vector<GameEvent> events;
    events.swap(pending_);
    return events;
}

std::vector<PlayerPublicState> HostAuthority::players() const {
    std::vector<PlayerPublicState> list;
    if (hasLocal_) list.push_back(local_);
    for (PlayerId id = 1; id < kMaxPlayers; id++) {
        const Remote& remote = remotes_[id];
        if (!remote.joined || remote.track.empty()) continue;
        PlayerPublicState state = remote.track.sample(now_);
        state.id = id;
        state.speaking = state.speaking || now_ < remote.speakingUntil;
        list.push_back(state);
    }
    return list;
}

void HostAuthority::publishLocalPlayer(const PlayerPublicState& state) {
    local_ = state;
    local_.id = kHostPlayer;
    local_.speaking = state.speaking || now_ < localSpeakingUntil_;
    hasLocal_ = true;
}

std::vector<PlayerId> HostAuthority::remotePlayers() const {
    std::vector<PlayerId> ids;
    for (PlayerId id = 1; id < kMaxPlayers; id++)
        if (remotes_[id].joined) ids.push_back(id);
    return ids;
}

std::string HostAuthority::playerName(PlayerId id) const {
    return id < kMaxPlayers && remotes_[id].joined ? remotes_[id].name : "";
}

// ---- Regras ----

// O host nunca confia no cliente: o autor e sempre quem enviou, e alguns eventos so o host cria.
bool HostAuthority::validate(GameEvent& event, bool remote, PlayerId sender) {
    if (remote) {
        if (event.type == GameEventType::CashewRespawned || event.type == GameEventType::PlayerJoined ||
            event.type == GameEventType::PlayerLeft || event.type == GameEventType::CashewSpilled) return false;
        // Um cliente so derruba a propria carga (batida em arvore); mergulhos e cacadores sao decididos aqui.
        if (event.type == GameEventType::CashewDropped && event.target != sender) return false;
        event.actor = sender;
    }
    switch (event.type) {
        case GameEventType::CashewCollected:
            // So o primeiro pedido leva o caju; os seguintes chegam com ele ja coletado.
            if (isLooseCashew(event.cashew)) {
                if (!spill_.take(event.cashew)) return false;
            } else if (event.cashew >= config_.cashewCount || !collected_.insert(event.cashew).second) {
                return false;
            }
            if (event.actor < kMaxPlayers && carried_[event.actor] < 255) carried_[event.actor]++;
            return true;
        case GameEventType::CashewRespawned:
            return collected_.erase(event.cashew) > 0;
        case GameEventType::CashewDelivered:
            // Ninguem entrega mais do que coletou. O pedido e confirmado mesmo zerado, para liberar o cliente.
            if (event.actor >= kMaxPlayers) return false;
            event.value = std::min(event.value, carried_[event.actor]);
            carried_[event.actor] = static_cast<uint8_t>(carried_[event.actor] - event.value);
            return true;
        case GameEventType::CashewDropped:
            if (event.target >= kMaxPlayers) return false;
            event.value = std::min(event.value, carried_[event.target]);
            carried_[event.target] = static_cast<uint8_t>(carried_[event.target] - event.value);
            return event.value > 0;
        case GameEventType::NestRobbed:
            if (!validateRobbery(event)) return false;
            lastRobAt_[event.actor] = now_;
            event.value = 1;
            if (carried_[event.actor] < 255) carried_[event.actor]++;
            return true;
        case GameEventType::CashewSpilled:
            return false;   // So nasce em confirm, a partir de um CashewDropped.
        default:
            return true;
    }
}

// O ladrao esta pousado no ninho de outro jogador conectado, que ainda tem moedas fora do Cofre.
bool HostAuthority::validateRobbery(const GameEvent& event) {
    if (event.actor >= kMaxPlayers || event.target >= kMaxPlayers || event.actor == event.target) return false;
    PlayerPublicState thief, owner;
    if (!stateOf(event.actor, thief) || !stateOf(event.target, owner) || owner.nestStock == 0) return false;
    if (now_ - lastRobAt_[event.actor] < kMinRobInterval) return false;
    if (config_.nestPerch) {
        const auto perch = config_.nestPerch(event.target);
        if (!perch || glm::length(thief.position - *perch) > kRobRange) return false;
    }
    return true;
}

bool HostAuthority::stateOf(PlayerId id, PlayerPublicState& state) const {
    if (id == kHostPlayer) {
        state = local_;
        return hasLocal_;
    }
    if (id >= kMaxPlayers || !remotes_[id].joined || !remotes_[id].hasState) return false;
    state = current(id);
    return true;
}

// Quem mergulha e encosta em outro jogador com carga derruba tudo o que ele leva.
// Pousado no ninho o jogador publica carga zero e fica imune.
void HostAuthority::checkDiveHits() {
    std::vector<PlayerPublicState> birds;
    for (PlayerId id = 0; id < kMaxPlayers; id++) {
        PlayerPublicState state;
        if (stateOf(id, state)) birds.push_back(state);
    }
    for (const PlayerPublicState& diver : birds) {
        if (diver.mode != FlightMode::Dive) continue;
        for (const PlayerPublicState& prey : birds) {
            if (prey.id == diver.id || prey.carried == 0 || carried_[prey.id] == 0 || now_ < knockableAt_[prey.id]) continue;
            if (glm::length(prey.position - diver.position) > kDiveHitRange) continue;
            knockableAt_[prey.id] = now_ + kKnockImmunity;
            GameEvent hit = eventOf(GameEventType::CashewDropped, diver.id);
            hit.target = prey.id;
            hit.value = carried_[prey.id];
            hit.position = prey.position;
            confirm(hit, false, kHostPlayer);
        }
    }
}

void HostAuthority::confirm(GameEvent event, bool remote, PlayerId sender) {
    if (!validate(event, remote, sender)) return;
    pending_.push_back(event);
    outgoing_.push_back(event);
    if (event.type != GameEventType::CashewDropped) return;
    for (const GameEvent& spilled : spill_.spill(event)) {
        pending_.push_back(spilled);
        outgoing_.push_back(spilled);
    }
}

void HostAuthority::flushConfirmed() {
    if (outgoing_.empty()) return;
    const auto batches = encodeEventBatches(MessageType::EventConfirmed, outgoing_);
    outgoing_.clear();
    for (PlayerId id : remotePlayers())
        for (const auto& batch : batches) udp_.sendReliable(id, batch);
}

// ---- Rede ----

void HostAuthority::update(float seconds) {
    now_ += std::max(0.0f, seconds);
    udp_.poll();
    for (const Message& message : udp_.pollMessages()) handleReliable(message);
    for (const Message& message : udp_.pollDatagrams()) handleDatagram(message);

    const std::vector<PeerId> connected = udp_.peers();
    for (PlayerId id : remotePlayers())
        if (std::find(connected.begin(), connected.end(), id) == connected.end()) dropPlayer(id);

    spill_.advance(seconds);
    checkDiveHits();
    flushConfirmed();
    snapshotTimer_ += std::max(0.0f, seconds);
    if (snapshotTimer_ >= kStateInterval) {
        snapshotTimer_ = std::min(snapshotTimer_ - kStateInterval, kStateInterval);
        sendSnapshot();
    }
}

void HostAuthority::handleReliable(const Message& message) {
    const PlayerId sender = message.from;
    if (sender == kHostPlayer || sender >= kMaxPlayers) return;
    const auto header = peekHeader(message.payload);
    if (!header) return;
    if (header->type == MessageType::Hello) {
        if (const auto hello = decodeHello(message.payload)) welcome(sender, *hello);
        return;
    }
    if (header->type != MessageType::EventRequest || !remotes_[sender].joined) return;
    if (const auto events = decodeEvents(MessageType::EventRequest, message.payload))
        for (const GameEvent& event : *events) confirm(event, true, sender);
}

void HostAuthority::welcome(PeerId peer, const HelloMessage& hello) {
    if (hello.version != config_.version) {
        udp_.sendReliable(peer, encodeRefuse(RefuseReason::Version));
        return;
    }
    if (hello.worldSeed != config_.worldSeed) {
        udp_.sendReliable(peer, encodeRefuse(RefuseReason::WorldSeed));
        return;
    }
    // Um Hello repetido no mesmo id e outro computador que herdou a vaga de quem caiu.
    if (remotes_[peer].joined) dropPlayer(peer);
    // O novo jogador recebe o mundo como ele esta antes de qualquer evento novo.
    flushConfirmed();

    udp_.sendReliable(peer, encodeWelcome({config_.version, config_.worldSeed, peer}));
    std::vector<GameEvent> sync;
    sync.push_back(eventOf(GameEventType::PlayerJoined, kHostPlayer));
    for (PlayerId id : remotePlayers()) sync.push_back(eventOf(GameEventType::PlayerJoined, id));
    for (CashewId cashew : collected_) {
        GameEvent event = eventOf(GameEventType::CashewCollected, kNoPlayer);
        event.cashew = cashew;
        event.value = 1;
        sync.push_back(event);
    }
    for (const GameEvent& loose : spill_.activeSpills()) sync.push_back(loose);
    for (const auto& batch : encodeEventBatches(MessageType::EventConfirmed, sync)) udp_.sendReliable(peer, batch);

    Remote& remote = remotes_[peer];
    remote = Remote{};
    remote.joined = true;
    remote.name = hello.name;
    remote.birdSeed = hello.birdSeed;
    carried_[peer] = 0;
    std::cout << "Rede: " << (hello.name.empty() ? "jogador" : hello.name) << " entrou como jogador "
              << static_cast<int>(peer) << "\n";
    confirm(eventOf(GameEventType::PlayerJoined, peer), false, kHostPlayer);
}

void HostAuthority::dropPlayer(PlayerId id) {
    remotes_[id] = Remote{};
    carried_[id] = 0;
    lastRobAt_[id] = -1.0e9;
    std::cout << "Rede: jogador " << static_cast<int>(id) << " saiu\n";
    confirm(eventOf(GameEventType::PlayerLeft, id), false, kHostPlayer);
}

void HostAuthority::handleDatagram(const Message& message) {
    const PlayerId sender = message.from;
    if (sender == kHostPlayer || sender >= kMaxPlayers || !remotes_[sender].joined) return;
    const auto header = peekHeader(message.payload);
    if (!header) return;
    Remote& remote = remotes_[sender];
    if (header->type == MessageType::PlayerState) {
        auto state = decodeState(message.payload);
        if (!state) return;
        state->state.id = sender;
        remote.latest = state->state;
        remote.latestAt = now_;
        remote.hasState = true;
        remote.track.push(state->time, now_, state->state);
    } else if (header->type == MessageType::Voice) {
        auto frame = decodeVoice(message.payload);
        if (!frame) return;
        frame->speaker = sender;
        remote.speakingUntil = now_ + kSpeakingHold;
        relayVoice(*frame);
    }
}

PlayerPublicState HostAuthority::current(PlayerId id) const {
    const Remote& remote = remotes_[id];
    PlayerPublicState state = remote.latest;
    const float age = static_cast<float>(std::min(now_ - remote.latestAt, kMaxExtrapolation));
    state.position += state.velocity * age;
    state.speaking = state.speaking || now_ < remote.speakingUntil;
    return state;
}

// O estado de cada cliente e avancado ate agora, para quem recebe interpolar sem tremer.
void HostAuthority::sendSnapshot() {
    const std::vector<PlayerId> ids = remotePlayers();
    if (ids.empty()) return;
    SnapshotMessage snapshot;
    snapshot.time = static_cast<float>(now_);
    if (hasLocal_) snapshot.players.push_back(local_);
    for (PlayerId id : ids)
        if (remotes_[id].hasState) snapshot.players.push_back(current(id));
    snapshot.npcs = npcs_;
    const auto bytes = encodeSnapshot(snapshot);
    for (PlayerId id : ids) udp_.sendUnreliable(id, bytes);
}

// ---- Voz ----

bool HostAuthority::positionOf(PlayerId id, glm::vec3& position) const {
    if (id == kHostPlayer) {
        position = local_.position;
        return hasLocal_;
    }
    if (id >= kMaxPlayers || !remotes_[id].joined || !remotes_[id].hasState) return false;
    position = current(id).position;
    return true;
}

// Cada ouvinte recebe no maximo um fluxo por outro jogador, nunca a propria voz.
void HostAuthority::relayVoice(const VoiceFrame& frame) {
    glm::vec3 speaker(0);
    if (!positionOf(frame.speaker, speaker)) return;
    const auto inRange = [&](PlayerId listener) {
        glm::vec3 position(0);
        return listener != frame.speaker && positionOf(listener, position) &&
               glm::length(position - speaker) <= kVoiceRange;
    };
    const auto bytes = encodeVoice(frame);
    for (PlayerId id : remotePlayers())
        if (inRange(id)) udp_.sendUnreliable(id, bytes);
    if (inRange(kHostPlayer)) {
        voice_.push_back(frame);
        while (voice_.size() > kMaxVoiceQueue) voice_.pop_front();
    }
}

void HostAuthority::sendVoice(const VoiceFrame& frame) {
    VoiceFrame own = frame;
    own.speaker = kHostPlayer;
    if (own.payload.empty() || own.payload.size() > kMaxVoicePayload) return;
    localSpeakingUntil_ = now_ + kSpeakingHold;
    local_.speaking = true;
    relayVoice(own);
}

std::vector<VoiceFrame> HostAuthority::receiveVoice() {
    std::vector<VoiceFrame> frames(voice_.begin(), voice_.end());
    voice_.clear();
    return frames;
}

} // namespace net

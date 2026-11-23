#include "net/ClientAuthority.h"
#include <algorithm>

namespace net {

namespace {
constexpr int kUnreadableSnapshotLimit = 40;   // Cerca de 2 s de snapshots que nao se consegue ler.
}

std::string refuseMessage(RefuseReason reason) {
    switch (reason) {
        case RefuseReason::Full: return "A sala está cheia.";
        case RefuseReason::Version: return "O host usa outra versão do jogo. Atualizem os dois para a mesma versão.";
        case RefuseReason::WorldSeed: return "O host usa outra ilha (semente do mundo diferente).";
    }
    return "O host recusou a entrada.";
}

ClientAuthority::ClientAuthority(UdpSession& udp, ClientConfig config) : udp_(udp), config_(std::move(config)) {
    HelloMessage hello;
    hello.version = config_.version;
    hello.worldSeed = config_.worldSeed;
    hello.name = config_.name;
    hello.birdSeed = config_.birdSeed;
    if (!udp_.connected() || !udp_.sendReliable(kHostPeer, encodeHello(hello))) {
        status_ = ClientStatus::Disconnected;
        error_ = "Sem conexão com o host.";
    }
}

// ---- GameAuthority ----

void ClientAuthority::request(const GameEvent& event) {
    if (status_ == ClientStatus::Ready || status_ == ClientStatus::Connecting) requests_.push_back(event);
}

std::vector<GameEvent> ClientAuthority::pollEvents() {
    std::vector<GameEvent> events;
    events.swap(pending_);
    return events;
}

std::vector<PlayerPublicState> ClientAuthority::players() const {
    std::vector<PlayerPublicState> list;
    if (hasLocal_) list.push_back(local_);
    for (PlayerId id = 0; id < kMaxPlayers; id++) {
        if (!present_[id] || id == localPlayer_ || players_[id].empty()) continue;
        PlayerPublicState state = players_[id].sample(now_);
        state.id = id;
        list.push_back(state);
    }
    return list;
}

void ClientAuthority::publishLocalPlayer(const PlayerPublicState& state) {
    local_ = state;
    local_.id = localPlayer_;
    local_.speaking = state.speaking || now_ < speakingUntil_;
    hasLocal_ = true;
}

void ClientAuthority::setIdentity(const std::string& name, uint32_t birdSeed) {
    if (name.empty() || (name == config_.name && birdSeed == config_.birdSeed)) return;
    config_.name = name;
    config_.birdSeed = birdSeed;
    if (status_ == ClientStatus::Ready) udp_.sendReliable(kHostPeer, encodeIdentity({name, birdSeed}));
}

std::string ClientAuthority::playerName(PlayerId id) const {
    if (id == localPlayer_) return config_.name;
    return id < kMaxPlayers ? names_[id] : "";
}

std::vector<NpcPublicState> ClientAuthority::npcs() const {
    std::vector<NpcPublicState> list;
    for (size_t i = 0; i < npcs_.size() && i < lastNpcs_.size(); i++) {
        if (npcs_[i].empty()) continue;
        const PlayerPublicState state = npcs_[i].sample(now_);
        list.push_back({lastNpcs_[i].seed, state.position, state.yaw, state.pitch, state.roll, state.mode,
                        state.carried, state.speaking});
    }
    return list;
}

// ---- Rede ----

void ClientAuthority::refuse(const std::string& reason) {
    status_ = ClientStatus::Refused;
    error_ = reason;
    requests_.clear();
}

void ClientAuthority::update(float seconds) {
    now_ += std::max(0.0f, seconds);
    if (status_ == ClientStatus::Refused || status_ == ClientStatus::Disconnected) return;
    udp_.poll();
    for (const Message& message : udp_.pollMessages()) handleReliable(message);
    for (const Message& message : udp_.pollDatagrams()) {
        if (message.from != kHostPeer || status_ != ClientStatus::Ready) continue;
        const auto header = peekHeader(message.payload);
        if (!header) continue;
        if (header->type == MessageType::Snapshot) {
            if (const auto snapshot = decodeSnapshot(message.payload)) {
                handleSnapshot(*snapshot);
                unreadableSnapshots_ = 0;
                readSnapshot_ = true;
            } else if (!readSnapshot_ && ++unreadableSnapshots_ >= kUnreadableSnapshotLimit) {
                // Conectou, mas nao entende o estado do host: e outra versao do jogo. Melhor sair com o motivo
                // do que ficar na sala sem ver ninguem.
                refuse(refuseMessage(RefuseReason::Version));
            }
        } else if (header->type == MessageType::Voice) {
            const auto frame = decodeVoice(message.payload);
            if (!frame || frame->speaker == localPlayer_) continue;
            voice_.push_back(*frame);
            while (voice_.size() > kMaxVoiceQueue) voice_.pop_front();
        }
    }
    if (status_ == ClientStatus::Refused) return;
    if (!udp_.connected()) {
        status_ = ClientStatus::Disconnected;
        error_ = "A conexão com o host caiu.";
        return;
    }
    if (status_ != ClientStatus::Ready) return;

    for (const auto& batch : encodeEventBatches(MessageType::EventRequest, requests_)) udp_.sendReliable(kHostPeer, batch);
    requests_.clear();
    stateTimer_ += std::max(0.0f, seconds);
    if (stateTimer_ >= kStateInterval) {
        stateTimer_ = std::min(stateTimer_ - kStateInterval, kStateInterval);
        sendState();
    }
}

void ClientAuthority::handleReliable(const Message& message) {
    if (message.from != kHostPeer) return;
    const auto header = peekHeader(message.payload);
    if (!header) return;
    switch (header->type) {
        case MessageType::Refuse:
            if (const auto reason = decodeRefuse(message.payload)) refuse(refuseMessage(*reason));
            else refuse("O host recusou a entrada.");
            return;
        case MessageType::Welcome: {
            const auto welcome = decodeWelcome(message.payload);
            if (status_ != ClientStatus::Connecting || !welcome) return;
            if (welcome->version != config_.version) return refuse(refuseMessage(RefuseReason::Version));
            if (welcome->worldSeed != config_.worldSeed) return refuse(refuseMessage(RefuseReason::WorldSeed));
            hostWorldSeed_ = welcome->worldSeed;
            localPlayer_ = welcome->player;
            local_.id = localPlayer_;
            status_ = ClientStatus::Ready;
            return;
        }
        case MessageType::EventConfirmed: {
            if (status_ != ClientStatus::Ready) return;
            const auto events = decodeEvents(MessageType::EventConfirmed, message.payload);
            if (!events) return;
            for (const GameEvent& event : *events) {
                if (event.type == GameEventType::PlayerLeft && event.actor < kMaxPlayers) {
                    present_[event.actor] = false;
                    players_[event.actor] = RemoteTrack{};
                }
                pending_.push_back(event);
            }
            return;
        }
        case MessageType::Roster: {
            const auto roster = decodeRoster(message.payload);
            if (status_ != ClientStatus::Ready || !roster) return;
            names_.fill("");
            for (const RosterEntry& entry : *roster) names_[entry.player] = entry.name;
            return;
        }
        default:
            return;
    }
}

void ClientAuthority::handleSnapshot(const SnapshotMessage& snapshot) {
    std::array<bool, kMaxPlayers> seen{};
    for (const PlayerPublicState& state : snapshot.players) {
        seen[state.id] = true;
        if (state.id != localPlayer_) players_[state.id].push(snapshot.time, now_, state);
    }
    for (PlayerId id = 0; id < kMaxPlayers; id++) {
        if (!seen[id]) players_[id] = RemoteTrack{};
        present_[id] = seen[id];
    }

    // Os NPCs nao trazem velocidade: ela sai da diferenca entre dois snapshots seguidos.
    if (snapshot.npcs.size() != lastNpcs_.size()) {
        npcs_.assign(snapshot.npcs.size(), RemoteTrack{});
        lastNpcs_.clear();
    }
    const float span = lastNpcs_.empty() ? 0.0f : snapshot.time - lastSnapshotTime_;
    if (!lastNpcs_.empty() && span <= 0) return;
    for (size_t i = 0; i < snapshot.npcs.size(); i++) {
        const NpcPublicState& npc = snapshot.npcs[i];
        PlayerPublicState state;
        state.id = static_cast<PlayerId>(i);
        state.position = npc.position;
        if (span > 0) state.velocity = (npc.position - lastNpcs_[i].position) / span;
        state.yaw = npc.yaw;
        state.pitch = npc.pitch;
        state.roll = npc.roll;
        state.mode = npc.mode;
        state.birdSeed = npc.seed;
        // Papel e pouso sao discretos: vao em campos que a interpolacao copia do estado mais proximo.
        state.carried = npc.role;
        state.speaking = npc.perched;
        npcs_[i].push(snapshot.time, now_, state);
    }
    lastNpcs_ = snapshot.npcs;
    lastSnapshotTime_ = snapshot.time;
}

void ClientAuthority::sendState() {
    if (!hasLocal_) return;
    StateMessage message;
    message.time = static_cast<float>(now_);
    message.state = local_;
    message.state.speaking = local_.speaking || now_ < speakingUntil_;
    udp_.sendUnreliable(kHostPeer, encodeState(message));
}

// ---- Voz ----

void ClientAuthority::sendVoice(const VoiceFrame& frame) {
    if (status_ != ClientStatus::Ready || frame.payload.empty() || frame.payload.size() > kMaxVoicePayload) return;
    VoiceFrame own = frame;
    own.speaker = localPlayer_;
    speakingUntil_ = now_ + kSpeakingHold;
    local_.speaking = true;
    udp_.sendUnreliable(kHostPeer, encodeVoice(own));
}

std::vector<VoiceFrame> ClientAuthority::receiveVoice() {
    std::vector<VoiceFrame> frames(voice_.begin(), voice_.end());
    voice_.clear();
    return frames;
}

} // namespace net

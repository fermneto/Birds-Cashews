#include "net/Protocol.h"
#include "net/UdpSession.h"
#include <cmath>
#include <cstring>

namespace net {
namespace {
using Bytes = std::vector<uint8_t>;

constexpr float kMaxCoordinate = 100000.0f;   // Bem alem do mar: nada valido passa disso.
constexpr float kMaxSpeed = 1000.0f;
constexpr float kMaxAngle = 1000.0f;
constexpr size_t kEventSize = 18;
constexpr size_t kPlayerSize = 47;
constexpr size_t kNpcSize = 30;

class Writer {
public:
    Writer(MessageType type, uint8_t version = kProtocolVersion) { bytes_ = {version, static_cast<uint8_t>(type)}; }
    void u8(uint8_t value) { bytes_.push_back(value); }
    void u16(uint16_t value) { u8(static_cast<uint8_t>(value >> 8)); u8(static_cast<uint8_t>(value)); }
    void u32(uint32_t value) { for (int shift = 24; shift >= 0; shift -= 8) u8(static_cast<uint8_t>(value >> shift)); }
    void f32(float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    void vec3(glm::vec3 value) { f32(value.x); f32(value.y); f32(value.z); }
    void raw(const uint8_t* data, size_t size) { bytes_.insert(bytes_.end(), data, data + size); }
    Bytes take() { return std::move(bytes_); }

private:
    Bytes bytes_;
};

// Le sem nunca passar do fim: qualquer leitura alem do pacote marca a leitura como invalida.
class Reader {
public:
    Reader(const Bytes& bytes, size_t offset) : bytes_(bytes), offset_(offset) {}
    bool ok() const { return ok_; }
    bool atEnd() const { return ok_ && offset_ == bytes_.size(); }
    size_t remaining() const { return ok_ ? bytes_.size() - offset_ : 0; }
    void fail() { ok_ = false; }

    uint8_t u8() {
        if (!ok_ || offset_ >= bytes_.size()) { ok_ = false; return 0; }
        return bytes_[offset_++];
    }
    uint16_t u16() { const uint16_t high = u8(); return static_cast<uint16_t>((high << 8) | u8()); }
    uint32_t u32() {
        uint32_t value = 0;
        for (int i = 0; i < 4; i++) value = (value << 8) | u8();
        return value;
    }
    float f32(float limit) {
        const uint32_t bits = u32();
        float value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        if (!std::isfinite(value) || std::fabs(value) > limit) ok_ = false;
        return ok_ ? value : 0.0f;
    }
    glm::vec3 vec3(float limit) {
        const float x = f32(limit), y = f32(limit);
        return {x, y, f32(limit)};
    }
    bool flag() {
        const uint8_t value = u8();
        if (value > 1) ok_ = false;
        return value == 1;
    }
    FlightMode mode() {
        const uint8_t value = u8();
        if (value > static_cast<uint8_t>(FlightMode::Dive)) ok_ = false;
        return ok_ ? static_cast<FlightMode>(value) : FlightMode::Glide;
    }
    std::string text(size_t size) {
        if (!ok_ || remaining() < size) { ok_ = false; return ""; }
        std::string value(bytes_.begin() + static_cast<long>(offset_), bytes_.begin() + static_cast<long>(offset_ + size));
        offset_ += size;
        for (const char c : value)
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) ok_ = false;
        return value;
    }
    void raw(size_t size, Bytes& out) {
        if (!ok_ || remaining() < size) { ok_ = false; return; }
        out.assign(bytes_.begin() + static_cast<long>(offset_), bytes_.begin() + static_cast<long>(offset_ + size));
        offset_ += size;
    }

private:
    const Bytes& bytes_;
    size_t offset_;
    bool ok_ = true;
};

// Confere o cabecalho. Mensagens comuns exigem a versao atual.
bool expect(const Bytes& bytes, MessageType type, bool anyVersion = false) {
    return bytes.size() >= 2 && bytes[1] == static_cast<uint8_t>(type) && (anyVersion || bytes[0] == kProtocolVersion);
}

void writeEvent(Writer& out, const GameEvent& event) {
    out.u8(static_cast<uint8_t>(event.type));
    out.u8(event.actor);
    out.u8(event.target);
    out.u16(event.cashew);
    out.u8(event.value);
    out.vec3(event.position);
}

GameEvent readEvent(Reader& in) {
    GameEvent event;
    const uint8_t type = in.u8();
    if (type > static_cast<uint8_t>(kLastGameEventType)) in.fail();
    event.type = static_cast<GameEventType>(type);
    event.actor = in.u8();
    event.target = in.u8();
    event.cashew = in.u16();
    event.value = in.u8();
    event.position = in.vec3(kMaxCoordinate);
    return event;
}

void writePlayer(Writer& out, const PlayerPublicState& state) {
    out.u8(state.id);
    out.vec3(state.position);
    out.vec3(state.velocity);
    out.f32(state.yaw);
    out.f32(state.pitch);
    out.f32(state.roll);
    out.u8(static_cast<uint8_t>(state.mode));
    out.u8(state.carried);
    out.u8(state.speaking ? 1 : 0);
    out.u32(state.birdSeed);
    out.u8(state.nestLevel);
    out.u8(state.nestDefense);
    out.u8(state.nestStock);
}

PlayerPublicState readPlayer(Reader& in) {
    PlayerPublicState state;
    state.id = in.u8();
    if (state.id >= kMaxPlayers) in.fail();
    state.position = in.vec3(kMaxCoordinate);
    state.velocity = in.vec3(kMaxSpeed);
    state.yaw = in.f32(kMaxAngle);
    state.pitch = in.f32(kMaxAngle);
    state.roll = in.f32(kMaxAngle);
    state.mode = in.mode();
    state.carried = in.u8();
    state.speaking = in.flag();
    state.birdSeed = in.u32();
    state.nestLevel = in.u8();
    state.nestDefense = in.u8();
    state.nestStock = in.u8();
    return state;
}

void writeNpc(Writer& out, const NpcPublicState& npc) {
    out.u32(npc.seed);
    out.vec3(npc.position);
    out.f32(npc.yaw);
    out.f32(npc.pitch);
    out.f32(npc.roll);
    out.u8(static_cast<uint8_t>(npc.mode));
    // Um byte de comportamento: papel nos 4 bits baixos e pouso no bit mais alto.
    out.u8(static_cast<uint8_t>((npc.role & 0x0F) | (npc.perched ? 0x80 : 0)));
}

NpcPublicState readNpc(Reader& in) {
    NpcPublicState npc;
    npc.seed = in.u32();
    npc.position = in.vec3(kMaxCoordinate);
    npc.yaw = in.f32(kMaxAngle);
    npc.pitch = in.f32(kMaxAngle);
    npc.roll = in.f32(kMaxAngle);
    npc.mode = in.mode();
    const uint8_t behavior = in.u8();
    if (behavior & 0x70) in.fail();
    npc.role = behavior & 0x0F;
    npc.perched = (behavior & 0x80) != 0;
    return npc;
}
} // namespace

std::optional<MessageHeader> peekHeader(const Bytes& bytes) {
    if (bytes.size() < 2 || bytes[1] < static_cast<uint8_t>(MessageType::Hello) ||
        bytes[1] > static_cast<uint8_t>(MessageType::Identity)) return std::nullopt;
    return MessageHeader{bytes[0], static_cast<MessageType>(bytes[1])};
}

Bytes encodeHello(const HelloMessage& message) {
    Writer out(MessageType::Hello, message.version);
    out.u32(message.worldSeed);
    const std::string name = message.name.substr(0, kMaxNameLength);
    out.u8(static_cast<uint8_t>(name.size()));
    out.raw(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    out.u32(message.birdSeed);
    return out.take();
}

std::optional<HelloMessage> decodeHello(const Bytes& bytes) {
    if (!expect(bytes, MessageType::Hello, true)) return std::nullopt;
    Reader in(bytes, 2);
    HelloMessage message;
    message.version = bytes[0];
    message.worldSeed = in.u32();
    const uint8_t length = in.u8();
    if (length > kMaxNameLength) return std::nullopt;
    message.name = in.text(length);
    message.birdSeed = in.u32();
    if (!in.atEnd()) return std::nullopt;
    return message;
}

Bytes encodeWelcome(const WelcomeMessage& message) {
    Writer out(MessageType::Welcome, message.version);
    out.u32(message.worldSeed);
    out.u8(message.player);
    return out.take();
}

std::optional<WelcomeMessage> decodeWelcome(const Bytes& bytes) {
    if (!expect(bytes, MessageType::Welcome, true)) return std::nullopt;
    Reader in(bytes, 2);
    WelcomeMessage message;
    message.version = bytes[0];
    message.worldSeed = in.u32();
    message.player = in.u8();
    // O host e sempre o 0; um cliente recebe de 1 ate kMaxPlayers - 1.
    if (!in.atEnd() || message.player == 0 || message.player >= kMaxPlayers) return std::nullopt;
    return message;
}

Bytes encodeRefuse(RefuseReason reason) {
    Writer out(MessageType::Refuse);
    out.u8(static_cast<uint8_t>(reason));
    return out.take();
}

std::optional<RefuseReason> decodeRefuse(const Bytes& bytes) {
    if (!expect(bytes, MessageType::Refuse, true) || bytes.size() != 3) return std::nullopt;
    if (bytes[2] < static_cast<uint8_t>(RefuseReason::Full) || bytes[2] > static_cast<uint8_t>(RefuseReason::WorldSeed))
        return std::nullopt;
    return static_cast<RefuseReason>(bytes[2]);
}

Bytes encodeEvents(MessageType type, const std::vector<GameEvent>& events) {
    Writer out(type);
    const size_t count = std::min(events.size(), kMaxEventsPerMessage);
    out.u8(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; i++) writeEvent(out, events[i]);
    return out.take();
}

std::vector<Bytes> encodeEventBatches(MessageType type, const std::vector<GameEvent>& events) {
    std::vector<Bytes> batches;
    for (size_t first = 0; first < events.size(); first += kMaxEventsPerMessage) {
        const auto begin = events.begin() + static_cast<long>(first);
        const auto end = events.begin() + static_cast<long>(std::min(events.size(), first + kMaxEventsPerMessage));
        batches.push_back(encodeEvents(type, std::vector<GameEvent>(begin, end)));
    }
    return batches;
}

std::optional<std::vector<GameEvent>> decodeEvents(MessageType type, const Bytes& bytes) {
    if ((type != MessageType::EventRequest && type != MessageType::EventConfirmed) || !expect(bytes, type))
        return std::nullopt;
    Reader in(bytes, 2);
    const uint8_t count = in.u8();
    if (count == 0 || count > kMaxEventsPerMessage || in.remaining() != count * kEventSize) return std::nullopt;
    std::vector<GameEvent> events;
    for (uint8_t i = 0; i < count; i++) events.push_back(readEvent(in));
    if (!in.atEnd()) return std::nullopt;
    return events;
}

Bytes encodeState(const StateMessage& message) {
    Writer out(MessageType::PlayerState);
    out.f32(message.time);
    writePlayer(out, message.state);
    return out.take();
}

std::optional<StateMessage> decodeState(const Bytes& bytes) {
    if (!expect(bytes, MessageType::PlayerState) || bytes.size() != 2 + 4 + kPlayerSize) return std::nullopt;
    Reader in(bytes, 2);
    StateMessage message;
    message.time = in.f32(1.0e7f);
    message.state = readPlayer(in);
    if (!in.atEnd()) return std::nullopt;
    return message;
}

Bytes encodeSnapshot(const SnapshotMessage& message) {
    Writer out(MessageType::Snapshot);
    out.f32(message.time);
    const size_t players = std::min<size_t>(message.players.size(), kMaxPlayers);
    const size_t npcs = std::min(message.npcs.size(), kMaxNpcs);
    out.u8(static_cast<uint8_t>(players));
    for (size_t i = 0; i < players; i++) writePlayer(out, message.players[i]);
    out.u8(static_cast<uint8_t>(npcs));
    for (size_t i = 0; i < npcs; i++) writeNpc(out, message.npcs[i]);
    return out.take();
}

std::optional<SnapshotMessage> decodeSnapshot(const Bytes& bytes) {
    if (!expect(bytes, MessageType::Snapshot)) return std::nullopt;
    Reader in(bytes, 2);
    SnapshotMessage message;
    message.time = in.f32(1.0e7f);
    const uint8_t players = in.u8();
    if (players > kMaxPlayers || in.remaining() < players * kPlayerSize + 1) return std::nullopt;
    for (uint8_t i = 0; i < players; i++) message.players.push_back(readPlayer(in));
    const uint8_t npcs = in.u8();
    if (npcs > kMaxNpcs || in.remaining() != npcs * kNpcSize) return std::nullopt;
    for (uint8_t i = 0; i < npcs; i++) message.npcs.push_back(readNpc(in));
    if (!in.atEnd()) return std::nullopt;
    // Um jogador repetido no mesmo snapshot e sinal de pacote forjado.
    for (size_t i = 0; i < message.players.size(); i++)
        for (size_t j = i + 1; j < message.players.size(); j++)
            if (message.players[i].id == message.players[j].id) return std::nullopt;
    return message;
}

Bytes encodeVoice(const VoiceFrame& frame) {
    Writer out(MessageType::Voice);
    out.u8(frame.speaker);
    out.u16(frame.sequence);
    const size_t size = std::min(frame.payload.size(), kMaxVoicePayload);
    out.u16(static_cast<uint16_t>(size));
    out.raw(frame.payload.data(), size);
    return out.take();
}

std::optional<VoiceFrame> decodeVoice(const Bytes& bytes) {
    if (!expect(bytes, MessageType::Voice)) return std::nullopt;
    Reader in(bytes, 2);
    VoiceFrame frame;
    frame.speaker = in.u8();
    frame.sequence = in.u16();
    const uint16_t size = in.u16();
    if (frame.speaker >= kMaxPlayers || size == 0 || size > kMaxVoicePayload || in.remaining() != size)
        return std::nullopt;
    in.raw(size, frame.payload);
    if (!in.atEnd()) return std::nullopt;
    return frame;
}

Bytes encodeRoster(const RosterMessage& roster) {
    Writer out(MessageType::Roster);
    const size_t count = std::min(roster.size(), static_cast<size_t>(kMaxPlayers));
    out.u8(static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; i++) {
        const std::string name = roster[i].name.substr(0, kMaxNameLength);
        out.u8(roster[i].player);
        out.u8(static_cast<uint8_t>(name.size()));
        out.raw(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    }
    return out.take();
}

std::optional<RosterMessage> decodeRoster(const Bytes& bytes) {
    if (!expect(bytes, MessageType::Roster)) return std::nullopt;
    Reader in(bytes, 2);
    const uint8_t count = in.u8();
    if (count > kMaxPlayers) return std::nullopt;
    RosterMessage roster;
    for (uint8_t i = 0; i < count; i++) {
        RosterEntry entry;
        entry.player = in.u8();
        const uint8_t length = in.u8();
        if (entry.player >= kMaxPlayers || length > kMaxNameLength) return std::nullopt;
        entry.name = in.text(length);
        roster.push_back(std::move(entry));
    }
    if (!in.atEnd()) return std::nullopt;
    return roster;
}

Bytes encodeIdentity(const IdentityMessage& identity) {
    Writer out(MessageType::Identity);
    const std::string name = identity.name.substr(0, kMaxNameLength);
    out.u8(static_cast<uint8_t>(name.size()));
    out.raw(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    out.u32(identity.birdSeed);
    return out.take();
}

std::optional<IdentityMessage> decodeIdentity(const Bytes& bytes) {
    if (!expect(bytes, MessageType::Identity)) return std::nullopt;
    Reader in(bytes, 2);
    IdentityMessage identity;
    const uint8_t length = in.u8();
    if (length > kMaxNameLength) return std::nullopt;
    identity.name = in.text(length);
    identity.birdSeed = in.u32();
    if (!in.atEnd()) return std::nullopt;
    return identity;
}

static_assert(2 + 1 + kMaxPlayers * (2 + kMaxNameLength) <= kMaxPayload, "A lista de nomes cabe em uma mensagem");
static_assert(2 + 4 + 1 + kMaxPlayers * kPlayerSize + 1 + kMaxNpcs * kNpcSize <= kMaxPayload,
              "O snapshot cheio precisa caber em um datagrama");
static_assert(2 + 1 + kMaxEventsPerMessage * kEventSize <= kMaxPayload);
static_assert(2 + 5 + kMaxVoicePayload <= kMaxPayload);

} // namespace net

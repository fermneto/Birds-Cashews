#include "net/UdpSession.h"
#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <charconv>
#include <chrono>
#include <deque>
#include <iterator>
#include <netinet/in.h>
#include <random>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace net {
namespace {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using namespace std::chrono_literals;
using Bytes = std::vector<uint8_t>;

constexpr size_t kHeaderSize = 13;
constexpr size_t kMaxPayload = 1024;
constexpr size_t kMaxRoomName = 64;
constexpr size_t kMaxQueue = 128;
constexpr uint8_t kMaxPlayers = 4;
constexpr auto kRetry = 120ms;
constexpr auto kHeartbeat = 500ms;
constexpr auto kTimeout = 4s;
constexpr auto kDiscoveryRefresh = 500ms;
constexpr auto kDiscoveryLifetime = 3s;
constexpr std::array<uint8_t, 4> kSignature{'B', 'C', 'S', 1};

enum class Kind : uint8_t { Discover, Room, Join, Accept, Reject, Ping, Leave, Message, Ack };

struct Packet {
    Kind kind;
    uint32_t token = 0;
    uint32_t sequence = 0;
    Bytes payload{};
};

struct Peer {
    PeerId id;
    sockaddr_in address;
    uint32_t token;
    Time lastHeard;
    Time lastPing{};
    Time lastSend{};
    uint32_t sendSequence = 0;
    uint32_t receiveSequence = 0;
    bool receivedFirst = false;
    std::deque<Bytes> queue{};
};

struct DiscoveredRoom {
    RoomInfo room;
    Time lastSeen;
};

bool sameAddress(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

sockaddr_in addressOf(uint32_t ipv4, uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ipv4);
    address.sin_port = htons(port);
    return address;
}

bool parseAddress(const std::string& input, uint16_t defaultPort, sockaddr_in& address) {
    auto host = input.substr(0, input.find(':'));
    uint16_t port = defaultPort;
    if (host.size() < input.size()) {
        const char* first = input.data() + host.size() + 1;
        const char* last = input.data() + input.size();
        const auto parsed = std::from_chars(first, last, port);
        if (parsed.ec != std::errc{} || parsed.ptr != last || port == 0) return false;
    }
    address = addressOf(INADDR_ANY, port);
    return inet_pton(AF_INET, host.c_str(), &address.sin_addr) == 1;
}

std::string displayAddress(const sockaddr_in& address) {
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(address.sin_port));
}

void append32(Bytes& bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
}

uint32_t read32(const uint8_t* bytes) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | bytes[i];
    return value;
}

bool decode(const uint8_t* bytes, size_t size, Packet& out) {
    if (size < kHeaderSize || size > kHeaderSize + kMaxPayload ||
        !std::equal(kSignature.begin(), kSignature.end(), bytes) ||
        bytes[4] > static_cast<uint8_t>(Kind::Ack)) return false;

    const auto kind = static_cast<Kind>(bytes[4]);
    const size_t length = size - kHeaderSize;
    if (kind == Kind::Room) {
        if (length < 2 || length > kMaxRoomName + 1 || bytes[kHeaderSize] < 1 ||
            bytes[kHeaderSize] > kMaxPlayers) return false;
    } else if (kind == Kind::Accept) {
        if (length != 1 || bytes[kHeaderSize] == kHostPeer || bytes[kHeaderSize] >= kMaxPlayers) return false;
    } else if (kind != Kind::Message && length != 0) {
        return false;
    }

    out = {kind, read32(bytes + 5), read32(bytes + 9), Bytes(bytes + kHeaderSize, bytes + size)};
    return true;
}

void sendPacket(int socket, const sockaddr_in& address, const Packet& packet) {
    Bytes bytes(kSignature.begin(), kSignature.end());
    bytes.push_back(static_cast<uint8_t>(packet.kind));
    append32(bytes, packet.token);
    append32(bytes, packet.sequence);
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    sendto(socket, bytes.data(), bytes.size(), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
}
} // namespace

struct UdpSession::State {
    explicit State(uint16_t serverPort) : port(serverPort) {}
    ~State() { if (socket >= 0) close(socket); }

    bool open(uint16_t bindPort) {
        if (socket >= 0) return true;
        socket = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (socket < 0) return false;
        const int enable = 1;
        const auto address = addressOf(INADDR_ANY, bindPort);
        if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) == 0 &&
            bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) return true;
        close(socket);
        socket = -1;
        return false;
    }

    void handleJoin(const sockaddr_in& from, const Packet& packet, Time now) {
        if (!hosting || packet.token == 0) return;
        auto peer = std::find_if(peers.begin(), peers.end(), [&](const Peer& p) {
            return sameAddress(p.address, from);
        });
        if (peer != peers.end() && peer->token != packet.token) {
            sendPacket(socket, from, {Kind::Reject, packet.token});
            return;
        }
        if (peer == peers.end()) {
            if (peers.size() >= kMaxPlayers - 1) {
                sendPacket(socket, from, {Kind::Reject, packet.token});
                return;
            }
            PeerId id = 1;
            while (std::any_of(peers.begin(), peers.end(), [id](const Peer& p) { return p.id == id; })) ++id;
            // Descarta mensagens pendentes do ocupante anterior deste ID.
            messages.erase(std::remove_if(messages.begin(), messages.end(), [id](const Message& message) {
                return message.from == id;
            }), messages.end());
            peers.push_back({id, from, packet.token, now});
            peer = std::prev(peers.end());
        }
        peer->lastHeard = now;
        sendPacket(socket, from, {Kind::Accept, packet.token, 0, {peer->id}});
    }

    void recordRoom(const sockaddr_in& from, const Packet& packet, Time now) {
        if (now - lastDiscovery >= kDiscoveryLifetime) return;
        const std::string address = displayAddress(from);
        RoomInfo room{std::string(packet.payload.begin() + 1, packet.payload.end()), address, packet.payload.front()};
        const auto existing = std::find_if(rooms.begin(), rooms.end(), [&](const DiscoveredRoom& item) {
            return item.room.address == address;
        });
        if (existing != rooms.end()) *existing = {std::move(room), now};
        else if (rooms.size() < kMaxQueue) rooms.push_back({std::move(room), now});
    }

    void receive(const sockaddr_in& from, Packet packet, Time now) {
        switch (packet.kind) {
        case Kind::Discover:
            if (hosting) {
                Bytes info{static_cast<uint8_t>(peers.size() + 1)};
                info.insert(info.end(), roomName.begin(), roomName.end());
                sendPacket(socket, from, {Kind::Room, 0, 0, std::move(info)});
            }
            return;
        case Kind::Room:
            recordRoom(from, packet, now);
            return;
        case Kind::Join:
            handleJoin(from, packet, now);
            return;
        case Kind::Accept:
        case Kind::Reject:
            if (joining && packet.token == joinToken && sameAddress(joinAddress, from)) {
                joining = false;
                if (packet.kind == Kind::Accept) {
                    localId = packet.payload.front();
                    peers.push_back({kHostPeer, from, packet.token, now});
                }
            }
            return;
        default:
            break;
        }

        const auto peer = std::find_if(peers.begin(), peers.end(), [&](const Peer& p) {
            return p.token == packet.token && sameAddress(p.address, from);
        });
        if (peer == peers.end()) return;
        peer->lastHeard = now;
        switch (packet.kind) {
        case Kind::Leave:
            peers.erase(peer);
            break;
        case Kind::Ack:
            if (!peer->queue.empty() && peer->sendSequence == packet.sequence) {
                peer->queue.pop_front();
                ++peer->sendSequence;
                peer->lastSend = Time{};
            }
            break;
        case Kind::Message:
            if (packet.sequence == peer->receiveSequence && messages.size() < kMaxQueue) {
                messages.push_back({peer->id, std::move(packet.payload)});
                ++peer->receiveSequence;
                peer->receivedFirst = true;
            }
            // Confirma a ultima mensagem entregue mesmo se o ACK original se perder.
            if (peer->receivedFirst && packet.sequence == peer->receiveSequence - 1)
                sendPacket(socket, from, {Kind::Ack, peer->token, packet.sequence});
            break;
        default:
            break;
        }
    }

    void poll() {
        if (socket < 0) return;
        const auto now = Clock::now();
        peers.erase(std::remove_if(peers.begin(), peers.end(), [&](const Peer& peer) {
            return now - peer.lastHeard >= kTimeout;
        }), peers.end());

        std::array<uint8_t, kHeaderSize + kMaxPayload> bytes{};
        for (size_t count = 0; count < 256; ++count) {
            sockaddr_in from{};
            socklen_t size = sizeof(from);
            const auto length = recvfrom(socket, bytes.data(), bytes.size(), MSG_TRUNC,
                                         reinterpret_cast<sockaddr*>(&from), &size);
            if (length < 0) break;
            Packet packet{};
            if (decode(bytes.data(), static_cast<size_t>(length), packet)) receive(from, std::move(packet), now);
        }

        for (auto& peer : peers) {
            if (now - peer.lastPing >= kHeartbeat) {
                sendPacket(socket, peer.address, {Kind::Ping, peer.token});
                peer.lastPing = now;
            }
            if (!peer.queue.empty() && now - peer.lastSend >= kRetry) {
                sendPacket(socket, peer.address, {Kind::Message, peer.token, peer.sendSequence, peer.queue.front()});
                peer.lastSend = now;
            }
        }
    }

    uint16_t port;
    int socket = -1;
    bool hosting = false;
    bool joining = false;
    PeerId localId = kHostPeer;
    uint32_t joinToken = 0;
    sockaddr_in joinAddress{};
    std::string roomName;
    Time lastDiscovery{};
    std::vector<Peer> peers;
    std::vector<DiscoveredRoom> rooms;
    std::vector<Message> messages;
};

UdpSession::UdpSession(uint16_t port) : state_(std::make_unique<State>(port)) {}
UdpSession::~UdpSession() {
    for (const auto& peer : state_->peers)
        sendPacket(state_->socket, peer.address, {Kind::Leave, peer.token});
}

bool UdpSession::host(const std::string& roomName) {
    if (roomName.empty() || roomName.size() > kMaxRoomName) return false;
    leave();
    if (!state_->open(state_->port)) return false;
    state_->hosting = true;
    state_->roomName = roomName;
    return true;
}

bool UdpSession::join(const std::string& input) {
    sockaddr_in address{};
    if (!parseAddress(input, state_->port, address)) return false;
    leave();
    if (!state_->open(0)) return false;
    state_->joinAddress = address;
    state_->joinToken = static_cast<uint32_t>(std::random_device{}()) | 1;
    state_->joining = true;
    const auto deadline = Clock::now() + 1500ms;
    auto nextAttempt = Time{};
    while (state_->joining && Clock::now() < deadline) {
        const auto now = Clock::now();
        if (now >= nextAttempt) {
            sendPacket(state_->socket, address, {Kind::Join, state_->joinToken});
            nextAttempt = now + kRetry;
        }
        poll();
        if (state_->joining) std::this_thread::sleep_for(5ms);
    }
    if (connected()) return true;
    leave();
    return false;
}

std::vector<RoomInfo> UdpSession::discoverRooms() {
    if (!state_->open(0)) return {};
    const auto now = Clock::now();
    if (now - state_->lastDiscovery >= kDiscoveryRefresh) {
        state_->lastDiscovery = now;
        sendPacket(state_->socket, addressOf(INADDR_BROADCAST, state_->port), {Kind::Discover});
        sendPacket(state_->socket, addressOf(INADDR_LOOPBACK, state_->port), {Kind::Discover});
    }
    poll();
    auto& rooms = state_->rooms;
    rooms.erase(std::remove_if(rooms.begin(), rooms.end(), [&](const DiscoveredRoom& item) {
        return now - item.lastSeen >= kDiscoveryLifetime;
    }), rooms.end());
    std::vector<RoomInfo> result;
    for (const auto& item : rooms) result.push_back(item.room);
    return result;
}

void UdpSession::leave() {
    if (state_->socket >= 0) {
        for (const auto& peer : state_->peers)
            sendPacket(state_->socket, peer.address, {Kind::Leave, peer.token});
        close(state_->socket);
        state_->socket = -1;
    }
    state_->hosting = false;
    state_->joining = false;
    state_->localId = kHostPeer;
    state_->lastDiscovery = Time{};
    state_->peers.clear();
    state_->rooms.clear();
    state_->messages.clear();
}

void UdpSession::poll() { state_->poll(); }

bool UdpSession::connected() const { return state_->hosting || !state_->peers.empty(); }

std::optional<PeerId> UdpSession::localPeer() const {
    if (!connected()) return std::nullopt;
    return state_->localId;
}

std::vector<PeerId> UdpSession::peers() const {
    std::vector<PeerId> ids;
    for (const auto& peer : state_->peers) ids.push_back(peer.id);
    return ids;
}

bool UdpSession::sendReliable(PeerId to, const std::vector<uint8_t>& payload) {
    if (payload.size() > kMaxPayload) return false;
    const auto found = std::find_if(state_->peers.begin(), state_->peers.end(),
                                    [to](const Peer& peer) { return peer.id == to; });
    if (found == state_->peers.end() || found->queue.size() >= kMaxQueue) return false;
    found->queue.push_back(payload);
    return true;
}

std::vector<Message> UdpSession::pollMessages() {
    std::vector<Message> messages;
    messages.swap(state_->messages);
    return messages;
}

} // namespace net

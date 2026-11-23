#include "net/NetMatch.h"
#include <iostream>
#include <thread>

namespace net {
namespace {
using namespace std::chrono_literals;
constexpr auto kWelcomeTimeout = 1500ms;
constexpr auto kDiscoveryWait = 250ms;
constexpr float kPumpSeconds = 0.005f;
}

std::string joinErrorMessage(JoinStatus status) {
    switch (status) {
        case JoinStatus::Refused: return "A sala está cheia.";
        case JoinStatus::NoAnswer: return "Host não encontrado nesse endereço.";
        case JoinStatus::InvalidAddress: return "Endereço inválido.";
        case JoinStatus::SocketError: return "Não foi possível abrir a conexão de rede.";
        default: return "Não foi possível entrar nessa sala.";
    }
}

NetMatch::NetMatch(uint32_t worldSeed, uint16_t port) : udp_(port), worldSeed_(worldSeed) {}

NetMatch::~NetMatch() { leave(); }

void NetMatch::setLocalIdentity(const std::string& name, uint32_t birdSeed) {
    name_ = name;
    birdSeed_ = birdSeed;
}

void NetMatch::setTimeouts(std::chrono::milliseconds peerTimeout, std::chrono::milliseconds joinTimeout) {
    udp_.setTimeouts(peerTimeout, joinTimeout);
}

bool NetMatch::host(const std::string& roomName) {
    leave();
    error_.clear();
    if (!udp_.host(roomName)) {
        error_ = "Não foi possível criar a sala. A porta " + std::to_string(kLanPort) + " pode estar em uso.";
        return false;
    }
    HostConfig config;
    config.worldSeed = worldSeed_;
    config.cashewCount = cashewCount_;
    config.nestPerch = nestPerch_;
    host_ = std::make_shared<HostAuthority>(udp_, config);
    mode_ = MatchMode::Host;
    changed_ = true;
    return true;
}

// Entra em duas etapas: a vaga no UdpSession e depois o Hello, que confere versao e semente do mundo.
bool NetMatch::join(const std::string& address) {
    leave();
    error_.clear();
    if (!udp_.join(address)) {
        error_ = joinErrorMessage(udp_.joinStatus());
        return false;
    }
    ClientConfig config;
    config.worldSeed = worldSeed_;
    config.name = name_;
    config.birdSeed = birdSeed_;
    auto client = std::make_shared<ClientAuthority>(udp_, config);
    const auto deadline = std::chrono::steady_clock::now() + kWelcomeTimeout;
    while (client->status() == ClientStatus::Connecting && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
        client->update(kPumpSeconds);
    }
    if (client->status() != ClientStatus::Ready) {
        error_ = client->status() == ClientStatus::Connecting ? "O host não respondeu à apresentação." : client->error();
        udp_.leave();
        return false;
    }
    client_ = std::move(client);
    mode_ = MatchMode::Client;
    changed_ = true;
    return true;
}

std::vector<RoomInfo> NetMatch::discoverRooms() {
    if (online()) return {};
    udp_.discoverRooms();
    const auto deadline = std::chrono::steady_clock::now() + kDiscoveryWait;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
        udp_.poll();
    }
    return udp_.discoverRooms();
}

void NetMatch::leave() {
    if (online()) changed_ = true;
    udp_.leave();
    host_.reset();
    client_.reset();
    mode_ = MatchMode::Solo;
}

void NetMatch::becomeSolo(const std::string& error) {
    std::cerr << "Rede: " << error << "\n";
    leave();
    error_ = error;
}

void NetMatch::update(float seconds) {
    if (host_) {
        host_->setLocalName(name_);
        host_->update(seconds);
    }
    if (client_) {
        client_->update(seconds);
        if (client_->status() != ClientStatus::Ready) becomeSolo(client_->error());
    }
}

bool NetMatch::consumeChange() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
}

PlayerId NetMatch::localPlayer() const { return client_ ? client_->localPlayer() : 0; }

std::shared_ptr<GameAuthority> NetMatch::authority() const {
    if (host_) return host_;
    return client_;
}

VoiceLink* NetMatch::voice() const {
    if (host_) return host_.get();
    return client_.get();
}

} // namespace net

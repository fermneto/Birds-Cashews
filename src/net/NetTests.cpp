#include "net/NetTests.h"
#include "core/TestRun.h"
#include "net/UdpSession.h"
#include <array>
#include <atomic>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {
using namespace std::chrono_literals;

sockaddr_in loopback(uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

std::string endpoint(uint16_t port) { return "127.0.0.1:" + std::to_string(port); }

class TestSocket {
public:
    TestSocket() : fd_(socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {
        if (fd_ < 0) return;
        auto address = loopback(0);
        socklen_t size = sizeof(address);
        if (bind(fd_, reinterpret_cast<const sockaddr*>(&address), size) == 0 &&
            getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &size) == 0) {
            port = ntohs(address.sin_port);
        }
    }
    ~TestSocket() { if (fd_ >= 0) close(fd_); }
    TestSocket(const TestSocket&) = delete;
    TestSocket& operator=(const TestSocket&) = delete;

    void forward(const sockaddr_in& host) {
        if (disconnected) return;
        std::array<uint8_t, 2048> bytes{};
        sockaddr_in source{};
        socklen_t size = sizeof(source);
        for (int i = 0; i < 16; ++i) {
            const auto n = recvfrom(fd_, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&source), &size);
            if (n < 0) break;
            const bool fromHost = source.sin_port == host.sin_port;
            if (!fromHost) client_ = source;
            const sockaddr_in& target = fromHost ? client_ : host;
            // Registra o tipo e perde o primeiro datagrama escolhido em cada sentido.
            if (n >= 5 && (fromHost ? bytes[4] == dropHostKind : bytes[4] == dropClientKind)) {
                auto& count = fromHost ? droppedHost : droppedClient;
                if (count.fetch_add(1) == 0) continue;
            }
            sendto(fd_, bytes.data(), n, 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
        }
    }

    uint16_t port = 0;
    std::atomic<bool> disconnected{false};
    std::atomic<uint8_t> dropHostKind{255}, dropClientKind{255};
    std::atomic<int> droppedHost{0}, droppedClient{0};

private:
    int fd_;
    sockaddr_in client_{};
};

uint16_t availablePort() { return TestSocket{}.port; }

template<class Check>
bool waitFor(net::UdpSession& session, std::chrono::milliseconds timeout, Check check) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        session.poll();
        if (check()) return true;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
} // namespace

int runNetworkSelfTest() {
    TestRun test;
    const auto port = availablePort();
    TestSocket proxy;
    net::UdpSession host(port);
    if (!port || !proxy.port || !host.host("Ilha teste")) {
        test.check("rede: abre host e proxy UDP local", false);
        return test.failures;
    }

    const auto hostAddress = loopback(port);
    std::atomic<bool> stop{false}, hostReceived{false}, hostExpired{false};
    std::thread worker([&] {
        while (!stop) {
            proxy.forward(hostAddress);
            host.poll();
            if (proxy.disconnected && host.peers().empty()) hostExpired = true;
            for (const auto& message : host.pollMessages()) {
                if (message.payload == std::vector<uint8_t>{4, 5}) {
                    hostReceived = true;
                    host.sendReliable(message.from, {6});
                    host.sendReliable(message.from, {7});
                }
            }
            proxy.forward(hostAddress);
            std::this_thread::sleep_for(2ms);
        }
    });

    net::UdpSession client(port);
    test.check("rede: descoberta local", waitFor(client, 800ms, [&] {
        for (const auto& room : client.discoverRooms()) {
            if (room.name == "Ilha teste" && room.players == 1) return true;
        }
        return false;
    }));
    test.check("rede: IP manual usa porta padrao da sessao", client.join("127.0.0.1") && client.localPeer() == 1);
    client.leave();

    if (client.join(endpoint(proxy.port))) {
        test.check("rede: host atribui ID", client.localPeer() == 1);
        proxy.dropClientKind = 7; // Mensagem
        proxy.dropHostKind = 8;   // Confirmacao
        test.check("rede: enfileira evento", client.sendReliable(net::kHostPeer, {4, 5}));
        std::vector<uint8_t> replies;
        const auto collectReplies = [&] {
            for (const auto& message : client.pollMessages()) {
                if (message.from == net::kHostPeer && message.payload.size() == 1)
                    replies.push_back(message.payload.front());
            }
        };
        const bool received = waitFor(client, 1200ms, [&] {
            collectReplies();
            return replies.size() >= 2 && proxy.droppedHost >= 2;
        });
        waitFor(client, 150ms, [&] { collectReplies(); return replies.size() > 2; });
        test.check("rede: retransmite mensagem e ACK perdidos, sem duplicar e em ordem",
                   received && hostReceived && proxy.droppedClient >= 3 && replies == std::vector<uint8_t>{6, 7});

        proxy.disconnected = true;
        test.check("rede: detecta queda sem pacote de saida", waitFor(client, 4700ms, [&] {
            return !client.connected() && !client.localPeer() && client.peers().empty();
        }));
        test.check("rede: host remove sessao expirada", waitFor(client, 500ms, [&] { return hostExpired.load(); }));
    } else {
        test.check("rede: conecta por IP e porta com proxy", false);
    }
    stop = true;
    worker.join();
    return test.failures;
}

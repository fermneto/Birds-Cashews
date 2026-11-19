#include "net/UdpSession.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    const std::string mode = argv[1];
    net::UdpSession session;
    if (mode == "discover") {
        for (int i = 0; i < 100; ++i) {
            for (const auto& room : session.discoverRooms())
                if (room.name == "Birds D1") {
                    std::cout << room.name << " " << room.address << std::endl;
                    return 0;
                }
            std::this_thread::sleep_for(50ms);
        }
        std::cerr << "Sala não encontrada\n";
        return 1;
    }
    const bool host = mode == "host";
    if (host ? !session.host("Birds D1") :
               (mode != "join" || argc != 3 || !session.join(argv[2]))) {
        std::cerr << "Falha ao abrir a sessão\n";
        return 1;
    }
    std::cout << (host ? "Sala aberta" : "Conectado") << std::endl;
    int sent = 0, received = 0;
    bool hadPeer = false;
    auto next = std::chrono::steady_clock::now();
    for (;;) {
        session.poll();
        const auto peers = session.peers();
        if (!peers.empty()) hadPeer = true;
        if (hadPeer && peers.empty()) {
            std::cout << "Desconexão detectada" << std::endl;
            return received >= 3 ? 0 : 1;
        }
        if (!host && sent < 3 && std::chrono::steady_clock::now() >= next) {
            if (!session.sendReliable(net::kHostPeer, {static_cast<uint8_t>(sent + 1)})) return 1;
            ++sent;
            next = std::chrono::steady_clock::now() + 500ms;
        }
        for (const auto& message : session.pollMessages()) {
            if (message.payload.size() != 1 || message.payload[0] != received + 1) return 1;
            ++received;
            std::cout << (host ? "Recebido " : "Eco recebido ") << received << std::endl;
            if (host && !session.sendReliable(message.from, message.payload)) return 1;
        }
        std::this_thread::sleep_for(20ms);
    }
}

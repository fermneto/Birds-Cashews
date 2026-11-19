#pragma once
#include "shared/NetSession.h"

// Substituto da rede real ate a tarefa D3: aceita tudo e lista salas de exemplo.
class FakeNetSession : public NetSession {
public:
    bool host(const std::string& roomName) override { hosted_ = roomName; return accept_; }
    bool join(const std::string& address) override { joined_ = address; return accept_; }
    std::vector<RoomInfo> discoverRooms() override {
        return {{"Sala da Ana", "192.168.0.12", 2}, {"Ilha do Beto", "192.168.0.27", 1}};
    }
    void leave() override { hosted_.clear(); joined_.clear(); }

    void setAccept(bool accept) { accept_ = accept; }
    const std::string& hosted() const { return hosted_; }
    const std::string& joined() const { return joined_; }

private:
    bool accept_ = true;
    std::string hosted_, joined_;
};

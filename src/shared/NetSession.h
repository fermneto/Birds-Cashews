#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct RoomInfo {
    std::string name, address;
    uint8_t players = 0;
};

// Usada pelos menus para listar, criar e entrar em salas.
class NetSession {
public:
    virtual ~NetSession() = default;
    virtual bool host(const std::string& roomName) = 0;
    virtual bool join(const std::string& address) = 0;
    virtual std::vector<RoomInfo> discoverRooms() = 0;
    virtual void leave() = 0;
};

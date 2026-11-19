#pragma once
#include "save/ProfileStore.h"
#include "shared/GameplayView.h"
#include "shared/NetSession.h"
#include <filesystem>
#include <string>
#include <vector>

enum class ProfileResult { Ok, InvalidName, Duplicate, WriteFailed, Missing };

// Lista, cria e escolhe os perfis guardados em <pasta de saves>/perfis.
class ProfilePicker {
public:
    explicit ProfilePicker(const std::filesystem::path& savesDir) : store_(savesDir) { refresh(); }

    void refresh() { names_ = store_.list(); }
    const std::vector<std::string>& names() const { return names_; }
    const std::string& active() const { return active_; }

    ProfileResult create(const std::string& name, GameplaySource& source);
    ProfileResult select(const std::string& name, GameplaySource& source);

    static std::string messageFor(ProfileResult result);

private:
    ProfileStore store_;
    std::vector<std::string> names_;
    std::string active_;
};

// Hospedar e entrar em salas pela interface NetSession.
class RoomsModel {
public:
    explicit RoomsModel(NetSession& session) : session_(session) {}

    void refresh() { rooms_ = session_.discoverRooms(); }
    const std::vector<RoomInfo>& rooms() const { return rooms_; }
    const std::string& error() const { return error_; }
    void clearError() { error_.clear(); }

    bool hostRoom(const std::string& name);
    bool joinAddress(const std::string& address);
    bool joinRoom(size_t index);
    void leave() { session_.leave(); }

    static bool isValidAddress(const std::string& address);

private:
    NetSession& session_;
    std::vector<RoomInfo> rooms_;
    std::string error_;
};

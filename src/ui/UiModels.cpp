#include "ui/UiModels.h"
#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>

namespace {
std::string trimmed(const std::string& text) {
    const size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

bool isOctet(const std::string& part) {
    if (part.empty() || part.size() > 3) return false;
    if (!std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c); })) return false;
    if (part.size() > 1 && part[0] == '0') return false;
    return std::stoi(part) <= 255;
}
}

ProfileResult ProfilePicker::create(const std::string& rawName, GameplaySource& source) {
    const std::string name = trimmed(rawName);
    if (!isValidProfileName(name)) return ProfileResult::InvalidName;
    if (store_.exists(name)) return ProfileResult::Duplicate;
    if (!store_.create(name, std::random_device{}())) return ProfileResult::WriteFailed;
    refresh();
    return select(name, source);
}

ProfileResult ProfilePicker::select(const std::string& name, GameplaySource& source) {
    if (!store_.exists(name)) return ProfileResult::Missing;
    active_ = name;
    source.startProfile(name);
    return ProfileResult::Ok;
}

std::string ProfilePicker::messageFor(ProfileResult result) {
    switch (result) {
        case ProfileResult::Ok: return "";
        case ProfileResult::InvalidName: return "Digite um nome para o perfil.";
        case ProfileResult::Duplicate: return "Já existe um perfil com esse nome.";
        case ProfileResult::WriteFailed: return "Não foi possível salvar o perfil.";
        case ProfileResult::Missing: return "Esse perfil não existe mais.";
    }
    return "";
}

bool RoomsModel::isValidAddress(const std::string& address) {
    std::istringstream parts(address + ".");
    std::string part;
    int count = 0;
    while (std::getline(parts, part, '.')) {
        if (!isOctet(part)) return false;
        count++;
    }
    return count == 4 && address.back() != '.';
}

// A rede explica a falha quando sabe o motivo, como sala cheia ou versao diferente.
std::string RoomsModel::failure(const std::string& fallback) const {
    const std::string reason = session_.lastError();
    return reason.empty() ? fallback : reason;
}

bool RoomsModel::hostRoom(const std::string& rawName) {
    const std::string name = trimmed(rawName);
    error_.clear();
    if (name.empty()) error_ = "Digite um nome para a sala.";
    else if (!session_.host(name)) error_ = failure("Não foi possível criar a sala.");
    return error_.empty();
}

bool RoomsModel::joinAddress(const std::string& rawAddress) {
    const std::string address = trimmed(rawAddress);
    error_.clear();
    if (!isValidAddress(address)) error_ = "IP inválido. Exemplo: 192.168.0.10";
    else if (!session_.join(address)) error_ = failure("Não foi possível entrar nessa sala.");
    return error_.empty();
}

bool RoomsModel::joinRoom(size_t index) {
    return index < rooms_.size() && joinAddress(rooms_[index].address);
}

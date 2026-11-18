#pragma once
#include "save/KeyValueFile.h"
#include "save/Profile.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Guarda os perfis locais em <pasta base>/perfis, um arquivo por jogador.
class ProfileStore {
public:
    explicit ProfileStore(const std::filesystem::path& baseDir);

    // Nome de arquivo seguro e exclusivo do perfil. Vazio se o nome for invalido.
    static std::string fileNameFor(const std::string& name);

    bool exists(const std::string& name) const;
    std::vector<std::string> list() const;  // em ordem alfabetica

    // Falha se o nome for invalido, se o perfil ja existir ou se nao for possivel gravar.
    std::optional<Profile> create(const std::string& name, uint32_t birdSeed);

    // Sem valor se o perfil nao existir. Arquivo invalido devolve o que foi possivel recuperar.
    std::optional<Profile> load(const std::string& name, LoadStatus* status = nullptr) const;

    bool save(const Profile& profile);

private:
    std::filesystem::path directory_;
};

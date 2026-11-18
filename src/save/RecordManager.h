#pragma once
#include <filesystem>
#include <string>

// Maior total de cajus entregues entre os perfis vistos neste computador, em <pasta base>/record.txt.
class RecordManager {
public:
    explicit RecordManager(const std::filesystem::path& baseDir);

    int record() const { return record_; }
    const std::string& holder() const { return holder_; }

    // Chamar ao escolher o perfil: registra o total atual sem aviso e libera o aviso da sessao.
    void startSession(const std::string& name, int total);

    // Chamar a cada entrega. Retorna true somente no instante em que o jogador ultrapassa
    // o record de outro perfil, no maximo uma vez por sessao.
    bool update(const std::string& name, int total);

    // Indica se o HUD deve mostrar "RECORD" para este perfil.
    bool isLeading(const std::string& name, int total) const;

private:
    void refreshFromDisk();
    bool registerTotal(const std::string& name, int total);

    std::filesystem::path path_;
    int record_ = 0;
    std::string holder_;
    bool noticeShown_ = false;
};

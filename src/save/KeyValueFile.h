#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

// Resultado da leitura de um arquivo salvo.
enum class LoadStatus {
    Ok,        // arquivo integro
    Missing,   // arquivo ausente, foram usados os padroes
    Recovered  // arquivo invalido, foi aproveitado o que era confiavel
};

// Monta o texto de um arquivo salvo: cabecalho, pares chave=valor e marca de fim.
class KeyValueWriter {
public:
    KeyValueWriter(const std::string& format, int version);
    void put(const std::string& key, const std::string& value);
    void put(const std::string& key, int64_t value);
    std::string finish() const;

private:
    std::string text_;
};

// Interpreta um arquivo salvo sem nunca lancar excecao. Qualquer problema encontrado,
// inclusive nas consultas, faz valid() retornar false.
class KeyValueReader {
public:
    KeyValueReader(const std::string& text, const std::string& format, int version);

    bool valid() const { return valid_; }
    void markInvalid() { valid_ = false; }
    const std::map<std::string, std::string>& entries() const { return entries_; }

    std::string text(const std::string& key, const std::string& fallback);
    // Fora da faixa: o valor e limitado ao extremo mais proximo.
    int64_t clamped(const std::string& key, int64_t fallback, int64_t min, int64_t max);
    // Fora da faixa: o valor nao e confiavel e vira o padrao.
    int64_t bounded(const std::string& key, int64_t fallback, int64_t min, int64_t max);

private:
    std::map<std::string, std::string> entries_;
    bool valid_ = true;
};

// Conteudo lido do disco. Arquivo grande demais ou ilegivel existe, mas vem com texto vazio.
struct SaveFileContent {
    bool exists = false;
    std::string text;
};

SaveFileContent readSaveFile(const std::filesystem::path& path);

// Grava em um temporario e troca pelo definitivo com rename.
bool writeFileAtomic(const std::filesystem::path& path, const std::string& content);

// Guarda uma copia em .corrompido sem apagar copias anteriores.
bool preserveCorruptCopy(const std::filesystem::path& path);

// Substitui um arquivo salvo, preservando antes o conteudo atual se isValid o rejeitar.
bool replaceSaveFile(const std::filesystem::path& path, const std::string& content,
                     const std::function<bool(const std::string&)>& isValid);

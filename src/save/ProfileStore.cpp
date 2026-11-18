#include "save/ProfileStore.h"
#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

namespace {
const char* const kFormat = "perfil";
constexpr int kVersion = 1;
const char* const kExtension = ".perfil";

const std::array<const char*, kBirdUpgradeCount> kBirdKeys = {
    "passaro.velocidade", "passaro.agilidade", "passaro.carga", "passaro.alcance"};
const std::array<const char*, kNestUpgradeCount> kNestKeys = {
    "ninho.defesa", "ninho.alarme", "ninho.cofre", "ninho.tamanho"};

bool isSafeFileChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Codificacao reversivel: nomes diferentes sempre geram arquivos diferentes. Maiusculas
// tambem sao codificadas para continuarem distintas em sistemas que ignoram a caixa.
std::string encodeName(const std::string& name) {
    static const char* const digits = "0123456789abcdef";
    std::string encoded;
    for (const char c : name) {
        if (isSafeFileChar(c)) {
            encoded += c;
        } else {
            const auto byte = static_cast<unsigned char>(c);
            encoded += '%';
            encoded += digits[byte >> 4];
            encoded += digits[byte & 15];
        }
    }
    return encoded;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Retorna vazio para arquivos que nao foram gerados por encodeName.
std::string decodeName(const std::string& encoded) {
    std::string name;
    for (size_t i = 0; i < encoded.size(); i++) {
        if (encoded[i] != '%') {
            name += encoded[i];
            continue;
        }
        if (i + 2 >= encoded.size()) return "";
        const int high = hexValue(encoded[i + 1]);
        const int low = hexValue(encoded[i + 2]);
        if (high < 0 || low < 0) return "";
        name += static_cast<char>(high * 16 + low);
        i += 2;
    }
    return isValidProfileName(name) && encodeName(name) == encoded ? name : "";
}

// Semente de reserva estavel, para o passaro nao mudar a cada leitura de um arquivo danificado.
uint32_t seedFromName(const std::string& name) {
    uint32_t hash = 2166136261u;
    for (const char c : name) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    return hash;
}

std::string encodeProfile(const Profile& profile) {
    KeyValueWriter writer(kFormat, kVersion);
    writer.put("nome", profile.name());
    writer.put("semente", int64_t{profile.birdSeed()});
    writer.put("moedas", int64_t{profile.coins()});
    writer.put("total_entregue", int64_t{profile.totalDelivered()});
    for (size_t i = 0; i < kBirdKeys.size(); i++) {
        writer.put(kBirdKeys[i], int64_t{profile.level(static_cast<BirdUpgrade>(i))});
    }
    for (size_t i = 0; i < kNestKeys.size(); i++) {
        writer.put(kNestKeys[i], int64_t{profile.level(static_cast<NestUpgrade>(i))});
    }
    return writer.finish();
}

Profile decodeProfile(const std::string& text, const std::string& name, bool& valid) {
    KeyValueReader reader(text, kFormat, kVersion);
    if (reader.text("nome", "") != name) reader.markInvalid();
    const auto seed = static_cast<uint32_t>(reader.bounded("semente", seedFromName(name), 0, UINT32_MAX));
    // Contador absurdo nao e progresso real e nao pode contaminar o record: volta a zero.
    const int coins = static_cast<int>(reader.bounded("moedas", 0, 0, kMaxProfileCounter));
    const int total = static_cast<int>(reader.bounded("total_entregue", 0, 0, kMaxProfileCounter));

    BirdUpgradeLevels birdLevels{};
    NestUpgradeLevels nestLevels{};
    for (size_t i = 0; i < kBirdKeys.size(); i++) {
        birdLevels[i] = static_cast<int>(reader.clamped(kBirdKeys[i], 0, 0, kMaxUpgradeLevel));
    }
    for (size_t i = 0; i < kNestKeys.size(); i++) {
        nestLevels[i] = static_cast<int>(reader.clamped(kNestKeys[i], 0, 0, kMaxUpgradeLevel));
    }
    valid = reader.valid();
    return Profile::restored(name, seed, coins, total, birdLevels, nestLevels);
}
}

ProfileStore::ProfileStore(const fs::path& baseDir) : directory_(baseDir / "perfis") {
    std::error_code error;
    fs::create_directories(directory_, error);
}

std::string ProfileStore::fileNameFor(const std::string& name) {
    return isValidProfileName(name) ? encodeName(name) + kExtension : "";
}

bool ProfileStore::exists(const std::string& name) const {
    const std::string fileName = fileNameFor(name);
    std::error_code error;
    return !fileName.empty() && fs::exists(fs::symlink_status(directory_ / fileName, error));
}

std::vector<std::string> ProfileStore::list() const {
    std::vector<std::string> names;
    std::error_code error;
    fs::directory_iterator entry(directory_, error);
    for (; !error && entry != fs::directory_iterator(); entry.increment(error)) {
        std::error_code statusError;
        if (!entry->is_regular_file(statusError) || entry->path().extension() != kExtension) continue;
        const std::string name = decodeName(entry->path().stem().string());
        if (!name.empty()) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::optional<Profile> ProfileStore::create(const std::string& name, uint32_t birdSeed) {
    if (!isValidProfileName(name) || exists(name)) return std::nullopt;
    Profile profile(name, birdSeed);
    if (!save(profile)) return std::nullopt;
    return profile;
}

std::optional<Profile> ProfileStore::load(const std::string& name, LoadStatus* status) const {
    if (status) *status = LoadStatus::Missing;
    const std::string fileName = fileNameFor(name);
    if (fileName.empty()) return std::nullopt;
    const SaveFileContent content = readSaveFile(directory_ / fileName);
    if (!content.exists) return std::nullopt;

    bool valid = false;
    Profile profile = decodeProfile(content.text, name, valid);
    if (status) *status = valid ? LoadStatus::Ok : LoadStatus::Recovered;
    return profile;
}

bool ProfileStore::save(const Profile& profile) {
    const std::string fileName = fileNameFor(profile.name());
    if (fileName.empty()) return false;
    const auto isValid = [&](const std::string& text) {
        bool valid = false;
        decodeProfile(text, profile.name(), valid);
        return valid;
    };
    return replaceSaveFile(directory_ / fileName, encodeProfile(profile), isValid);
}

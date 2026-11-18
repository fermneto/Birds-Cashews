#include "save/WorldStore.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {
const char* const kFormat = "mundo";
constexpr int kVersion = 1;
const char* const kCashewPrefix = "caju.";
constexpr size_t kCashewPrefixSize = 5;

// Tempos ficam em milissegundos inteiros: o texto nao depende do idioma do sistema.
constexpr int64_t kMaxElapsedMs = static_cast<int64_t>(kMaxWorldElapsedSeconds * 1000.0);
constexpr int64_t kMaxRespawnMs = static_cast<int64_t>(kMaxCashewRespawnSeconds * 1000.0f);

int64_t toMilliseconds(double seconds, int64_t maxMs) {
    if (std::isnan(seconds)) return 0;
    const double limited = std::clamp(seconds, 0.0, static_cast<double>(maxMs) / 1000.0);
    return std::min<int64_t>(std::llround(limited * 1000.0), maxMs);
}

bool isCashewKey(const std::string& key) {
    return key.compare(0, kCashewPrefixSize, kCashewPrefix) == 0;
}

// Interpreta o id em "caju.<id>", recusando zeros a esquerda e o id reservado.
bool parseCashewKey(const std::string& key, CashewId& id) {
    const std::string digits = key.substr(kCashewPrefixSize);
    unsigned value = 0;
    const char* const end = digits.data() + digits.size();
    const auto [next, error] = std::from_chars(digits.data(), end, value);
    if (error != std::errc() || next != end || value >= kNoCashew || std::to_string(value) != digits) return false;
    id = static_cast<CashewId>(value);
    return true;
}

std::string encodeWorld(const WorldState& state) {
    // Id repetido mantem a espera mais longa, e o id reservado nao representa um caju.
    std::map<CashewId, int64_t> cashews;
    for (const UnavailableCashew& cashew : state.unavailable) {
        if (cashew.id == kNoCashew) continue;
        int64_t& waitMs = cashews[cashew.id];
        waitMs = std::max(waitMs, toMilliseconds(cashew.secondsLeft, kMaxRespawnMs));
    }

    KeyValueWriter writer(kFormat, kVersion);
    writer.put("semente", int64_t{state.seed});
    writer.put("tempo_ms", toMilliseconds(state.elapsedSeconds, kMaxElapsedMs));
    for (const auto& [id, waitMs] : cashews) writer.put(kCashewPrefix + std::to_string(id), waitMs);
    return writer.finish();
}

WorldState decodeWorld(const std::string& text, uint32_t fallbackSeed, bool& valid) {
    KeyValueReader reader(text, kFormat, kVersion);
    WorldState state;
    state.seed = static_cast<uint32_t>(reader.bounded("semente", fallbackSeed, 0, UINT32_MAX));
    state.elapsedSeconds = static_cast<double>(reader.clamped("tempo_ms", 0, 0, kMaxElapsedMs)) / 1000.0;

    std::map<CashewId, float> cashews;
    for (const auto& entry : reader.entries()) {
        if (!isCashewKey(entry.first)) continue;
        CashewId id = kNoCashew;
        if (!parseCashewKey(entry.first, id)) {
            reader.markInvalid();
            continue;
        }
        const int64_t waitMs = reader.clamped(entry.first, 0, 0, kMaxRespawnMs);
        cashews[id] = static_cast<float>(static_cast<double>(waitMs) / 1000.0);
    }
    for (const auto& [id, secondsLeft] : cashews) state.unavailable.push_back({id, secondsLeft});
    valid = reader.valid();
    return state;
}
}

WorldStore::WorldStore(const fs::path& baseDir) : path_(baseDir / "mundo.txt") {
    std::error_code error;
    fs::create_directories(baseDir, error);
}

bool WorldStore::exists() const {
    std::error_code error;
    return fs::is_regular_file(path_, error);
}

WorldState WorldStore::load(uint32_t fallbackSeed, LoadStatus* status) const {
    const SaveFileContent content = readSaveFile(path_);
    if (!content.exists) {
        if (status) *status = LoadStatus::Missing;
        WorldState state;
        state.seed = fallbackSeed;
        return state;
    }
    bool valid = false;
    WorldState state = decodeWorld(content.text, fallbackSeed, valid);
    if (status) *status = valid ? LoadStatus::Ok : LoadStatus::Recovered;
    return state;
}

bool WorldStore::save(const WorldState& state) {
    const auto isValid = [](const std::string& text) {
        bool valid = false;
        decodeWorld(text, 0, valid);
        return valid;
    };
    return replaceSaveFile(path_, encodeWorld(state), isValid);
}

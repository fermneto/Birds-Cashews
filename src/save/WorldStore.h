#pragma once
#include "save/KeyValueFile.h"
#include "shared/GameTypes.h"
#include <filesystem>
#include <vector>

constexpr double kMaxWorldElapsedSeconds = 315360000.0;  // dez anos
constexpr float kMaxCashewRespawnSeconds = 86400.0f;     // um dia

// Caju que ainda nao reapareceu no mundo.
struct UnavailableCashew {
    CashewId id = kNoCashew;
    float secondsLeft = 0;
};

struct WorldState {
    uint32_t seed = 0;
    double elapsedSeconds = 0;
    std::vector<UnavailableCashew> unavailable;  // em ordem crescente de id ao carregar
};

// Guarda o estado do mundo do host em <pasta base>/mundo.txt.
class WorldStore {
public:
    explicit WorldStore(const std::filesystem::path& baseDir);

    bool exists() const;

    // Sem arquivo ou sem semente confiavel, o mundo usa fallbackSeed.
    WorldState load(uint32_t fallbackSeed, LoadStatus* status = nullptr) const;

    bool save(const WorldState& state);

private:
    std::filesystem::path path_;
};

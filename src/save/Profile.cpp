#include "save/Profile.h"
#include <algorithm>
#include <utility>

namespace {
// Soma sem estourar o limite dos contadores.
int addToCounter(int counter, int amount) {
    return static_cast<int>(std::min<int64_t>(int64_t{counter} + amount, kMaxProfileCounter));
}
}

bool isValidProfileName(const std::string& name) {
    if (name.empty() || name.size() > kMaxProfileNameBytes) return false;
    const auto isControl = [](unsigned char c) { return c < 0x20 || c == 0x7f; };
    if (std::any_of(name.begin(), name.end(), isControl)) return false;
    return name.find_first_not_of(' ') != std::string::npos;
}

Profile::Profile(std::string name, uint32_t birdSeed) : name_(std::move(name)), birdSeed_(birdSeed) {}

Profile Profile::restored(std::string name, uint32_t birdSeed, int coins, int totalDelivered,
                          const BirdUpgradeLevels& birdLevels, const NestUpgradeLevels& nestLevels) {
    Profile profile(std::move(name), birdSeed);
    profile.coins_ = std::clamp(coins, 0, kMaxProfileCounter);
    profile.totalDelivered_ = std::clamp(totalDelivered, 0, kMaxProfileCounter);
    for (size_t i = 0; i < birdLevels.size(); i++) {
        profile.birdLevels_[i] = std::clamp(birdLevels[i], 0, kMaxUpgradeLevel);
    }
    for (size_t i = 0; i < nestLevels.size(); i++) {
        profile.nestLevels_[i] = std::clamp(nestLevels[i], 0, kMaxUpgradeLevel);
    }
    return profile;
}

bool Profile::upgrade(BirdUpgrade upgrade) {
    if (isMaxed(upgrade)) return false;
    birdLevels_[static_cast<size_t>(upgrade)]++;
    return true;
}

bool Profile::upgrade(NestUpgrade upgrade) {
    if (isMaxed(upgrade)) return false;
    nestLevels_[static_cast<size_t>(upgrade)]++;
    return true;
}

int Profile::deliverCashews(int delivered, int stolen) {
    if (delivered <= 0) return 0;
    const int legitimate = delivered - std::clamp(stolen, 0, delivered);
    const int coinsBefore = coins_;
    coins_ = addToCounter(coins_, delivered);
    totalDelivered_ = addToCounter(totalDelivered_, legitimate);
    return coins_ - coinsBefore;
}

bool Profile::spendCoins(int amount) {
    if (amount < 0 || amount > coins_) return false;
    coins_ -= amount;
    return true;
}

#include "game/NestEconomy.h"
#include <algorithm>
#include <array>
#include <cstddef>

namespace {
using PriceTable = std::array<int, kMaxUpgradeLevel>;

// Ajuste de economia: cada coluna e o preco de comprar o nivel 1, 2, 3, 4 e 5.
const std::array<PriceTable, kBirdUpgradeCount> kBirdPrices = {{
    {3, 6, 10, 15, 22},   // Velocidade
    {2, 5, 9, 14, 20},    // Agilidade
    {4, 8, 13, 19, 26},   // Capacidade
    {2, 4, 8, 13, 18},    // Alcance
}};
const std::array<PriceTable, kNestUpgradeCount> kNestPrices = {{
    {3, 6, 10, 15, 22},   // Defesa
    {3, 6, 10, 15, 22},   // Alarme
    {4, 8, 13, 19, 26},   // Cofre
    {3, 7, 12, 18, 25},   // Tamanho
}};

const std::array<const char*, kBirdUpgradeCount> kBirdNames = {"Velocidade", "Agilidade", "Capacidade", "Alcance"};
const std::array<const char*, kNestUpgradeCount> kNestNames = {"Defesa", "Alarme", "Cofre", "Tamanho"};

constexpr float kSpeedPerLevel = 0.08f, kAgilityPerLevel = 0.12f;
constexpr int kBaseCapacity = 3, kCapacityPerLevel = 1;
constexpr float kBaseCollectRadius = 9.0f, kCollectRadiusPerLevel = 2.5f;
constexpr float kNestScalePerLevel = 0.18f;
constexpr int kBonusPercentPerLevel = 20;
constexpr float kBaseRobSeconds = 4.0f, kRobSecondsPerLevel = 1.5f;
constexpr int kVaultCoinsPerLevel = 5;
constexpr float kBaseAlarmSeconds = 5.0f, kAlarmSecondsPerLevel = 3.0f;

int clampLevel(int level) { return std::clamp(level, 0, kMaxUpgradeLevel); }

int priceAt(const PriceTable& table, int level) {
    return level >= 0 && level < kMaxUpgradeLevel ? table[static_cast<size_t>(level)] : 0;
}
}

int upgradePrice(BirdUpgrade upgrade, int level) { return priceAt(kBirdPrices[static_cast<size_t>(upgrade)], level); }
int upgradePrice(NestUpgrade upgrade, int level) { return priceAt(kNestPrices[static_cast<size_t>(upgrade)], level); }

const char* upgradeName(BirdUpgrade upgrade) { return kBirdNames[static_cast<size_t>(upgrade)]; }
const char* upgradeName(NestUpgrade upgrade) { return kNestNames[static_cast<size_t>(upgrade)]; }

int upgradeCode(BirdUpgrade upgrade) { return static_cast<int>(upgrade); }
int upgradeCode(NestUpgrade upgrade) { return kBirdUpgradeCount + static_cast<int>(upgrade); }

float speedMultiplier(int level) { return 1.0f + kSpeedPerLevel * level; }
float agilityMultiplier(int level) { return 1.0f + kAgilityPerLevel * level; }
int carryCapacity(int level) { return kBaseCapacity + kCapacityPerLevel * level; }
float collectRadius(int level) { return kBaseCollectRadius + kCollectRadiusPerLevel * level; }
float nestScale(int sizeLevel) { return 1.0f + kNestScalePerLevel * sizeLevel; }

// Arredonda para cima: o primeiro nivel ja rende moeda em qualquer entrega.
int sizeCoinBonus(int delivered, int sizeLevel) {
    if (delivered <= 0 || sizeLevel <= 0) return 0;
    return (delivered * sizeLevel * kBonusPercentPerLevel + 99) / 100;
}

float robSeconds(int defenseLevel) { return kBaseRobSeconds + kRobSecondsPerLevel * clampLevel(defenseLevel); }
int vaultProtection(int vaultLevel) { return kVaultCoinsPerLevel * clampLevel(vaultLevel); }
int robbableCoins(int coins, int vaultLevel) { return std::max(0, coins - vaultProtection(vaultLevel)); }

float alarmSeconds(int alarmLevel) {
    return alarmLevel <= 0 ? 0.0f : kBaseAlarmSeconds + kAlarmSecondsPerLevel * clampLevel(alarmLevel);
}

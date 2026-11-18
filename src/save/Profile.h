#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

enum class BirdUpgrade { Speed, Agility, CarryCapacity, CollectRange };
enum class NestUpgrade { Defense, Alarm, Vault, Size };

constexpr int kBirdUpgradeCount = 4;
constexpr int kNestUpgradeCount = 4;
constexpr int kMaxUpgradeLevel = 5;
constexpr int kMaxProfileCounter = 1000000000;
constexpr size_t kMaxProfileNameBytes = 64;

using BirdUpgradeLevels = std::array<int, kBirdUpgradeCount>;
using NestUpgradeLevels = std::array<int, kNestUpgradeCount>;

// Aceita de 1 a 64 bytes, sem caracteres de controle e com algo alem de espacos.
bool isValidProfileName(const std::string& name);

// Progresso local do jogador. Os precos dos upgrades ficam com a economia do ninho.
class Profile {
public:
    Profile(std::string name, uint32_t birdSeed);

    // Reconstroi um perfil salvo, limitando cada valor a sua faixa.
    static Profile restored(std::string name, uint32_t birdSeed, int coins, int totalDelivered,
                            const BirdUpgradeLevels& birdLevels, const NestUpgradeLevels& nestLevels);

    const std::string& name() const { return name_; }
    uint32_t birdSeed() const { return birdSeed_; }
    int coins() const { return coins_; }
    int totalDelivered() const { return totalDelivered_; }

    int level(BirdUpgrade upgrade) const { return birdLevels_[static_cast<size_t>(upgrade)]; }
    int level(NestUpgrade upgrade) const { return nestLevels_[static_cast<size_t>(upgrade)]; }
    bool isMaxed(BirdUpgrade upgrade) const { return level(upgrade) >= kMaxUpgradeLevel; }
    bool isMaxed(NestUpgrade upgrade) const { return level(upgrade) >= kMaxUpgradeLevel; }

    // Sobe um nivel. Retorna false se o upgrade ja estiver no maximo.
    bool upgrade(BirdUpgrade upgrade);
    bool upgrade(NestUpgrade upgrade);

    // Todos os cajus viram moeda, mas os roubados nao somam no total nem no record.
    // Retorna as moedas recebidas.
    int deliverCashews(int delivered, int stolen);

    // Retorna false, sem alterar o saldo, se nao houver moedas suficientes.
    bool spendCoins(int amount);

private:
    std::string name_;
    uint32_t birdSeed_;
    int coins_ = 0;
    int totalDelivered_ = 0;
    BirdUpgradeLevels birdLevels_{};
    NestUpgradeLevels nestLevels_{};
};

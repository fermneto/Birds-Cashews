#pragma once
#include "vision/GestureRecognizer.h"
#include <string>
#include <vector>

enum class NestMenuPage { Closed, Root, Eat, Invest };

struct UpgradeOptionView {
    std::string name;
    int level = 0, maxLevel = 5, price = 0;
    bool affordable = false;
};

// Menu do ninho. O braco esquerdo e o direito executam as acoes descritas nos rotulos.
struct NestMenuView {
    NestMenuPage page = NestMenuPage::Closed;
    std::vector<UpgradeOptionView> options;
    int selected = 0;
    std::string leftLabel, rightLabel;
    ArmSide raisedArm = ArmSide::None;
    float holdProgress = 0;
};

// Tudo que a interface precisa mostrar sobre a partida.
struct GameplayView {
    std::string playerName;
    int carried = 0, capacity = 3;
    int coins = 0;
    int totalDelivered = 0;    // Pontuacao atual.
    int record = 0;
    std::string recordHolder;
    bool leadingRecord = false;
    bool recordBeatenNow = false;   // Verdadeiro enquanto o aviso deve aparecer.
    bool perchedOnNest = false;
    float nestDistance = 0;    // Metros ate o ninho.
    float nestBearing = 0;     // Radianos em relacao a frente do passaro; positivo e a direita.
    NestMenuView menu;
};

enum class GameplayFeedback { CashewCollected, CashewDelivered, UpgradeBought, UpgradeDenied,
                              RecordBeaten, MenuMove, NestLanded, TakeOff };

// Ligacao entre as regras da partida (N3) e a interface (N4).
class GameplaySource {
public:
    virtual ~GameplaySource() = default;
    virtual void startProfile(const std::string& name) = 0;
    virtual void saveNow() = 0;
    virtual const GameplayView& view() const = 0;
    virtual std::vector<GameplayFeedback> drainFeedback() = 0;   // Cada aviso sai uma unica vez.
};

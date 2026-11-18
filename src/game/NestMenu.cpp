#include "game/NestMenu.h"
#include "game/NestEconomy.h"
#include <string>

namespace {
template <typename Upgrade>
UpgradeOptionView describeOption(Upgrade upgrade, const Profile& profile) {
    UpgradeOptionView option;
    option.name = upgradeName(upgrade);
    option.level = profile.level(upgrade);
    option.maxLevel = kMaxUpgradeLevel;
    option.price = upgradePrice(upgrade, option.level);
    option.affordable = !profile.isMaxed(upgrade) && profile.coins() >= option.price;
    return option;
}

// A opcao de voltar nao tem niveis: maxLevel 0 a diferencia dos upgrades.
UpgradeOptionView backOption() {
    UpgradeOptionView option;
    option.name = "Voltar";
    option.maxLevel = 0;
    option.affordable = true;
    return option;
}

std::string buyLabel(const UpgradeOptionView& option, int coins) {
    if (option.level >= option.maxLevel) return "Nivel maximo";
    if (!option.affordable) return "Faltam " + std::to_string(option.price - coins) + " moedas";
    return "Comprar por " + std::to_string(option.price);
}
}

void NestMenu::open() {
    page_ = NestMenuPage::Root;
    selected_ = 0;
}

void NestMenu::close() {
    page_ = NestMenuPage::Closed;
    selected_ = 0;
}

NestMenuAction NestMenu::press(ArmSide side) {
    if (side == ArmSide::None || page_ == NestMenuPage::Closed) return NestMenuAction::None;
    return page_ == NestMenuPage::Root ? pressOnRoot(side) : pressOnList(side);
}

NestMenuAction NestMenu::pressOnRoot(ArmSide side) {
    page_ = side == ArmSide::Left ? NestMenuPage::Eat : NestMenuPage::Invest;
    selected_ = 0;
    return NestMenuAction::Moved;
}

NestMenuAction NestMenu::pressOnList(ArmSide side) {
    if (side == ArmSide::Left) {
        selected_ = (selected_ + 1) % kOptionCount;
        return NestMenuAction::Moved;
    }
    if (selected_ != kBackOption) return NestMenuAction::Buy;
    open();
    return NestMenuAction::Moved;
}

NestMenuView describeNestMenu(const NestMenu& menu, const Profile& profile) {
    NestMenuView view;
    view.page = menu.page();
    view.selected = menu.selected();
    if (menu.page() == NestMenuPage::Closed) return view;
    if (menu.page() == NestMenuPage::Root) {
        view.leftLabel = "Comer";
        view.rightLabel = "Investir";
        return view;
    }
    for (int i = 0; i < NestMenu::kBackOption; i++) {
        view.options.push_back(menu.page() == NestMenuPage::Eat ? describeOption(static_cast<BirdUpgrade>(i), profile)
                                                                : describeOption(static_cast<NestUpgrade>(i), profile));
    }
    view.options.push_back(backOption());
    view.leftLabel = "Proxima opcao";
    view.rightLabel = menu.selected() == NestMenu::kBackOption ? "Voltar"
                                                               : buyLabel(view.options[menu.selected()], profile.coins());
    return view;
}

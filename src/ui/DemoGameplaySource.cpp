#include "ui/DemoGameplaySource.h"
#include <utility>

std::vector<GameplayFeedback> DemoGameplaySource::drainFeedback() {
    return std::exchange(pending_, {});
}

GameplayView DemoGameplaySource::flightSample() {
    GameplayView view;
    view.playerName = "Jogador";
    view.carried = 2;
    view.capacity = 3;
    view.coins = 14;
    view.totalDelivered = 27;
    view.record = 41;
    view.recordHolder = "Ana";
    view.nestDistance = 128.0f;
    view.nestBearing = 0.7f;
    return view;
}

GameplayView DemoGameplaySource::recordSample() {
    GameplayView view = flightSample();
    view.totalDelivered = 42;
    view.record = 42;
    view.recordHolder = view.playerName;
    view.leadingRecord = true;
    view.recordBeatenNow = true;
    return view;
}

GameplayView DemoGameplaySource::nestRootSample() {
    GameplayView view = flightSample();
    view.carried = 0;
    view.perchedOnNest = true;
    view.nestDistance = 0.0f;
    view.menu.page = NestMenuPage::Root;
    view.menu.selected = 0;
    view.menu.leftLabel = "Comer";
    view.menu.rightLabel = "Investir";
    return view;
}

GameplayView DemoGameplaySource::nestEatSample() {
    GameplayView view = nestRootSample();
    view.menu.page = NestMenuPage::Eat;
    view.menu.options = {{"Velocidade", 2, 5, 10, true}, {"Agilidade", 1, 5, 5, true}, {"Capacidade", 5, 5, 0, false},
                         {"Alcance", 3, 5, 18, false}, {"Voltar", 0, 0, 0, true}};
    view.menu.selected = 1;
    view.menu.leftLabel = "Próxima opção";
    view.menu.rightLabel = "Comprar por 5";
    view.menu.raisedArm = ArmSide::Right;
    view.menu.holdProgress = 0.6f;
    return view;
}

GameplayView DemoGameplaySource::nestInvestSample() {
    GameplayView view = nestRootSample();
    view.menu.page = NestMenuPage::Invest;
    view.menu.options = {{"Defesa", 1, 5, 6, true}, {"Alarme", 0, 5, 3, true}, {"Cofre", 4, 5, 26, false},
                         {"Tamanho", 2, 5, 12, true}, {"Voltar", 0, 0, 0, true}};
    view.menu.selected = 2;
    view.menu.leftLabel = "Próxima opção";
    view.menu.rightLabel = "Faltam 12 moedas";
    view.menu.raisedArm = ArmSide::Left;
    view.menu.holdProgress = 0.35f;
    return view;
}

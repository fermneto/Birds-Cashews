#include "ui/UiWidgets.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kHalfTurn = 3.14159265f;
constexpr int kCargoIcon = 34, kCargoStep = 36;
constexpr int kCompassHalf = 170, kCompassY = 42;
const cv::Point kBannerCenter(640, 184);
const cv::Rect kNestColumn(884, 200, 356, 484);

void drawCargo(UiKit& kit, const GameplayView& view) {
    const int y = kUiMargin - 12;
    for (int i = 0; i < view.capacity; i++)
        kit.canvas.image(kit.assets.icon(UiIcon::Cashew, kCargoIcon), {kUiMargin + i * kCargoStep, y}, i < view.carried ? 1.0f : 0.28f);
    if (view.carried >= view.capacity)
        kit.text.draw(kit.canvas, "Carga cheia", {kUiMargin + view.capacity * kCargoStep + 8, y + 24}, uiStyle(16, theme::kGold));
}

// Pontuacao e record ficam em uma unica linha discreta. O destaque do record e so a notificacao.
void drawScore(UiKit& kit, const GameplayView& view) {
    const std::string score = std::to_string(view.totalDelivered);
    kit.text.draw(kit.canvas, score, {kUiMargin, 100}, uiStyle(30));
    kit.text.draw(kit.canvas, "record " + std::to_string(view.record), {kUiMargin + kit.text.width(score, 30) + 12, 100},
                  uiStyle(15, theme::kMuted));
}

void drawCoins(UiKit& kit, const GameplayView& view, float opacity) {
    if (opacity <= 0.0f) return;
    kit.canvas.image(kit.assets.icon(UiIcon::Coin, 24), {kUiMargin - 1, 112}, opacity);
    kit.text.draw(kit.canvas, std::to_string(view.coins), {kUiMargin + 30, 131}, uiStyle(19, theme::kGold), opacity);
}

// Com o Alarme, um losango vermelho mostra para que lado o ladrao esta.
void drawThief(UiKit& kit, const GameplayView& view) {
    if (!view.alarmNow) return;
    const int center = kUiWidth / 2;
    const float limited = std::clamp(view.thiefBearing / (kHalfTurn * 0.5f), -1.0f, 1.0f);
    const int x = center + static_cast<int>(limited * (kCompassHalf - 14));
    kit.canvas.fill({x - 9, kCompassY - 11, 18, 18}, theme::kRed, 1.0f, 4);
    kit.canvas.fill({x - 5, kCompassY - 7, 10, 10}, theme::kWhite, 0.9f, 3);
    const std::string distance = "ladrão " + std::to_string(static_cast<int>(std::lround(std::max(view.thiefDistance, 0.0f)))) + " m";
    kit.text.draw(kit.canvas, distance, {x, kCompassY - 18}, uiStyle(15, theme::kRed, TextAlign::Center));
}

// Barra de rumo: o ninho desliza sobre ela e, se ficar para tras, encosta na ponta indicando o lado.
void drawCompass(UiKit& kit, const GameplayView& view) {
    if (view.perchedOnNest || view.robbery.active) return;
    const float opacity = view.carried > 0 ? 1.0f : 0.62f;
    const int center = kUiWidth / 2;
    kit.canvas.gradient({center - kCompassHalf, kCompassY, kCompassHalf, 2}, theme::kWhite, 0.0f, 0.75f * opacity, true);
    kit.canvas.gradient({center, kCompassY, kCompassHalf, 2}, theme::kWhite, 0.75f * opacity, 0.0f, true);
    for (int tick : {-2, -1, 1, 2})
        kit.canvas.fill({center + tick * kCompassHalf / 3 - 1, kCompassY - 4, 2, 10}, theme::kWhite, 0.42f * opacity);
    kit.canvas.fill({center - 1, kCompassY - 9, 3, 20}, theme::kGold, opacity);

    const float limited = std::clamp(view.nestBearing / (kHalfTurn * 0.5f), -1.0f, 1.0f);
    const int x = center + static_cast<int>(limited * (kCompassHalf - 14));
    kit.canvas.imageCentered(kit.assets.icon(UiIcon::Nest, 38), {x, kCompassY - 2}, opacity);
    if (std::abs(limited) >= 1.0f) {
        const float side = limited > 0.0f ? kHalfTurn * 0.5f : -kHalfTurn * 0.5f;
        kit.canvas.imageCentered(rotatedClockwise(kit.assets.icon(UiIcon::CompassArrow, 26), side),
                                 {x + (limited > 0.0f ? 30 : -30), kCompassY - 2}, opacity);
    }
    const std::string distance = std::to_string(static_cast<int>(std::lround(std::max(view.nestDistance, 0.0f)))) + " m";
    kit.text.draw(kit.canvas, distance, {center, kCompassY + 36}, uiStyle(17, theme::kWhite, TextAlign::Center), opacity);
}

std::string robStatus(const RobView& rob) {
    switch (rob.blocked) {
        case RobBlock::EmptyNest: return "Nada para levar: o ninho está vazio ou no Cofre";
        case RobBlock::CargoFull: return "Carga cheia: leve tudo para o seu ninho";
        case RobBlock::Waiting: return "Pegando o caju...";
        case RobBlock::None: break;
    }
    return "Fique pousado para levar um caju";
}

// Pousado no ninho de um rival: barra de progresso ate o proximo caju roubado.
void drawRobbery(UiKit& kit, const GameplayView& view) {
    const RobView& rob = view.robbery;
    if (!rob.active) return;
    const cv::Rect panel((kUiWidth - 460) / 2, 520, 460, 132);
    kit.canvas.fill(panel, theme::kInk, 0.82f, 22);
    kit.text.draw(kit.canvas, "Ninho rival", {panel.x + 24, panel.y + 36}, uiStyle(24, theme::kGold));
    const std::string count = std::to_string(rob.stolenNow) + " roubado" + (rob.stolenNow == 1 ? "" : "s");
    kit.text.draw(kit.canvas, count, {panel.x + panel.width - 24, panel.y + 36}, uiStyle(16, theme::kMuted, TextAlign::Right));
    const cv::Rect bar(panel.x + 24, panel.y + 54, panel.width - 48, 12);
    kit.canvas.fill(bar, theme::kWhite, 0.18f, 6);
    const int filled = static_cast<int>(std::lround(bar.width * std::clamp(rob.progress, 0.0f, 1.0f)));
    if (filled > 0) kit.canvas.fill({bar.x, bar.y, filled, bar.height}, rob.blocked == RobBlock::None || rob.blocked == RobBlock::Waiting ? theme::kGold : theme::kMuted, 1.0f, 6);
    kit.text.draw(kit.canvas, robStatus(rob), {panel.x + 24, panel.y + 92}, uiStyle(16));
    kit.text.draw(kit.canvas, "Bata as asas (Espaço) para fugir", {panel.x + 24, panel.y + 116}, uiStyle(14, theme::kMuted));
}

std::string cargoLostText(CargoLossCause cause) {
    switch (cause) {
        case CargoLossCause::Player: return "Um rival mergulhou em você e derrubou a carga!";
        case CargoLossCause::Tree: return "Você bateu na árvore e derrubou um caju!";
        case CargoLossCause::Hunter: break;
    }
    return "Um caçador derrubou a sua carga!";
}

void drawWarning(UiKit& kit, const std::string& text) {
    const int width = kit.text.width(text, 18) + 58;
    const cv::Rect pill((kUiWidth - width) / 2, 96, width, 40);
    kit.canvas.fill(pill, theme::kInk, 0.78f, 20);
    kit.canvas.fill({pill.x + 16, pill.y + 14, 12, 12}, theme::kRed, 1.0f, 6);
    TextStyle label = uiStyle(18);
    label.outline = false;
    kit.text.draw(kit.canvas, text, {pill.x + 40, pill.y + 27}, label);
}

// Notificacao curta: aparece quando o record e batido e some sozinha.
cv::Mat bannerArt(UiKit& kit, int total) {
    Canvas art(336, 72);
    art.fill({0, 0, 336, 72}, theme::kInk, 0.86f, 36);
    art.image(kit.assets.icon(UiIcon::RecordBadge, 52), {12, 10});
    TextStyle title = uiStyle(22, theme::kGold), detail = uiStyle(14, theme::kMuted);
    title.outline = detail.outline = false;
    kit.text.draw(art, "Novo record!", {76, 34}, title);
    kit.text.draw(art, std::to_string(total) + " cajus entregues", {76, 56}, detail);
    return art.pixels();
}

void drawBanner(UiKit& kit, const HudScene& scene) {
    if (scene.bannerOpacity <= 0.0f) return;
    const cv::Mat art = bannerArt(kit, scene.view->totalDelivered);
    cv::Mat scaled;
    cv::resize(art, scaled, cv::Size(), scene.bannerScale, scene.bannerScale, cv::INTER_LINEAR);
    kit.canvas.imageCentered(scaled, kBannerCenter, scene.bannerOpacity);
}

void drawLevelDots(UiKit& kit, cv::Point origin, int level, int maxLevel) {
    for (int i = 0; i < maxLevel; i++)
        kit.canvas.fill({origin.x + i * 14, origin.y, 9, 5}, i < level ? theme::kGold : theme::kWhite, i < level ? 1.0f : 0.24f, 2);
}

void drawPrice(UiKit& kit, const UpgradeOptionView& option, cv::Point rightCenter, bool selected) {
    if (option.maxLevel > 0 && option.level >= option.maxLevel) {
        kit.text.draw(kit.canvas, "MÁX", rightCenter + cv::Point(0, 7), uiStyle(17, theme::kGreen, TextAlign::Right));
    } else if (option.price > 0) {
        const std::string price = std::to_string(option.price);
        const cv::Scalar color = option.affordable || selected ? theme::kGold : theme::kMuted;
        kit.text.draw(kit.canvas, price, rightCenter + cv::Point(0, 8), uiStyle(21, color, TextAlign::Right));
        const int x = rightCenter.x - kit.text.width(price, 21) - 27;
        kit.canvas.image(kit.assets.icon(UiIcon::Coin, 22), {x, rightCenter.y - 11}, option.affordable ? 1.0f : 0.5f);
    }
}

void drawNestOption(UiKit& kit, const UpgradeOptionView& option, const cv::Rect& area, bool selected) {
    if (selected) {
        kit.canvas.gradient(area, theme::kAccent, 0.9f, 0.18f, true);
        kit.canvas.fill({area.x, area.y, 4, area.height}, theme::kGold, 1.0f);
    }
    const auto icon = iconForOption(option.name);
    if (icon) kit.canvas.image(kit.assets.icon(*icon, 36), {area.x + 12, area.y + (area.height - 36) / 2}, selected ? 1.0f : 0.8f);
    const int textX = area.x + 58, room = area.width - 138;
    const bool upgrade = option.maxLevel > 0;
    const int baseline = upgrade ? area.y + 25 : area.y + area.height / 2 + 7;
    kit.text.drawFit(kit.canvas, option.name, {textX, baseline}, room, uiStyle(19, selected ? theme::kWhite : theme::kMuted));
    if (upgrade) drawLevelDots(kit, {textX, area.y + area.height - 15}, option.level, option.maxLevel);
    drawPrice(kit, option, {area.x + area.width - 10, area.y + area.height / 2}, selected);
}

void drawNestChoice(UiKit& kit, cv::Point center, UiIcon icon, const std::string& label, const std::string& arm) {
    kit.canvas.imageCentered(kit.assets.icon(icon, 84), center);
    kit.text.draw(kit.canvas, label, center + cv::Point(0, 76), uiStyle(25, theme::kGold, TextAlign::Center));
    kit.text.draw(kit.canvas, arm, center + cv::Point(0, 102), uiStyle(14, theme::kMuted, TextAlign::Center));
}

void drawNestRoot(UiKit& kit, const NestMenuView& menu, int top) {
    const int quarter = kNestColumn.width / 4;
    drawNestChoice(kit, {kNestColumn.x + quarter, top + 62}, UiIcon::Cashew, menu.leftLabel, "Braço esquerdo");
    drawNestChoice(kit, {kNestColumn.x + 3 * quarter, top + 62}, UiIcon::Nest, menu.rightLabel, "Braço direito");
    kit.canvas.fill({kNestColumn.x + 2 * quarter, top + 18, 1, 150}, theme::kWhite, 0.2f);
    kit.text.draw(kit.canvas, "Bata as asas (Espaço) para decolar", {kNestColumn.x + 2 * quarter, top + 222},
                  uiStyle(16, theme::kWhite, TextAlign::Center));
}

std::string nestTitle(NestMenuPage page) {
    switch (page) {
        case NestMenuPage::Eat: return "Comer";
        case NestMenuPage::Invest: return "Investir";
        default: return "Ninho";
    }
}

std::string nestSubtitle(NestMenuPage page) {
    switch (page) {
        case NestMenuPage::Eat: return "Melhora o pássaro";
        case NestMenuPage::Invest: return "Melhora o ninho";
        default: return "Use as moedas dos cajus entregues";
    }
}

void drawNestMenu(UiKit& kit, const GameplayView& view) {
    const NestMenuView& menu = view.menu;
    kit.canvas.gradient({600, 0, 270, kUiHeight}, theme::kInk, 0.0f, 0.9f, true);
    kit.canvas.fill({870, 0, kUiWidth - 870, kUiHeight}, theme::kInk, 0.9f);

    const int right = kNestColumn.x + kNestColumn.width;
    kit.text.draw(kit.canvas, nestTitle(menu.page), {kNestColumn.x, kNestColumn.y + 30}, uiStyle(32));
    kit.text.draw(kit.canvas, nestSubtitle(menu.page), {kNestColumn.x, kNestColumn.y + 54}, uiStyle(15, theme::kMuted));
    const std::string coins = std::to_string(view.coins);
    kit.text.draw(kit.canvas, coins, {right, kNestColumn.y + 30}, uiStyle(26, theme::kGold, TextAlign::Right));
    kit.canvas.image(kit.assets.icon(UiIcon::Coin, 28), {right - kit.text.width(coins, 26) - 34, kNestColumn.y + 7});

    constexpr int kListTop = 70, kPromptHeight = 48, kPromptGap = 6;
    const int promptsY = kNestColumn.y + kNestColumn.height - 2 * kPromptHeight - kPromptGap;
    const int top = kNestColumn.y + kListTop;
    if (menu.page == NestMenuPage::Root) drawNestRoot(kit, menu, top);
    const int count = std::max(static_cast<int>(menu.options.size()), 1);
    const int rowHeight = std::min((promptsY - top - 8) / count, 58);
    for (int i = 0; i < static_cast<int>(menu.options.size()); i++)
        drawNestOption(kit, menu.options[i], {kNestColumn.x, top + i * rowHeight, kNestColumn.width, rowHeight - 3}, i == menu.selected);

    drawArmPrompt(kit, {kNestColumn.x, promptsY, kNestColumn.width, kPromptHeight}, ArmSide::Left, menu.leftLabel,
                  menu.raisedArm == ArmSide::Left, menu.holdProgress);
    drawArmPrompt(kit, {kNestColumn.x, promptsY + kPromptHeight + kPromptGap, kNestColumn.width, kPromptHeight}, ArmSide::Right,
                  menu.rightLabel, menu.raisedArm == ArmSide::Right, menu.holdProgress);
}
}

void drawHudScene(UiKit& kit, const HudScene& scene) {
    const GameplayView& view = *scene.view;
    if (view.menu.page != NestMenuPage::Closed) drawNestMenu(kit, view);
    drawCargo(kit, view);
    drawScore(kit, view);
    drawCoins(kit, view, view.menu.page != NestMenuPage::Closed ? 0.0f : scene.coinsOpacity);
    drawCompass(kit, view);
    drawThief(kit, view);
    drawRobbery(kit, view);
    if (scene.showPreview) drawPreviewFrame(kit);
    if (scene.bodyLost) drawWarning(kit, "Corpo fora do quadro: o pássaro plana nivelado");
    else if (view.nearBoundary) drawWarning(kit, "Volte para a ilha");
    else if (view.cargoLostNow) drawWarning(kit, cargoLostText(view.cargoLossCause));
    else if (view.alarmNow) drawWarning(kit, "Alarme: estão roubando o seu ninho!");
    else if (view.knockedRivalNow) drawWarning(kit, "Mergulho certeiro: a carga do rival caiu!");
    drawBanner(kit, scene);
}

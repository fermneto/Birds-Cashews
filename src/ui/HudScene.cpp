#include "ui/UiScenes.h"
#include "ui/UiTheme.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace {
const cv::Rect kStats(16, 16, 348, 142);
const cv::Point kCompass(640, 72);
const cv::Rect kNestPanel(900, 264, 368, 444);
const cv::Point kBannerCenter(640, 286);

TextStyle style(int size, cv::Scalar color = theme::kWhite, TextAlign align = TextAlign::Left) {
    TextStyle s;
    s.size = size;
    s.color = color;
    s.align = align;
    return s;
}

void drawStats(UiKit& kit, const GameplayView& view) {
    kit.canvas.fill(kStats, theme::kInk, 0.68f, 14);
    const bool full = view.carried >= view.capacity;
    kit.canvas.image(kit.assets.icon(UiIcon::Cashew, 44), {kStats.x + 12, kStats.y + 8});
    kit.text.draw(kit.canvas, std::to_string(view.carried) + "/" + std::to_string(view.capacity),
                  {kStats.x + 64, kStats.y + 43}, style(32, full ? theme::kGold : theme::kWhite));
    kit.canvas.image(kit.assets.icon(UiIcon::Coin, 40), {kStats.x + 190, kStats.y + 10});
    kit.text.drawFit(kit.canvas, std::to_string(view.coins), {kStats.x + 238, kStats.y + 43}, 100, style(32, theme::kGold));

    const int right = kStats.x + kStats.width - 14;
    kit.text.draw(kit.canvas, "Pontuação", {kStats.x + 14, kStats.y + 88}, style(20, theme::kMuted));
    kit.text.draw(kit.canvas, std::to_string(view.totalDelivered), {right, kStats.y + 90}, style(30, theme::kWhite, TextAlign::Right));
    kit.text.draw(kit.canvas, "Record", {kStats.x + 14, kStats.y + 126}, style(20, theme::kMuted));
    const std::string record = std::to_string(view.record);
    kit.text.draw(kit.canvas, record, {right, kStats.y + 128}, style(30, theme::kGold, TextAlign::Right));
    const int holderRoom = kStats.width - 120 - kit.text.width(record, 30) - 20;
    if (!view.recordHolder.empty() && holderRoom > 30)
        kit.text.drawFit(kit.canvas, view.recordHolder, {kStats.x + 96, kStats.y + 126}, holderRoom, style(18, theme::kMuted));
}

void drawRecordSeal(UiKit& kit) {
    const cv::Rect chip(kStats.x, kStats.y + kStats.height + 8, 196, 60);
    kit.canvas.fill(chip, theme::kAccent, 0.94f, 14);
    kit.canvas.outline(chip, theme::kGold, 2, 14);
    kit.canvas.image(kit.assets.icon(UiIcon::RecordBadge, 52), {chip.x + 6, chip.y + 4});
    kit.text.draw(kit.canvas, "RECORD", {chip.x + 62, chip.y + 40}, style(27, theme::kWhite));
}

std::string distanceText(float meters) {
    return std::to_string(static_cast<int>(std::lround(std::max(meters, 0.0f)))) + " m";
}

void drawCompass(UiKit& kit, const GameplayView& view) {
    const cv::Rect disk(kCompass.x - 56, kCompass.y - 56, 112, 112);
    kit.canvas.fill(disk, theme::kInk, 0.68f, 56);
    kit.canvas.outline(disk, theme::kGold, 2, 56);
    if (view.perchedOnNest) kit.canvas.imageCentered(kit.assets.icon(UiIcon::Nest, 76), kCompass);
    else kit.canvas.imageCentered(rotatedClockwise(kit.assets.icon(UiIcon::CompassArrow, 84), view.nestBearing), kCompass);

    const std::string label = view.perchedOnNest ? "No ninho" : distanceText(view.nestDistance);
    const int width = kit.text.width(label, 24) + 62;
    const cv::Rect chip(kCompass.x - width / 2, kCompass.y + 62, width, 40);
    kit.canvas.fill(chip, theme::kInk, 0.72f, 12);
    kit.canvas.image(kit.assets.icon(UiIcon::Nest, 32), {chip.x + 8, chip.y + 4});
    kit.text.draw(kit.canvas, label, {chip.x + 48, chip.y + 29}, style(24));
}

// O aviso e montado uma vez em tamanho real e so depois ampliado pela animacao.
cv::Mat bannerArt(UiKit& kit, int total) {
    Canvas art(640, 150);
    const cv::Rect area(8, 8, 624, 134);
    art.fill(area, theme::kAccent, 0.96f, 22);
    art.outline(area, theme::kGold, 4, 22);
    art.image(kit.assets.icon(UiIcon::RecordBadge, 112), {22, 19});
    kit.text.draw(art, "Novo record!", {150, 80}, style(56, theme::kWhite));
    kit.text.draw(art, std::to_string(total) + " cajus entregues", {152, 122}, style(26, theme::kGold));
    return art.pixels();
}

void drawBanner(UiKit& kit, const HudScene& scene) {
    if (scene.bannerOpacity <= 0.0f) return;
    const cv::Mat art = bannerArt(kit, scene.view->totalDelivered);
    cv::Mat scaled;
    cv::resize(art, scaled, cv::Size(), scene.bannerScale, scene.bannerScale, cv::INTER_LINEAR);
    kit.canvas.imageCentered(scaled, kBannerCenter, scene.bannerOpacity);
}

void drawLevelPips(UiKit& kit, cv::Point origin, int level, int maxLevel) {
    for (int i = 0; i < maxLevel; i++) {
        const cv::Rect pip(origin.x + i * 19, origin.y, 15, 9);
        kit.canvas.fill(pip, i < level ? theme::kGold : theme::kInk, i < level ? 1.0f : 0.8f, 3);
        kit.canvas.outline(pip, theme::kMuted, 1, 3);
    }
}

void drawPrice(UiKit& kit, const UpgradeOptionView& option, cv::Point rightCenter) {
    if (option.maxLevel > 0 && option.level >= option.maxLevel) {
        kit.text.draw(kit.canvas, "MÁX", rightCenter + cv::Point(0, 8), style(20, theme::kGreen, TextAlign::Right));
    } else if (option.price > 0) {
        const std::string price = std::to_string(option.price);
        kit.text.draw(kit.canvas, price, rightCenter + cv::Point(0, 9),
                      style(24, option.affordable ? theme::kGold : theme::kRed, TextAlign::Right));
        const int x = rightCenter.x - kit.text.width(price, 24) - 32;
        kit.canvas.image(kit.assets.icon(UiIcon::Coin, 26), {x, rightCenter.y - 13});
    }
}

void drawNestOption(UiKit& kit, const UpgradeOptionView& option, const cv::Rect& area, bool selected) {
    kit.canvas.fill(area, selected ? theme::kAccent : theme::kInk, selected ? 0.95f : 0.6f, 10);
    if (selected) kit.canvas.outline(area, theme::kGold, 2, 10);
    const int iconSize = std::min(area.height - 8, 46);
    const auto icon = iconForOption(option.name);
    if (icon) kit.canvas.image(kit.assets.icon(*icon, iconSize), {area.x + 6, area.y + (area.height - iconSize) / 2});

    const int textX = area.x + 60, textRoom = area.width - 150;
    const bool twoLines = option.maxLevel > 0 && area.height >= 50;
    const int baseline = twoLines ? area.y + 25 : area.y + area.height / 2 + 8;
    kit.text.drawFit(kit.canvas, option.name, {textX, baseline}, textRoom, style(20));
    if (twoLines) drawLevelPips(kit, {textX, area.y + area.height - 19}, option.level, option.maxLevel);
    drawPrice(kit, option, {area.x + area.width - 12, area.y + area.height / 2});
}

void drawArmCard(UiKit& kit, const cv::Rect& area, const std::string& arm, const std::string& action, bool active, float progress) {
    kit.canvas.fill(area, theme::kInk, 0.72f, 10);
    if (active) kit.canvas.outline(area, theme::kGold, 2, 10);
    kit.text.draw(kit.canvas, arm, {area.x + 12, area.y + 24}, style(16, theme::kMuted));
    kit.text.drawFit(kit.canvas, action, {area.x + 112, area.y + 25}, area.width - 124, style(20));
    kit.canvas.progressBar({area.x + 12, area.y + 36, area.width - 24, 10}, active ? progress : 0.0f, theme::kGold);
}

std::string nestTitle(NestMenuPage page) {
    switch (page) {
        case NestMenuPage::Eat: return "Comer: melhora o pássaro";
        case NestMenuPage::Invest: return "Investir: melhora o ninho";
        default: return "Ninho";
    }
}

void drawNestMenu(UiKit& kit, const GameplayView& view) {
    const NestMenuView& menu = view.menu;
    kit.canvas.fill(kNestPanel, theme::kPanel, 0.9f, 16);
    kit.canvas.outline(kNestPanel, theme::kGold, 2, 16);
    kit.text.drawFit(kit.canvas, nestTitle(menu.page), {kNestPanel.x + 16, kNestPanel.y + 36}, kNestPanel.width - 32, style(25, theme::kGold));

    constexpr int kArmCards = 2 * 54 + 8, kHeader = 50, kGap = 6;
    const int count = std::max(static_cast<int>(menu.options.size()), 1);
    const int room = kNestPanel.height - kHeader - kArmCards - 24;
    const int rowHeight = std::min((room - (count - 1) * kGap) / count, 64);
    for (int i = 0; i < static_cast<int>(menu.options.size()); i++) {
        const cv::Rect area(kNestPanel.x + 12, kNestPanel.y + kHeader + i * (rowHeight + kGap), kNestPanel.width - 24, rowHeight);
        drawNestOption(kit, menu.options[i], area, i == menu.selected);
    }

    const int cardWidth = kNestPanel.width - 24;
    const int cardsY = kNestPanel.y + kNestPanel.height - kArmCards - 12;
    drawArmCard(kit, {kNestPanel.x + 12, cardsY, cardWidth, 54}, "Braço esq.", menu.leftLabel,
                menu.raisedArm == ArmSide::Left, menu.holdProgress);
    drawArmCard(kit, {kNestPanel.x + 12, cardsY + 62, cardWidth, 54}, "Braço dir.", menu.rightLabel,
                menu.raisedArm == ArmSide::Right, menu.holdProgress);
}

void drawBodyWarning(UiKit& kit) {
    const std::string text = "Corpo fora do quadro: o pássaro plana nivelado";
    const int width = kit.text.width(text, 22) + 40;
    const cv::Rect chip((kUiWidth - width) / 2, kUiHeight - 62, width, 44);
    kit.canvas.fill(chip, theme::kRed, 0.9f, 12);
    kit.text.draw(kit.canvas, text, {kUiWidth / 2, chip.y + 30}, style(22, theme::kWhite, TextAlign::Center));
}
}

void drawHudScene(UiKit& kit, const HudScene& scene) {
    const GameplayView& view = *scene.view;
    drawStats(kit, view);
    if (view.leadingRecord) drawRecordSeal(kit);
    drawCompass(kit, view);
    kit.canvas.outline(kPreviewArea, theme::kGold, 2, 4);
    if (view.menu.page != NestMenuPage::Closed) drawNestMenu(kit, view);
    if (scene.bodyLost) drawBodyWarning(kit);
    drawBanner(kit, scene);
}

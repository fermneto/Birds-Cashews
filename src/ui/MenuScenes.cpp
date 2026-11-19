#include "ui/UiWidgets.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

namespace {
constexpr int kMenuX = 64, kMenuWidth = 540, kRowHeight = 50, kRowGap = 4, kHeroHeight = 66, kMaxVisibleRows = 7;
constexpr int kPromptWidth = 232, kPromptHeight = 52;

TextStyle style(int size, cv::Scalar color = theme::kWhite, TextAlign align = TextAlign::Left) { return uiStyle(size, color, align); }

void drawFieldValue(UiKit& kit, const MenuRow& row, const cv::Rect& area, bool selected, bool caretOn) {
    const cv::Rect box(area.x + 190, area.y + 8, area.width - 204, area.height - 16);
    kit.canvas.fill({box.x, box.y + box.height - 2, box.width, 2}, selected ? theme::kGold : theme::kMuted, selected ? 1.0f : 0.5f);
    const int size = 20, room = box.width - 12;
    std::string shown = row.value;
    while (!shown.empty() && kit.text.width(shown, size) > room) {
        size_t length = 1;
        while (length < shown.size() && (static_cast<unsigned char>(shown[length]) & 0xC0) == 0x80) length++;
        shown.erase(0, length);
    }
    kit.text.draw(kit.canvas, shown, {box.x + 4, box.y + box.height - 9}, style(size));
    if (selected && caretOn) kit.canvas.fill({box.x + 6 + kit.text.width(shown, size), box.y + 5, 2, box.height - 12}, theme::kGold, 1.0f);
}

// A linha escolhida ganha luz e cor. As demais ficam como texto simples sobre o cenario.
void drawRow(UiKit& kit, const MenuRow& row, const cv::Rect& area, bool selected, bool caretOn, int textSize) {
    if (selected) {
        kit.canvas.gradient(area, theme::kAccent, 0.95f, 0.0f, true);
        kit.canvas.fill({area.x, area.y, 5, area.height}, theme::kGold, 1.0f);
    }
    const int baseline = area.y + area.height / 2 + textSize / 3;
    const cv::Scalar color = selected ? theme::kWhite : theme::kMuted;
    if (row.field) {
        kit.text.drawFit(kit.canvas, row.label, {area.x + 22, baseline}, 160, style(textSize, color));
        drawFieldValue(kit, row, area, selected, caretOn);
        return;
    }
    const int valueWidth = row.value.empty() ? 0 : std::min(kit.text.width(row.value, 16) + 16, area.width / 2);
    kit.text.drawFit(kit.canvas, row.label, {area.x + 22, baseline}, area.width - 44 - valueWidth, style(textSize, color));
    if (valueWidth > 0)
        kit.text.drawFit(kit.canvas, row.value, {area.x + area.width - valueWidth - 8, baseline}, valueWidth - 4, style(16, theme::kMuted));
}

void drawScrollMark(UiKit& kit, cv::Point center, bool up) {
    const int tip = up ? -5 : 5;
    kit.canvas.line(center + cv::Point(-7, -tip), center + cv::Point(0, tip), theme::kGold, 2);
    kit.canvas.line(center + cv::Point(0, tip), center + cv::Point(7, -tip), theme::kGold, 2);
}

void drawRows(UiKit& kit, const std::vector<MenuRow>& rows, int selected, const cv::Rect& area, int maxRows, bool caretOn, bool hero = false) {
    const int total = static_cast<int>(rows.size());
    const int visible = std::min(total, maxRows);
    const int first = std::clamp(selected - visible / 2, 0, std::max(total - visible, 0));
    int y = area.y;
    for (int i = 0; i < visible; i++) {
        const bool big = hero && first + i == 0;
        const int height = big ? kHeroHeight : kRowHeight;
        drawRow(kit, rows[first + i], {area.x, y, area.width, height}, first + i == selected, caretOn, big ? 32 : 22);
        y += height + kRowGap + (big ? 8 : 0);
    }
    const int markX = area.x + area.width - 14;
    if (first > 0) drawScrollMark(kit, {markX, area.y - 12}, true);
    if (first + visible < total) drawScrollMark(kit, {markX, y + 6}, false);
}

// Comandos no rodape, como os botoes indicados em jogos de console.
void drawGestureLegend(UiKit& kit, cv::Point origin, int promptWidth, const GestureHint& hint) {
    const float opacity = hint.enabled ? 1.0f : 0.55f;
    drawArmPrompt(kit, {origin.x, origin.y, promptWidth, kPromptHeight}, ArmSide::Left, "Próxima opção",
                  hint.enabled && hint.raisedArm == ArmSide::Left, hint.progress, opacity);
    drawArmPrompt(kit, {origin.x + promptWidth + 10, origin.y, promptWidth, kPromptHeight}, ArmSide::Right, "Confirmar",
                  hint.enabled && hint.raisedArm == ArmSide::Right, hint.progress, opacity);
}

void drawKeyboardHint(UiKit& kit, cv::Point baseline, int width, const GestureHint& hint) {
    const std::string keys = hint.enabled ? "Teclado: setas e Enter, ou Q e E" : "Conclua os passos para liberar os gestos";
    kit.text.drawFit(kit.canvas, keys, baseline, width, style(14, hint.enabled ? theme::kMuted : theme::kGold));
}

// O nome do jogo usa o amarelo do caju apenas no "&".
void drawWordmark(UiKit& kit, cv::Point baseline) {
    constexpr int kSize = 58;
    kit.text.draw(kit.canvas, "Birds", baseline, style(kSize));
    int x = baseline.x + kit.text.width("Birds", kSize) + 4;
    kit.text.draw(kit.canvas, "&", {x, baseline.y}, style(kSize, theme::kGold));
    x += kit.text.width("&", kSize) + 4;
    kit.text.draw(kit.canvas, "Cashews", {x, baseline.y}, style(kSize));
}

void drawCheck(UiKit& kit, cv::Point center, bool done) {
    kit.canvas.fill({center.x - 13, center.y - 13, 26, 26}, done ? theme::kGreen : theme::kInk, 0.95f, 13);
    kit.canvas.outline({center.x - 13, center.y - 13, 26, 26}, done ? theme::kGreen : theme::kMuted, 2, 13);
    if (!done) return;
    kit.canvas.line(center + cv::Point(-7, 0), center + cv::Point(-2, 6), theme::kInk, 3);
    kit.canvas.line(center + cv::Point(-2, 6), center + cv::Point(8, -6), theme::kInk, 3);
}

cv::Rect regionArea(CalRegion region, const cv::Rect& video) {
    const int bandX = static_cast<int>(video.width * kCalRegionBand), bandY = static_cast<int>(video.height * kCalRegionBand);
    switch (region) {
        case CalRegion::Left: return {video.x, video.y + bandY, bandX, video.height - 2 * bandY};
        case CalRegion::Right: return {video.x + video.width - bandX, video.y + bandY, bandX, video.height - 2 * bandY};
        case CalRegion::Top: return {video.x, video.y, video.width, bandY};
        case CalRegion::Bottom: return {video.x, video.y + video.height - bandY, video.width, bandY};
    }
    return video;
}

const char* regionName(CalRegion region) {
    switch (region) {
        case CalRegion::Left: return "Esquerda";
        case CalRegion::Right: return "Direita";
        case CalRegion::Top: return "Alto";
        case CalRegion::Bottom: return "Baixo";
    }
    return "";
}

void drawRegions(UiKit& kit, const cv::Rect& video, const CalibrationTracker& tracker) {
    for (int i = 0; i < kCalRegionCount; i++) {
        const auto region = static_cast<CalRegion>(i);
        const cv::Rect area = regionArea(region, video);
        const bool done = tracker.reached(region);
        kit.canvas.fill(area, done ? theme::kGreen : theme::kGold, done ? 0.34f : 0.16f);
        kit.canvas.outline(area, done ? theme::kGreen : theme::kGold, 2, 0);
        const cv::Point center(area.x + area.width / 2, area.y + area.height / 2);
        kit.text.draw(kit.canvas, regionName(region), center + cv::Point(0, 8), style(22, theme::kWhite, TextAlign::Center));
    }
}

void drawSkeleton(UiKit& kit, const Pose& pose, cv::Size frame, const cv::Rect& video) {
    static const int bones[][2] = {{LShoulder, RShoulder}, {LShoulder, LElbow}, {LElbow, LWrist}, {RShoulder, RElbow},
                                   {RElbow, RWrist}, {LShoulder, LHip}, {RShoulder, RHip}, {LHip, RHip},
                                   {LHip, LKnee}, {LKnee, LAnkle}, {RHip, RKnee}, {RKnee, RAnkle}};
    const auto place = [&](int joint) {
        const cv::Point2f p = pose.at(joint);
        return cv::Point(video.x + static_cast<int>(p.x * video.width / frame.width),
                         video.y + static_cast<int>(p.y * video.height / frame.height));
    };
    for (const auto& bone : bones)
        if (pose.has(bone[0]) && pose.has(bone[1])) kit.canvas.line(place(bone[0]), place(bone[1]), theme::kGreen, 4);
    for (int joint = 0; joint < JointCount; joint++)
        if (pose.has(joint)) kit.canvas.dot(place(joint), joint == LWrist || joint == RWrist ? 10 : 6, theme::kGold);
}

void drawVideoStatus(UiKit& kit, const cv::Rect& video, const CalibrationTracker& tracker) {
    const bool visible = tracker.bodyVisibleNow();
    const std::string text = visible ? "Corpo inteiro visível" : "Afaste-se: mostre o corpo inteiro";
    const int width = kit.text.width(text, 22) + 40;
    const int bottomBand = static_cast<int>(video.height * kCalRegionBand);
    const cv::Rect chip(video.x + (video.width - width) / 2, video.y + video.height - bottomBand - 58, width, 48);
    kit.canvas.fill(chip, visible ? theme::kInk : theme::kRed, 0.82f, 12);
    kit.text.draw(kit.canvas, text, {chip.x + width / 2, chip.y + 32}, style(22, theme::kWhite, TextAlign::Center));
}

void drawNoCamera(UiKit& kit, const cv::Rect& area) {
    kit.canvas.fill(area, theme::kInk, 0.9f, 14);
    kit.text.draw(kit.canvas, "Webcam não disponível", {area.x + 40, area.y + 90}, style(36, theme::kGold));
    const char* const lines[] = {
        "Sem a webcam não dá para conferir o corpo no vídeo,",
        "mas você pode jogar normalmente pelo teclado:",
        "",
        "Espaço: bater as asas",
        "S: mergulhar",
        "A e D: fazer curvas",
        "Q e E: escolher nos menus",
        "Esc: pausar o jogo",
    };
    int y = area.y + 160;
    for (const char* line : lines) {
        kit.text.draw(kit.canvas, line, {area.x + 40, y}, style(24));
        y += 42;
    }
}

void drawChecklist(UiKit& kit, cv::Point origin, const CalibrationTracker& tracker) {
    struct Item { const char* label; bool done; };
    const Item items[] = {{"Corpo inteiro no vídeo", tracker.bodyConfirmed()},
                          {"Mão na região esquerda", tracker.reached(CalRegion::Left)},
                          {"Mão na região direita", tracker.reached(CalRegion::Right)},
                          {"Mão na região alta", tracker.reached(CalRegion::Top)},
                          {"Mão na região baixa", tracker.reached(CalRegion::Bottom)}};
    int y = origin.y;
    for (const Item& item : items) {
        drawCheck(kit, {origin.x + 14, y}, item.done);
        kit.text.drawFit(kit.canvas, item.label, {origin.x + 40, y + 8}, 340, style(21, item.done ? theme::kGreen : theme::kWhite));
        y += 38;
    }
}
}

void drawMenuScene(UiKit& kit, const MenuScene& scene) {
    // O mundo continua visivel: so a coluna do menu escurece para manter a leitura.
    kit.canvas.fill({0, 0, kit.canvas.width(), kit.canvas.height()}, theme::kInk, 0.22f);
    kit.canvas.gradient({0, 0, 860, kUiHeight}, theme::kInk, 0.94f, 0.0f, true);

    if (scene.hero) drawWordmark(kit, {kMenuX, 112});
    else kit.text.drawFit(kit.canvas, scene.title, {kMenuX, 104}, kMenuWidth, style(46));
    kit.text.drawFit(kit.canvas, scene.subtitle, {kMenuX, 146}, kMenuWidth, style(18, theme::kMuted));

    drawRows(kit, scene.rows, scene.selected, {kMenuX - 22, 176, kMenuWidth + 22, 400}, kMaxVisibleRows, scene.caretOn, scene.hero);

    constexpr int kPromptsY = 636;
    if (!scene.error.empty()) kit.text.drawFit(kit.canvas, scene.error, {kMenuX, 592}, kMenuWidth, style(17, theme::kRed));
    else kit.text.drawFit(kit.canvas, scene.notice, {kMenuX, 592}, kMenuWidth, style(16, theme::kGold));
    kit.text.drawFit(kit.canvas, scene.footer, {kMenuX, 620}, kMenuWidth, style(15, theme::kMuted));

    drawGestureLegend(kit, {kMenuX, kPromptsY}, kPromptWidth, scene.gesture);
    drawKeyboardHint(kit, {kMenuX + 2 * kPromptWidth + 28, kPromptsY + 32}, 300, scene.gesture);
    if (scene.showPreview) drawPreviewFrame(kit);
}

void drawCalibrationScene(UiKit& kit, const CalibrationScene& scene) {
    kit.canvas.fill({0, 0, kit.canvas.width(), kit.canvas.height()}, theme::kPanel, 0.96f);
    kit.text.draw(kit.canvas, "Calibração", {28, 54}, style(36));
    kit.text.drawFit(kit.canvas, "Vídeo em modo espelho", {262, 52}, 400, style(17, theme::kMuted));

    const cv::Rect video(28, 72, 816, 612);
    if (scene.hasCamera && !scene.frame.empty()) {
        kit.canvas.video(scene.frame, video);
        drawRegions(kit, video, *scene.tracker);
        drawSkeleton(kit, scene.pose, scene.frame.size(), video);
        drawVideoStatus(kit, video, *scene.tracker);
    } else if (scene.hasCamera) {
        kit.canvas.fill(video, theme::kInk, 0.9f, 14);
        kit.text.draw(kit.canvas, "Aguardando a imagem da webcam...", {video.x + video.width / 2, video.y + video.height / 2},
                      style(28, theme::kWhite, TextAlign::Center));
    } else {
        drawNoCamera(kit, video);
    }

    const int column = 868, width = 388;
    int y = 92;
    if (scene.hasCamera) {
        drawChecklist(kit, {column, y}, *scene.tracker);
        y += 5 * 38 - 8;
    } else {
        kit.text.draw(kit.canvas, "Modo teclado", {column, y + 8}, style(28, theme::kGold));
        y += 40;
    }
    drawRows(kit, scene.rows, scene.selected, {column, y, width, 2 * kRowHeight + kRowGap}, 2, false);
    y += 2 * (kRowHeight + kRowGap) + 12;
    drawGestureLegend(kit, {column, y}, (width - 10) / 2, scene.gesture);
    drawKeyboardHint(kit, {column + 2, y + kPromptHeight + 24}, width - 4, scene.gesture);
}

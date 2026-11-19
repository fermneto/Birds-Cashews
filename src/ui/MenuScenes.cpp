#include "ui/UiScenes.h"
#include "ui/UiTheme.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

namespace {
const cv::Rect kPanel(24, 24, 900, 672);
constexpr int kRowHeight = 56, kRowGap = 8, kMaxVisibleRows = 7;

TextStyle style(int size, cv::Scalar color = theme::kWhite, TextAlign align = TextAlign::Left) {
    TextStyle s;
    s.size = size;
    s.color = color;
    s.align = align;
    return s;
}

void drawFieldBox(UiKit& kit, const MenuRow& row, const cv::Rect& area, bool selected, bool caretOn) {
    const cv::Rect box(area.x + 230, area.y + 8, area.width - 244, area.height - 16);
    kit.canvas.fill(box, theme::kInk, 0.85f, 8);
    kit.canvas.outline(box, selected ? theme::kWhite : theme::kMuted, selected ? 2 : 1, 8);
    const int size = 22, room = box.width - 28;
    std::string shown = row.value;
    while (!shown.empty() && kit.text.width(shown, size) > room) {
        size_t length = 1;
        while (length < shown.size() && (static_cast<unsigned char>(shown[length]) & 0xC0) == 0x80) length++;
        shown.erase(0, length);
    }
    kit.text.draw(kit.canvas, shown, {box.x + 12, box.y + box.height - 11}, style(size));
    if (selected && caretOn) {
        const int x = box.x + 14 + kit.text.width(shown, size);
        kit.canvas.fill({x, box.y + 7, 3, box.height - 14}, theme::kWhite, 1.0f);
    }
}

void drawRow(UiKit& kit, const MenuRow& row, const cv::Rect& area, bool selected, bool caretOn) {
    kit.canvas.fill(area, selected ? theme::kAccent : theme::kInk, selected ? 0.95f : 0.62f, 12);
    if (selected) kit.canvas.outline(area, theme::kGold, 2, 12);
    const int baseline = area.y + area.height / 2 + 9;
    if (row.field) {
        kit.text.drawFit(kit.canvas, row.label, {area.x + 18, baseline}, 204, style(24));
        drawFieldBox(kit, row, area, selected, caretOn);
        return;
    }
    const int valueWidth = row.value.empty() ? 0 : std::min(kit.text.width(row.value, 20) + 16, area.width / 2);
    kit.text.drawFit(kit.canvas, row.label, {area.x + 18, baseline}, area.width - 36 - valueWidth, style(24));
    kit.text.drawFit(kit.canvas, row.value, {area.x + area.width - valueWidth - 2, baseline}, valueWidth - 4,
                     style(20, theme::kMuted));
}

void drawRows(UiKit& kit, const std::vector<MenuRow>& rows, int selected, const cv::Rect& area, int maxRows, bool caretOn) {
    const int total = static_cast<int>(rows.size());
    const int visible = std::min(total, maxRows);
    const int first = std::clamp(selected - visible / 2, 0, std::max(total - visible, 0));
    for (int i = 0; i < visible; i++) {
        const cv::Rect rowArea(area.x, area.y + i * (kRowHeight + kRowGap), area.width, kRowHeight);
        drawRow(kit, rows[first + i], rowArea, first + i == selected, caretOn);
    }
    const int right = area.x + area.width - 4;
    if (first > 0) kit.text.draw(kit.canvas, "▲", {right, area.y - 6}, style(16, theme::kGold, TextAlign::Right));
    if (first + visible < total) {
        const int bottom = area.y + visible * (kRowHeight + kRowGap) + 12;
        kit.text.draw(kit.canvas, "▼", {right, bottom}, style(16, theme::kGold, TextAlign::Right));
    }
}

void drawGestureCard(UiKit& kit, const cv::Rect& area, const std::string& arm, const std::string& action,
                     bool active, float progress) {
    kit.canvas.fill(area, theme::kInk, 0.72f, 10);
    if (active) kit.canvas.outline(area, theme::kGold, 2, 10);
    kit.text.draw(kit.canvas, arm, {area.x + 14, area.y + 25}, style(17, theme::kMuted));
    kit.text.drawFit(kit.canvas, action, {area.x + 14, area.y + 52}, area.width - 28, style(22));
    kit.canvas.progressBar({area.x + 14, area.y + 62, area.width - 28, 10}, active ? progress : 0.0f, theme::kGold);
}

// Legenda fixa com o que cada braco faz e o andamento do gesto atual.
void drawGestureLegend(UiKit& kit, const cv::Rect& area, const GestureHint& hint) {
    const int cardHeight = 84;
    drawGestureCard(kit, {area.x, area.y, area.width, cardHeight}, "Braço esquerdo erguido", "Próxima opção",
                    hint.enabled && hint.raisedArm == ArmSide::Left, hint.progress);
    drawGestureCard(kit, {area.x, area.y + cardHeight + 8, area.width, cardHeight}, "Braço direito erguido", "Confirmar",
                    hint.enabled && hint.raisedArm == ArmSide::Right, hint.progress);
    const int y = area.y + 2 * cardHeight + 34;
    const std::string keys = hint.enabled ? "Teclado: setas e Enter, ou Q e E" : "Conclua os passos para liberar os gestos";
    kit.text.drawFit(kit.canvas, keys, {area.x + 4, y}, area.width - 8, style(16, hint.enabled ? theme::kMuted : theme::kGold));
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
    const std::string text = visible ? "Corpo inteiro visível" : "Afaste-se até o corpo inteiro aparecer no vídeo";
    const int width = kit.text.width(text, 22) + 40;
    const cv::Rect chip(video.x + (video.width - width) / 2, video.y + video.height / 2 - 24, width, 48);
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
    kit.canvas.fill({0, 0, kit.canvas.width(), kit.canvas.height()}, cv::Scalar(0, 0, 0), 0.42f);
    kit.canvas.fill(kPanel, theme::kPanel, 0.86f, 18);
    kit.canvas.image(kit.assets.icon(UiIcon::Cashew, 60), {kPanel.x + 28, kPanel.y + 20});
    kit.text.drawFit(kit.canvas, scene.title, {kPanel.x + 100, kPanel.y + 66}, kPanel.width - 130, style(44, theme::kGold));
    kit.text.drawFit(kit.canvas, scene.subtitle, {kPanel.x + 32, kPanel.y + 112}, kPanel.width - 64, style(21, theme::kMuted));

    const cv::Rect rowsArea(kPanel.x + 28, kPanel.y + 134, kPanel.width - 56, 448);
    drawRows(kit, scene.rows, scene.selected, rowsArea, kMaxVisibleRows, scene.caretOn);

    const int messageY = kPanel.y + kPanel.height - 44;
    if (!scene.error.empty()) kit.text.drawFit(kit.canvas, scene.error, {kPanel.x + 32, messageY}, kPanel.width - 64, style(21, theme::kRed));
    else kit.text.drawFit(kit.canvas, scene.notice, {kPanel.x + 32, messageY}, kPanel.width - 64, style(20, theme::kGold));
    kit.text.drawFit(kit.canvas, scene.footer, {kPanel.x + 32, kPanel.y + kPanel.height - 14}, kPanel.width - 64, style(18, theme::kMuted));

    kit.canvas.outline(kPreviewArea, theme::kGold, 2, 4);
    drawGestureLegend(kit, {kPreviewArea.x, kPreviewArea.y + kPreviewArea.height + 14, kPreviewArea.width, 240}, scene.gesture);
}

void drawCalibrationScene(UiKit& kit, const CalibrationScene& scene) {
    kit.canvas.fill({0, 0, kit.canvas.width(), kit.canvas.height()}, theme::kPanel, 0.94f);
    kit.text.draw(kit.canvas, "Calibração", {28, 52}, style(38, theme::kGold));
    kit.text.drawFit(kit.canvas, "Vídeo em modo espelho", {270, 50}, 400, style(20, theme::kMuted));

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
    y += 2 * (kRowHeight + kRowGap) + 8;
    drawGestureLegend(kit, {column, y, width, 220}, scene.gesture);
}

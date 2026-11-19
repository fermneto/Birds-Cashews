#include "ui/UiWidgets.h"
#include <algorithm>

TextStyle uiStyle(int size, cv::Scalar color, TextAlign align) {
    TextStyle s;
    s.size = size;
    s.color = color;
    s.align = align;
    return s;
}

void drawArmPrompt(UiKit& kit, const cv::Rect& area, ArmSide arm, const std::string& action, bool active, float progress,
                   float opacity) {
    const int radius = area.height / 2;
    kit.canvas.fill(area, theme::kInk, 0.62f * opacity, radius);
    const int badge = area.height - 14;
    const cv::Rect circle(area.x + 7, area.y + 7, badge, badge);
    kit.canvas.fill(circle, active ? theme::kGold : theme::kWhite, active ? 1.0f : 0.16f * opacity, badge / 2);
    TextStyle letter = uiStyle(badge / 2 + 2, active ? theme::kInk : theme::kWhite, TextAlign::Center);
    letter.outline = false;
    kit.text.draw(kit.canvas, arm == ArmSide::Left ? "E" : "D", {circle.x + badge / 2, circle.y + badge / 2 + badge / 5}, letter, opacity);

    const int textX = circle.x + badge + 10, room = area.x + area.width - textX - radius / 2;
    TextStyle small = uiStyle(12, theme::kMuted);
    small.outline = false;
    kit.text.draw(kit.canvas, arm == ArmSide::Left ? "Braço esquerdo" : "Braço direito", {textX, area.y + area.height / 2 - 5}, small, opacity);
    TextStyle main = uiStyle(17);
    main.outline = false;
    kit.text.drawFit(kit.canvas, action, {textX, area.y + area.height / 2 + 15}, room, main);
    if (active && progress > 0.0f) {
        const int width = std::max(6, static_cast<int>((area.width - 2 * radius) * std::min(progress, 1.0f)));
        kit.canvas.fill({area.x + radius, area.y + area.height - 4, width, 3}, theme::kGold, 1.0f, 1);
    }
}

// Abre um recorte na camada para o escurecimento dos menus nao apagar a webcam.
void drawPreviewFrame(UiKit& kit) {
    kit.canvas.pixels()(kPreviewArea).setTo(cv::Scalar::all(0));
    kit.canvas.outline(kPreviewArea, theme::kWhite, 1, 0);
}

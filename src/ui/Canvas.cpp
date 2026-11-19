#include "ui/Canvas.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Divide por 255 com arredondamento correto.
inline int div255(int value) {
    const int rounded = value + 128;
    return (rounded + (rounded >> 8)) >> 8;
}

inline uchar toByte(double value) {
    return static_cast<uchar>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

inline void blendPixel(uchar* out, const uchar* in, int opacity) {
    const int sourceAlpha = div255(in[3] * opacity);
    const int keep = 255 - sourceAlpha;
    for (int c = 0; c < 3; c++) out[c] = static_cast<uchar>(std::min(div255(in[c] * opacity) + div255(out[c] * keep), 255));
    out[3] = static_cast<uchar>(std::min(sourceAlpha + div255(out[3] * keep), 255));
}

// Fracao do pixel coberta por um retangulo de cantos arredondados.
float cornerCoverage(int x, int y, const cv::Rect& area, int radius) {
    if (radius <= 0) return 1.0f;
    const float px = x + 0.5f, py = y + 0.5f;
    const float left = static_cast<float>(area.x + radius), right = static_cast<float>(area.x + area.width - radius);
    const float top = static_cast<float>(area.y + radius), bottom = static_cast<float>(area.y + area.height - radius);
    const float dx = px < left ? left - px : px > right ? px - right : 0.0f;
    const float dy = py < top ? top - py : py > bottom ? py - bottom : 0.0f;
    if (dx == 0.0f || dy == 0.0f) return 1.0f;
    return std::clamp(radius + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
}
}

cv::Mat premultiplied(const cv::Mat& straight) {
    CV_Assert(straight.type() == CV_8UC4);
    cv::Mat out = straight.clone();
    for (int y = 0; y < out.rows; y++) {
        uchar* row = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols; x++, row += 4)
            for (int c = 0; c < 3; c++) row[c] = static_cast<uchar>(div255(row[c] * row[3]));
    }
    return out;
}

void compositeOver(cv::Mat& destination, const cv::Mat& source, cv::Point at, float opacity) {
    CV_Assert(destination.type() == CV_8UC4 && source.type() == CV_8UC4);
    const cv::Rect visible = cv::Rect(at, source.size()) & cv::Rect(cv::Point(0, 0), destination.size());
    const int amount = std::clamp(static_cast<int>(std::lround(opacity * 255.0f)), 0, 255);
    if (visible.empty() || amount == 0) return;
    for (int y = 0; y < visible.height; y++) {
        uchar* out = destination.ptr<uchar>(visible.y + y) + visible.x * 4;
        const uchar* in = source.ptr<uchar>(visible.y - at.y + y) + (visible.x - at.x) * 4;
        for (int x = 0; x < visible.width; x++, out += 4, in += 4) blendPixel(out, in, amount);
    }
}

cv::Mat rotatedClockwise(const cv::Mat& source, float radians) {
    const cv::Point2f center((source.cols - 1) * 0.5f, (source.rows - 1) * 0.5f);
    // Na imagem o eixo y aponta para baixo, e o OpenCV trata angulo positivo como anti-horario.
    const cv::Mat turn = cv::getRotationMatrix2D(center, -radians * 180.0 / CV_PI, 1.0);
    cv::Mat out;
    cv::warpAffine(source, out, turn, source.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    return out;
}

Canvas::Canvas(int width, int height) : pixels_(height, width, CV_8UC4, cv::Scalar::all(0)) {}

void Canvas::clear() {
    pixels_.setTo(cv::Scalar::all(0));
}

void Canvas::gradient(const cv::Rect& area, cv::Scalar color, float startAlpha, float endAlpha, bool horizontal) {
    const cv::Rect visible = area & cv::Rect(0, 0, width(), height());
    if (visible.empty()) return;
    // Calcula cada faixa uma unica vez, porque o HUD e redesenhado durante o voo.
    const int length = horizontal ? area.width : area.height;
    std::vector<cv::Vec4b> bands(static_cast<size_t>(length));
    for (int i = 0; i < length; i++) {
        const float t = static_cast<float>(i) / std::max(length - 1, 1);
        const float alpha = startAlpha + (endAlpha - startAlpha) * t * t * (3.0f - 2.0f * t);
        bands[static_cast<size_t>(i)] = {toByte(color[0] * alpha), toByte(color[1] * alpha), toByte(color[2] * alpha), toByte(alpha * 255.0)};
    }
    for (int y = visible.y; y < visible.y + visible.height; y++) {
        uchar* out = pixels_.ptr<uchar>(y) + visible.x * 4;
        for (int x = visible.x; x < visible.x + visible.width; x++, out += 4) {
            const cv::Vec4b& in = bands[static_cast<size_t>(horizontal ? x - area.x : y - area.y)];
            if (in[3] == 0) continue;
            const int keep = 255 - in[3];
            for (int c = 0; c < 3; c++) out[c] = static_cast<uchar>(std::min(in[c] + div255(out[c] * keep), 255));
            out[3] = static_cast<uchar>(std::min(in[3] + div255(out[3] * keep), 255));
        }
    }
}

void Canvas::fill(const cv::Rect& area, cv::Scalar color, float alpha, int radius) {
    const cv::Rect visible = area & cv::Rect(0, 0, width(), height());
    const int baseAlpha = std::clamp(static_cast<int>(std::lround(alpha * 255.0f)), 0, 255);
    if (visible.empty() || baseAlpha == 0) return;
    for (int y = visible.y; y < visible.y + visible.height; y++) {
        uchar* out = pixels_.ptr<uchar>(y) + visible.x * 4;
        for (int x = visible.x; x < visible.x + visible.width; x++, out += 4) {
            const float coverage = cornerCoverage(x, y, area, radius);
            if (coverage <= 0.0f) continue;
            const int a = static_cast<int>(baseAlpha * coverage + 0.5f);
            const uchar in[4] = {static_cast<uchar>(div255(static_cast<int>(color[0]) * a)),
                                 static_cast<uchar>(div255(static_cast<int>(color[1]) * a)),
                                 static_cast<uchar>(div255(static_cast<int>(color[2]) * a)), static_cast<uchar>(a)};
            blendPixel(out, in, 255);
        }
    }
}

void Canvas::outline(const cv::Rect& area, cv::Scalar color, int thickness, int radius) {
    const cv::Rect visible = area & cv::Rect(0, 0, width(), height());
    const cv::Rect inner(area.x + thickness, area.y + thickness, area.width - 2 * thickness, area.height - 2 * thickness);
    const int innerRadius = std::max(radius - thickness, 0);
    for (int y = visible.y; y < visible.y + visible.height; y++) {
        uchar* out = pixels_.ptr<uchar>(y) + visible.x * 4;
        for (int x = visible.x; x < visible.x + visible.width; x++, out += 4) {
            const float outer = cornerCoverage(x, y, area, radius);
            const float hole = inner.contains({x, y}) ? cornerCoverage(x, y, inner, innerRadius) : 0.0f;
            const int a = static_cast<int>(255.0f * std::max(outer - hole, 0.0f) + 0.5f);
            if (a == 0) continue;
            const uchar in[4] = {static_cast<uchar>(div255(static_cast<int>(color[0]) * a)),
                                 static_cast<uchar>(div255(static_cast<int>(color[1]) * a)),
                                 static_cast<uchar>(div255(static_cast<int>(color[2]) * a)), static_cast<uchar>(a)};
            blendPixel(out, in, 255);
        }
    }
}

void Canvas::progressBar(const cv::Rect& area, float progress, cv::Scalar color) {
    const int radius = area.height / 2;
    fill(area, cv::Scalar(20, 20, 20), 0.75f, radius);
    const int filled = static_cast<int>(area.width * std::clamp(progress, 0.0f, 1.0f));
    if (filled > 0) fill({area.x, area.y, std::max(filled, area.height), area.height}, color, 1.0f, radius);
}

void Canvas::image(const cv::Mat& source, cv::Point topLeft, float opacity) {
    compositeOver(pixels_, source, topLeft, opacity);
}

void Canvas::imageCentered(const cv::Mat& source, cv::Point center, float opacity) {
    image(source, center - cv::Point(source.cols / 2, source.rows / 2), opacity);
}

// A imagem da webcam ja chega espelhada e cobre a area inteira.
void Canvas::video(const cv::Mat& bgr, const cv::Rect& area) {
    if (bgr.empty()) return;
    cv::Mat scaled, bgra;
    cv::resize(bgr, scaled, area.size(), 0, 0, cv::INTER_LINEAR);
    cv::cvtColor(scaled, bgra, cv::COLOR_BGR2BGRA);
    image(bgra, area.tl());
}

void Canvas::line(cv::Point from, cv::Point to, cv::Scalar color, int thickness) {
    cv::line(pixels_, from, to, cv::Scalar(color[0], color[1], color[2], 255), thickness, cv::LINE_AA);
}

void Canvas::dot(cv::Point center, int radius, cv::Scalar color) {
    cv::circle(pixels_, center, radius, cv::Scalar(color[0], color[1], color[2], 255), -1, cv::LINE_AA);
}

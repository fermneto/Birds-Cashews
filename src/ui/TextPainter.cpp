#include "ui/TextPainter.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <sstream>

namespace {
constexpr size_t kMaxCachedSprites = 600;
constexpr int kMinFitSize = 13;
constexpr float kMinFitRatio = 0.72f;

std::string cacheKey(const std::string& text, const TextStyle& style) {
    std::ostringstream key;
    key << style.size << '|' << static_cast<int>(style.color[0]) << ',' << static_cast<int>(style.color[1]) << ','
        << static_cast<int>(style.color[2]) << '|' << style.outline << '|' << text;
    return key.str();
}

// Junta a letra e o contorno escuro em uma unica imagem pre-multiplicada.
cv::Mat composeGlyphs(const cv::Mat& coverage, const cv::Mat& halo, cv::Scalar color) {
    cv::Mat out(coverage.size(), CV_8UC4);
    for (int y = 0; y < out.rows; y++) {
        const uchar* letter = coverage.ptr<uchar>(y);
        const uchar* shadow = halo.ptr<uchar>(y);
        uchar* pixel = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols; x++, pixel += 4) {
            const float a = letter[x] / 255.0f;
            const float behind = (shadow[x] / 255.0f) * 0.82f * (1.0f - a);
            for (int c = 0; c < 3; c++) pixel[c] = cv::saturate_cast<uchar>(color[c] * a + 12.0f * behind);
            pixel[3] = cv::saturate_cast<uchar>((a + behind) * 255.0f);
        }
    }
    return out;
}

size_t previousCharacter(const std::string& text, size_t end) {
    size_t i = end;
    while (i > 0) {
        i--;
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) break;
    }
    return i;
}
}

TextPainter::TextPainter(const std::string& fontPath) : font_(cv::freetype::createFreeType2()) {
    font_->loadFontData(fontPath, 0);
}

int TextPainter::width(const std::string& text, int size) {
    if (text.empty()) return 0;
    int baseline = 0;
    return font_->getTextSize(text, size, -1, &baseline).width;
}

TextPainter::Sprite TextPainter::render(const std::string& text, const TextStyle& style) {
    const int border = style.outline ? std::max(2, style.size / 10) : 0;
    const int pad = style.size / 2 + border + 2;
    const int advance = width(text, style.size);
    const cv::Point origin(pad, pad + style.size + style.size / 3);
    cv::Mat canvas(origin.y + style.size + pad, advance + 2 * pad, CV_8UC3, cv::Scalar::all(0));
    font_->putText(canvas, text, origin, style.size, cv::Scalar::all(255), -1, cv::LINE_AA, true);

    cv::Mat coverage, halo;
    cv::extractChannel(canvas, coverage, 0);
    if (border > 0) {
        const cv::Mat shape = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * border + 1, 2 * border + 1));
        cv::dilate(coverage, halo, shape);
    } else {
        halo = cv::Mat::zeros(coverage.size(), CV_8UC1);
    }

    // Guarda so a area com tinta para o desenho custar pouco.
    const cv::Mat ink = halo | coverage;
    cv::Rect used = cv::boundingRect(ink);
    if (used.empty()) used = cv::Rect(0, 0, 1, 1);
    Sprite result;
    result.pixels = composeGlyphs(coverage(used), halo(used), style.color);
    result.origin = origin - used.tl();
    result.advance = advance;
    return result;
}

const TextPainter::Sprite& TextPainter::sprite(const std::string& text, const TextStyle& style) {
    const std::string key = cacheKey(text, style);
    const auto found = cache_.find(key);
    if (found != cache_.end()) return found->second;
    if (cache_.size() >= kMaxCachedSprites) cache_.clear();
    return cache_.emplace(key, render(text, style)).first->second;
}

void TextPainter::draw(Canvas& canvas, const std::string& text, cv::Point baseline, const TextStyle& style, float opacity) {
    if (text.empty()) return;
    const Sprite& glyphs = sprite(text, style);
    int x = baseline.x;
    if (style.align == TextAlign::Center) x -= glyphs.advance / 2;
    if (style.align == TextAlign::Right) x -= glyphs.advance;
    canvas.image(glyphs.pixels, cv::Point(x, baseline.y) - glyphs.origin, opacity);
}

std::string TextPainter::ellipsized(const std::string& text, int size, int maxWidth) {
    static const std::string kDots = "...";
    std::string shortened = text;
    while (!shortened.empty() && width(shortened + kDots, size) > maxWidth)
        shortened.erase(previousCharacter(shortened, shortened.size()));
    return shortened + kDots;
}

void TextPainter::drawFit(Canvas& canvas, const std::string& text, cv::Point baseline, int maxWidth, const TextStyle& style) {
    TextStyle fitted = style;
    const int smallest = std::max(kMinFitSize, static_cast<int>(style.size * kMinFitRatio));
    while (fitted.size > smallest && width(text, fitted.size) > maxWidth) fitted.size--;
    const bool fits = width(text, fitted.size) <= maxWidth;
    draw(canvas, fits ? text : ellipsized(text, fitted.size, maxWidth), baseline, fitted);
}

std::vector<std::string> TextPainter::wrap(const std::string& text, int size, int maxWidth) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(candidate, size) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

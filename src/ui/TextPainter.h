#pragma once
#include "ui/Canvas.h"
#include <opencv2/freetype.hpp>
#include <string>
#include <unordered_map>
#include <vector>

enum class TextAlign { Left, Center, Right };

struct TextStyle {
    int size = 24;
    cv::Scalar color = cv::Scalar(255, 255, 255);
    TextAlign align = TextAlign::Left;
    bool outline = true;
};

// Desenha texto UTF-8 com FreeType. Cada texto vira uma imagem guardada, porque o FreeType e lento.
class TextPainter {
public:
    explicit TextPainter(const std::string& fontPath);

    int width(const std::string& text, int size);

    // "baseline" e a linha de apoio das letras; a coordenada x segue o alinhamento.
    void draw(Canvas& canvas, const std::string& text, cv::Point baseline, const TextStyle& style, float opacity = 1.0f);

    // Reduz a fonte ate caber em maxWidth e, se ainda faltar espaco, corta com reticencias.
    void drawFit(Canvas& canvas, const std::string& text, cv::Point baseline, int maxWidth, const TextStyle& style);

    std::vector<std::string> wrap(const std::string& text, int size, int maxWidth);

    size_t cachedSprites() const { return cache_.size(); }

private:
    struct Sprite {
        cv::Mat pixels;
        cv::Point origin;
        int advance = 0;
    };

    const Sprite& sprite(const std::string& text, const TextStyle& style);
    Sprite render(const std::string& text, const TextStyle& style);
    std::string ellipsized(const std::string& text, int size, int maxWidth);

    cv::Ptr<cv::freetype::FreeType2> font_;
    std::unordered_map<std::string, Sprite> cache_;
};

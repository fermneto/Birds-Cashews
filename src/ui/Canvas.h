#pragma once
#include <opencv2/core.hpp>

// Converte BGRA comum em BGRA com a cor ja multiplicada pelo alfa.
cv::Mat premultiplied(const cv::Mat& straightBgra);

// Operacao "over" sobre pixels pre-multiplicados: resultado = fonte + destino * (1 - alfa da fonte).
void compositeOver(cv::Mat& destination, const cv::Mat& source, cv::Point topLeft, float opacity = 1.0f);

// Gira uma imagem pre-multiplicada em torno do centro. Angulo positivo gira no sentido horario.
cv::Mat rotatedClockwise(const cv::Mat& source, float radians);

// Camada transparente desenhada sobre o jogo, em resolucao logica fixa.
class Canvas {
public:
    Canvas(int width, int height);

    const cv::Mat& pixels() const { return pixels_; }
    cv::Mat& pixels() { return pixels_; }
    int width() const { return pixels_.cols; }
    int height() const { return pixels_.rows; }

    void clear();
    // Cores em BGR, com alfa de 0 a 1 separado.
    void fill(const cv::Rect& area, cv::Scalar color, float alpha, int radius = 0);
    void outline(const cv::Rect& area, cv::Scalar color, int thickness, int radius);
    // Degrade suave de alfa, da esquerda para a direita ou de cima para baixo.
    void gradient(const cv::Rect& area, cv::Scalar color, float startAlpha, float endAlpha, bool horizontal);
    void progressBar(const cv::Rect& area, float progress, cv::Scalar color);
    void image(const cv::Mat& premultipliedBgra, cv::Point topLeft, float opacity = 1.0f);
    void imageCentered(const cv::Mat& premultipliedBgra, cv::Point center, float opacity = 1.0f);
    void video(const cv::Mat& bgr, const cv::Rect& area);
    void line(cv::Point from, cv::Point to, cv::Scalar color, int thickness);
    void dot(cv::Point center, int radius, cv::Scalar color);

private:
    cv::Mat pixels_;
};

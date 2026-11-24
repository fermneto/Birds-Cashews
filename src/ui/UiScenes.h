#pragma once
#include "shared/GameplayView.h"
#include "ui/CalibrationTracker.h"
#include "ui/Canvas.h"
#include "ui/TextPainter.h"
#include "ui/UiAssets.h"
#include <filesystem>
#include <string>
#include <vector>

// Resolucao logica da camada de interface, ajustada ao tamanho da janela pelo Renderer.
constexpr int kUiWidth = 1280;
constexpr int kUiHeight = 720;

// Recursos compartilhados por todas as telas.
struct UiKit {
    explicit UiKit(const std::filesystem::path& assetsDir)
        : canvas(kUiWidth, kUiHeight), assets(assetsDir), text(assets.fontPath()) {}
    Canvas canvas;
    UiAssets assets;
    TextPainter text;
};

struct MenuRow {
    std::string label, value;
    bool field = false;
};

struct GestureHint {
    ArmSide raisedArm = ArmSide::None;
    float progress = 0;
    bool enabled = true;
};

struct MenuScene {
    std::string title, subtitle, notice, error, footer;
    std::vector<MenuRow> rows;
    int selected = 0;
    bool caretOn = false;
    bool hero = false;          // Tela inicial: nome do jogo e primeira opcao em destaque.
    bool showPreview = false;
    GestureHint gesture;
};

struct CalibrationScene {
    bool hasCamera = false;
    cv::Mat frame;
    Pose pose;
    const CalibrationTracker* tracker = nullptr;
    std::string doneText;       // Aviso mostrado entre concluir e sair sozinho da tela.
    std::vector<MenuRow> rows;
    int selected = 0;
    GestureHint gesture;
};

struct HudScene {
    const GameplayView* view = nullptr;
    float bannerOpacity = 0, bannerScale = 1;
    bool showPreview = false;
    float coinsOpacity = 0;     // As moedas so aparecem quando mudam.
};

// Retangulo da imagem da webcam dentro de area, centralizado e sem distorcer a proporcao.
cv::Rect fitVideo(cv::Size frame, const cv::Rect& area);
// Pixel da tela onde fica o ponto p da imagem (coordenadas continuas) desenhada em video.
// Imagem, regioes e esqueleto usam esta mesma conta para nao se desalinharem.
cv::Point videoPoint(cv::Point2f p, cv::Size frame, const cv::Rect& video);

void drawMenuScene(UiKit& kit, const MenuScene& scene);
void drawCalibrationScene(UiKit& kit, const CalibrationScene& scene);
void drawHudScene(UiKit& kit, const HudScene& scene);

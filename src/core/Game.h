#pragma once
#include "entity/PlayerBird.h"
#include "gfx/Renderer.h"
#include "gfx/Window.h"
#include "input/FlightInput.h"
#include <memory>
#include <string>

struct GameOptions {
    bool keyboard = false;
    bool hidden = false;
    int cameraIndex = 0;
    uint32_t seed = 20261207;
    std::string modelPath;
};

// Laco principal do voo pela ilha.
class Game {
public:
    explicit Game(const GameOptions& options);

    int run();
    int renderTest(const std::string& outputDir);
    int soakTest(int seconds);

private:
    void createInput();
    bool handleEvents();
    void advance(float frameSeconds);
    void refreshPreview();
    void drawTelemetry(cv::Mat& image) const;
    Camera3D buildCamera() const;
    void renderFrame();
    void captureDiveShot(const std::string& outputDir);

    GameOptions options_;
    Window window_;
    World world_;
    Renderer renderer_;
    PlayerBird player_;
    std::unique_ptr<FlightInput> input_;
    cv::Mat previewBase_;
    float accumulator_ = 0, time_ = 0, telemetryTimer_ = 0;
    int width_ = 1280, height_ = 720;
};

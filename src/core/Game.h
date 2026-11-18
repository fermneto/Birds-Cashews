#pragma once
#include "entity/PlayerBird.h"
#include "game/CashewCollector.h"
#include "gfx/Renderer.h"
#include "gfx/Window.h"
#include "input/FlightInput.h"
#include "shared/LocalAuthority.h"
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
    void updateCashews(float frameSeconds);
    void refreshPreview();
    void drawTelemetry(cv::Mat& image) const;
    Camera3D buildCamera() const;
    void prepareCashews(const Camera3D& camera);
    void renderFrame();
    cv::Mat captureFrame(float time);
    void captureDiveShot(const std::string& outputDir);
    void captureCashewShots(const std::string& outputDir);
    void aimAt(glm::vec3 eye, glm::vec3 target);
    void measureFrames(const std::string& label);

    GameOptions options_;
    Window window_;
    World world_;
    Renderer renderer_;
    PlayerBird player_;
    LocalAuthority authority_;
    CashewField cashews_;
    CashewCollector collector_;
    std::unique_ptr<FlightInput> input_;
    cv::Mat previewBase_;
    float accumulator_ = 0, time_ = 0, telemetryTimer_ = 0;
    int width_ = 1280, height_ = 720;
};

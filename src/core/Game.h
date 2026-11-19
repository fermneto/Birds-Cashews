#pragma once
#include "ai/NpcEcosystem.h"
#include "entity/PlayerBird.h"
#include "game/GameplaySession.h"
#include "gfx/Renderer.h"
#include "gfx/Window.h"
#include "input/FlightInput.h"
#include "net/NetMatch.h"
#include "ui/DemoGameplaySource.h"
#include "ui/GameUi.h"
#include <filesystem>
#include <memory>
#include <string>

struct GameOptions {
    bool keyboard = false;
    bool hidden = false;
    int cameraIndex = 0;
    uint32_t seed = 20261207;
    uint16_t port = net::kLanPort;
    std::string modelPath;
    std::filesystem::path saveDir;   // Vazio usa a pasta saves/ ao lado do executavel.
};

// Laco principal do voo pela ilha.
class Game {
public:
    explicit Game(const GameOptions& options);

    int run();
    int renderTest(const std::string& outputDir);
    int soakTest(int seconds);
    GameplaySource& gameplay() { return *session_; }
    // Sala em uso. A autoridade de rede ativa e o VoiceLink: match().voice(), com match().localPlayer().
    net::NetMatch& match() { return match_; }

private:
    void createInput();
    void createUi(const std::filesystem::path& savesDir, SoundManager::Output sound);
    void updateScreens(float frameSeconds);
    void presentUi(float frameSeconds);
    bool handleEvents();
    void advance(float frameSeconds);
    void updateCashews(float frameSeconds);
    void updateNpcs(float frameSeconds);
    void syncMatch(float frameSeconds);
    void rebuildSession();
    void prepareBirds();
    void refreshPreview();
    Camera3D buildCamera() const;
    void prepareCashews(const Camera3D& camera);
    void prepareNest(const Camera3D& camera);
    void renderFrame();
    cv::Mat captureFrame(float time);
    void captureDiveShot(const std::string& outputDir);
    void captureCashewShots(const std::string& outputDir);
    void captureNestShots(const std::string& outputDir);
    void aimAt(glm::vec3 eye, glm::vec3 target);
    void measureFrames(const std::string& label);
    void captureUiShots(const std::string& outputDir);
    void captureLiveNestShots(const std::string& outputDir);
    void measureHudFrames(GameUi& ui, DemoGameplaySource& demo);

    GameOptions options_;
    Window window_;
    World world_;
    Renderer renderer_;
    PlayerBird player_;
    CashewField cashews_;
    net::NetMatch match_;   // Declarada antes da sessao, que pode usar a autoridade dela.
    std::unique_ptr<GameplaySession> session_;
    NpcEcosystem npcs_;
    std::unique_ptr<FlightInput> input_;
    std::unique_ptr<GameUi> ui_;
    PoseSource* poseSource_ = nullptr;
    cv::Mat previewBase_;
    float accumulator_ = 0, time_ = 0, telemetryTimer_ = 0;
    int width_ = 1280, height_ = 720;
    int shownNestRevision_ = -1;
};

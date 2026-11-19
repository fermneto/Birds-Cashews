#pragma once
#include "shared/GameplayView.h"
#include "ui/CalibrationTracker.h"
#include "ui/FakeNetSession.h"
#include "ui/MenuNav.h"
#include "ui/PoseSource.h"
#include "ui/RecordBanner.h"
#include "ui/ScreenFlow.h"
#include "ui/SoundManager.h"
#include "ui/UiModels.h"
#include "ui/UiScenes.h"
#include <SDL.h>
#include <filesystem>
#include <string>
#include <vector>

struct UiConfig {
    std::filesystem::path assetsDir, savesDir;
    SoundManager::Output sound = SoundManager::Output::Device;
};

// Liga as telas, a navegacao, o HUD e os sons. O Game so repassa eventos e desenha o resultado.
class GameUi {
public:
    GameUi(const UiConfig& config, GameplaySource& gameplay);

    void attachPoseSource(PoseSource* source) { poseSource_ = source; }

    void handleEvent(const SDL_Event& event);
    void closeWindow();
    void updateMenus(const FlightControls& controls);   // Fora do jogo, quem le os comandos e a interface.
    void update(float seconds);

    // Redesenha a camada so quando algo mudou. Retorna true se ela precisa ser reenviada ao Renderer.
    bool compose(float seconds);
    void forceCompose();
    const cv::Mat& overlay() const { return kit_.canvas.pixels(); }

    UiScreen screen() const { return flow_.current(); }
    bool playing() const { return flow_.current() == UiScreen::Playing; }
    bool closed() const { return flow_.closed(); }
    bool showsCornerPreview() const { return poseSource_ && flow_.current() != UiScreen::Calibration; }

    int selectedRow() const { return cursor_.index(); }
    std::vector<MenuRow> rows() const;
    const std::string& errorMessage() const { return error_; }
    const ProfilePicker& profiles() const { return profiles_; }
    const CalibrationTracker& calibration() const { return tracker_; }
    const RecordBanner& banner() const { return banner_; }

private:
    void syncScreen();
    void enterScreen(UiScreen screen);
    void saveIfNeeded(UiScreen previous, UiScreen next);
    void refreshRows();
    void apply(NavAction action);
    void activate();
    void activateMain(int index);
    void activateCalibration(int index);
    void activateProfile(int index);
    void activateRooms(int index);
    void activateHostRow(int index);
    void activateJoinRow(int index);
    void activatePause(int index);
    void finishRoom(bool ready);
    void updateCalibration(float seconds);
    void updateCoins(float seconds);
    float coinsOpacity() const;
    void handleKey(const SDL_KeyboardEvent& key);

    TextField* activeField();
    const TextField* activeField() const;
    std::vector<MenuRow> mainRows() const;
    std::vector<MenuRow> profileRows() const;
    std::vector<MenuRow> roomRows() const;
    std::vector<MenuRow> calibrationRows() const;
    MenuScene menuScene() const;
    std::string menuTitle() const;
    std::string menuSubtitle() const;
    std::string mainFooter() const;
    std::string pauseFooter() const;
    HudScene hudScene() const;
    std::string frameKey() const;
    void redraw();

    UiKit kit_;
    SoundManager sound_;
    GameplaySource* gameplay_;
    FakeNetSession net_;   // A integracao com a rede real e da tarefa D3.
    PoseSource* poseSource_ = nullptr;
    std::filesystem::path savesDir_;

    ScreenFlow flow_;
    UiScreen shown_ = UiScreen::MainMenu;
    MenuCursor cursor_;
    ProfilePicker profiles_;
    RoomsModel rooms_;
    TextField profileName_{32}, roomName_{32}, hostAddress_{15};
    CalibrationTracker tracker_;
    RecordBanner banner_;
    cv::Mat calibrationFrame_;
    Pose calibrationPose_;
    uint64_t calibrationSerial_ = 0;
    ArmSide raisedArm_ = ArmSide::None;
    float armProgress_ = 0;
    std::string error_, recordFooter_, lastKey_;
    float clock_ = 0, sinceCompose_ = 1.0f;
    int lastCoins_ = -1;
    float coinsHold_ = 0;
};

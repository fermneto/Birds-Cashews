#pragma once

enum class UiScreen { MainMenu, Calibration, Rooms, Profile, Playing, Pause, Closed };
enum class MainOption { PlaySolo, HostRoom, JoinRoom, Calibrate, Profile, Quit };
enum class RoomMode { Host, Join };

// Regras de transicao entre as telas, sem nenhum desenho.
class ScreenFlow {
public:
    UiScreen current() const { return current_; }
    RoomMode roomMode() const { return roomMode_; }
    bool hasProfile() const { return hasProfile_; }
    bool calibrated() const { return calibrated_; }
    bool playPending() const { return playPending_; }
    bool closed() const { return current_ == UiScreen::Closed; }

    void chooseMain(MainOption option);
    void profileChosen();
    void calibrationFinished();
    void roomReady();

    // Esc: pausa o jogo, retoma a pausa e volta das telas secundarias. No menu principal nao faz nada.
    void back();
    void resume();
    void leaveToMenu();
    void closeWindow();

private:
    void routeToPlay();
    void show(UiScreen screen);

    UiScreen current_ = UiScreen::MainMenu;
    RoomMode roomMode_ = RoomMode::Join;
    bool hasProfile_ = false, calibrated_ = false, playPending_ = false;
};

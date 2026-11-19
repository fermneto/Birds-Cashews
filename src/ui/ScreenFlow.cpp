#include "ui/ScreenFlow.h"

// Depois de fechada, nenhuma tela pode reabrir.
void ScreenFlow::show(UiScreen screen) {
    if (!closed()) current_ = screen;
}

// Jogar exige um perfil escolhido e uma passagem pela calibracao nesta sessao.
void ScreenFlow::routeToPlay() {
    playPending_ = true;
    if (!hasProfile_) {
        show(UiScreen::Profile);
    } else if (!calibrated_) {
        show(UiScreen::Calibration);
    } else {
        playPending_ = false;
        show(UiScreen::Playing);
    }
}

void ScreenFlow::chooseMain(MainOption option) {
    if (current_ != UiScreen::MainMenu) return;
    playPending_ = false;
    switch (option) {
        case MainOption::PlaySolo: routeToPlay(); break;
        case MainOption::HostRoom: roomMode_ = RoomMode::Host; show(UiScreen::Rooms); break;
        case MainOption::JoinRoom: roomMode_ = RoomMode::Join; show(UiScreen::Rooms); break;
        case MainOption::Calibrate: show(UiScreen::Calibration); break;
        case MainOption::Profile: show(UiScreen::Profile); break;
        case MainOption::Quit: show(UiScreen::Closed); break;
    }
}

void ScreenFlow::profileChosen() {
    if (current_ != UiScreen::Profile) return;
    hasProfile_ = true;
    if (playPending_) routeToPlay();
    else show(UiScreen::MainMenu);
}

void ScreenFlow::calibrationFinished() {
    if (current_ != UiScreen::Calibration) return;
    calibrated_ = true;
    if (playPending_) routeToPlay();
    else show(UiScreen::MainMenu);
}

// Com a sala pronta, segue para o jogo passando por perfil e calibracao se faltarem.
void ScreenFlow::roomReady() {
    if (current_ == UiScreen::Rooms) routeToPlay();
}

void ScreenFlow::back() {
    switch (current_) {
        case UiScreen::Playing: show(UiScreen::Pause); break;
        case UiScreen::Pause: show(UiScreen::Playing); break;
        case UiScreen::Calibration:
        case UiScreen::Rooms:
        case UiScreen::Profile:
            playPending_ = false;
            show(UiScreen::MainMenu);
            break;
        case UiScreen::MainMenu:
        case UiScreen::Closed:
            break;
    }
}

void ScreenFlow::resume() {
    if (current_ == UiScreen::Pause) show(UiScreen::Playing);
}

void ScreenFlow::leaveToMenu() {
    if (current_ == UiScreen::Pause) show(UiScreen::MainMenu);
}

void ScreenFlow::closeWindow() {
    current_ = UiScreen::Closed;
}

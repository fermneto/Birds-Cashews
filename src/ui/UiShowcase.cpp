#include "ui/UiShowcase.h"
#include <opencv2/imgproc.hpp>
#include <cstring>

namespace {
constexpr float kFrame = 1.0f / 30.0f;

void hold(GameUi& ui, ScriptedPoseSource& camera, const Pose& pose, float seconds) {
    camera.setPose(pose);
    for (float t = 0; t < seconds; t += kFrame) ui.update(kFrame);
}

void showCalibration(GameUi& ui, ScriptedPoseSource& camera, const std::function<void(const std::string&)>& shot) {
    ui.attachPoseSource(nullptr);
    sendKeys(ui, SDLK_DOWN, 3);
    sendKey(ui, SDLK_RETURN);
    shot("ui_03_calibracao_sem_webcam");
    sendKey(ui, SDLK_ESCAPE);

    ui.attachPoseSource(&camera);
    sendKeys(ui, SDLK_DOWN, 3);
    sendKey(ui, SDLK_RETURN);
    Pose legsHidden = ScriptedPoseSource::standingPose();
    legsHidden.joints[LAnkle].conf = legsHidden.joints[RAnkle].conf = 0;
    hold(ui, camera, ScriptedPoseSource::withWrist(legsHidden, {60, 240}), 0.4f);
    hold(ui, camera, ScriptedPoseSource::withWrist(legsHidden, {320, 40}), 0.4f);
    shot("ui_04_calibracao_parcial");

    const Pose body = ScriptedPoseSource::standingPose();
    hold(ui, camera, body, 0.7f);
    for (const cv::Point2f& wrist : {cv::Point2f(60, 240), cv::Point2f(580, 240), cv::Point2f(320, 40), cv::Point2f(320, 440)})
        hold(ui, camera, ScriptedPoseSource::withWrist(body, wrist), 0.4f);
    hold(ui, camera, body, 0.1f);
    shot("ui_05_calibracao_completa");
    sendKey(ui, SDLK_ESCAPE);
}

void createProfile(GameUi& ui, const std::string& name, int existing) {
    sendKeys(ui, SDLK_DOWN, 4);
    sendKey(ui, SDLK_RETURN);
    sendKeys(ui, SDLK_DOWN, existing);
    sendText(ui, name);
    sendKey(ui, SDLK_RETURN);
}

void showProfiles(GameUi& ui, const std::function<void(const std::string&)>& shot) {
    createProfile(ui, "Ana", 0);
    createProfile(ui, "João Ávila", 1);
    sendKeys(ui, SDLK_DOWN, 4);
    sendKey(ui, SDLK_RETURN);
    shot("ui_06_perfil");
    sendKeys(ui, SDLK_DOWN, 2);
    sendText(ui, "Ana");
    sendKey(ui, SDLK_RETURN);
    shot("ui_07_perfil_nome_repetido");
    for (int i = 0; i < 3; i++) sendKey(ui, SDLK_BACKSPACE);
    sendKey(ui, SDLK_ESCAPE);
}

void showRooms(GameUi& ui, const std::function<void(const std::string&)>& shot) {
    sendKeys(ui, SDLK_DOWN, 2);
    sendKey(ui, SDLK_RETURN);
    sendKeys(ui, SDLK_DOWN, 2);
    sendText(ui, "192.168.0.300");
    sendKey(ui, SDLK_RETURN);
    shot("ui_08_salas_entrar");
    sendKey(ui, SDLK_ESCAPE);

    sendKey(ui, SDLK_DOWN);
    sendKey(ui, SDLK_RETURN);
    shot("ui_09_salas_hospedar");
    sendKey(ui, SDLK_ESCAPE);
}

void showHud(GameUi& ui, DemoGameplaySource& demo, ScriptedPoseSource& camera,
             const std::function<void(const std::string&)>& shot) {
    sendKey(ui, SDLK_RETURN);   // Jogar sozinho leva a calibracao da sessao.
    sendKey(ui, SDLK_RETURN);   // Pular calibracao.
    demo.setView(DemoGameplaySource::flightSample());
    ui.update(kFrame);
    shot("ui_10_hud");

    camera.setDetected(false);
    shot("ui_11_hud_corpo_fora");
    camera.setDetected(true);

    demo.setView(DemoGameplaySource::recordSample());
    ui.update(1.0f);
    shot("ui_12_hud_novo_record");

    GameplayView afterRecord = DemoGameplaySource::recordSample();
    afterRecord.recordBeatenNow = false;
    demo.setView(afterRecord);
    ui.update(10.0f);
    ui.update(10.0f);
    shot("ui_13_hud_lider_do_record");

    demo.setView(DemoGameplaySource::nestRootSample());
    shot("ui_14_ninho_inicio");
    demo.setView(DemoGameplaySource::nestEatSample());
    shot("ui_15_ninho_comer");
    demo.setView(DemoGameplaySource::nestInvestSample());
    shot("ui_16_ninho_investir");

    demo.setView(DemoGameplaySource::flightSample());
    sendKey(ui, SDLK_ESCAPE);
    shot("ui_17_pausa");
}
}

ScriptedPoseSource::ScriptedPoseSource() : frame_(480, 640, CV_8UC3) {
    for (int y = 0; y < frame_.rows; y++)
        frame_.row(y).setTo(cv::Scalar(120 - y / 8, 96 - y / 12, 70 + y / 10));
    cv::rectangle(frame_, cv::Rect(0, 360, 640, 120), cv::Scalar(64, 72, 84), -1);
    cv::rectangle(frame_, cv::Rect(70, 70, 120, 150), cv::Scalar(150, 170, 186), -1);
}

bool ScriptedPoseSource::latestPose(cv::Mat& mirroredFrame, Pose& pose) {
    if (!fresh_) return false;
    fresh_ = false;
    mirroredFrame = frame_;
    pose = pose_;
    return true;
}

void ScriptedPoseSource::setPose(const Pose& pose) {
    pose_ = pose;
    fresh_ = true;
}

Pose ScriptedPoseSource::standingPose() {
    Pose pose;
    pose.valid = true;
    const cv::Point2f points[JointCount] = {
        {320, 90}, {312, 82}, {328, 82}, {302, 88}, {338, 88}, {280, 140}, {360, 140}, {262, 200}, {378, 200},
        {250, 255}, {390, 255}, {292, 270}, {348, 270}, {288, 350}, {352, 350}, {284, 430}, {356, 430}};
    for (int i = 0; i < JointCount; i++) pose.joints[i] = {points[i], 0.9f};
    return pose;
}

Pose ScriptedPoseSource::withWrist(Pose pose, cv::Point2f position) {
    pose.joints[RWrist].pos = position;
    return pose;
}

void sendKey(GameUi& ui, SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    ui.handleEvent(event);
}

void sendKeys(GameUi& ui, SDL_Keycode key, int times) {
    for (int i = 0; i < times; i++) sendKey(ui, key);
}

void sendText(GameUi& ui, const std::string& text) {
    SDL_Event event{};
    event.type = SDL_TEXTINPUT;
    std::strncpy(event.text.text, text.c_str(), SDL_TEXTINPUTEVENT_TEXT_SIZE - 1);
    ui.handleEvent(event);
}

void runUiShowcase(GameUi& ui, DemoGameplaySource& demo, ScriptedPoseSource& camera,
                   const std::function<void(const std::string&)>& shot) {
    shot("ui_01_menu");
    FlightControls raised;
    raised.raisedArm = ArmSide::Right;
    raised.menuProgress = 0.6f;
    ui.updateMenus(raised);
    shot("ui_02_menu_gesto");
    ui.updateMenus(FlightControls{});

    showCalibration(ui, camera, shot);
    showProfiles(ui, shot);
    showRooms(ui, shot);
    showHud(ui, demo, camera, shot);
}

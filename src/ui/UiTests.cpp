#include "ui/UiTests.h"
#include "entity/PlayerBird.h"
#include "ui/BirdChorus.h"
#include "ui/UiTheme.h"
#include "core/TestRun.h"
#include "ui/FakeNetSession.h"
#include "ui/UiShowcase.h"
#include <opencv2/imgcodecs.hpp>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
// Pasta exclusiva do teste: os saves reais do jogador nunca sao abertos aqui.
class TempDir {
public:
    explicit TempDir(const std::string& name)
        : path_(fs::temp_directory_path() / ("birds_ui_" + name + "_" + std::to_string(getpid()))) {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

UiConfig testConfig(const TempDir& dir) {
    static FakeNetSession net;
    return {assetsDirectory(), dir.path(), SoundManager::Output::Silent, &net};
}

FlightControls pressed(ArmSide side, bool gesture = true) {
    FlightControls controls;
    controls.menuPressed = side;
    if (gesture) controls.raisedArm = side;
    return controls;
}

cv::Vec4b pixel(const cv::Mat& image, int x, int y) { return image.at<cv::Vec4b>(y, x); }

bool near(const cv::Vec4b& value, const cv::Vec4b& expected, int tolerance = 1) {
    for (int c = 0; c < 4; c++)
        if (std::abs(value[c] - expected[c]) > tolerance) return false;
    return true;
}

void testFlowToPlay(TestRun& t) {
    ScreenFlow flow;
    t.check("telas: comeca no menu principal", flow.current() == UiScreen::MainMenu);
    flow.back();
    t.check("telas: Esc no menu principal nao fecha o jogo", flow.current() == UiScreen::MainMenu);
    flow.chooseMain(MainOption::PlaySolo);
    t.check("telas: jogar sem perfil pede o perfil", flow.current() == UiScreen::Profile);
    flow.profileChosen();
    t.check("telas: depois do perfil vem a calibracao", flow.current() == UiScreen::Calibration);
    flow.calibrationFinished();
    t.check("telas: depois da calibracao comeca o jogo", flow.current() == UiScreen::Playing);
}

void testFlowPause(TestRun& t) {
    ScreenFlow flow;
    flow.chooseMain(MainOption::PlaySolo);
    flow.profileChosen();
    flow.calibrationFinished();
    flow.back();
    t.check("telas: Esc no jogo abre a pausa", flow.current() == UiScreen::Pause);
    flow.back();
    t.check("telas: Esc na pausa retoma o jogo", flow.current() == UiScreen::Playing);
    flow.back();
    flow.resume();
    t.check("telas: continuar retoma o jogo", flow.current() == UiScreen::Playing);
    flow.back();
    flow.leaveToMenu();
    t.check("telas: pausa volta ao menu", flow.current() == UiScreen::MainMenu);
    flow.chooseMain(MainOption::PlaySolo);
    t.check("telas: segunda partida nao repete perfil e calibracao", flow.current() == UiScreen::Playing);
}

void testFlowExit(TestRun& t) {
    ScreenFlow quit;
    quit.chooseMain(MainOption::Quit);
    t.check("telas: Sair encerra", quit.closed());
    quit.back();
    quit.resume();
    t.check("telas: nada reabre depois de encerrar", quit.closed());

    for (const MainOption option : {MainOption::HostRoom, MainOption::JoinRoom, MainOption::Calibrate, MainOption::Profile}) {
        ScreenFlow flow;
        flow.chooseMain(option);
        const bool opened = flow.current() != UiScreen::MainMenu;
        flow.closeWindow();
        t.check("telas: fechar a janela sai da tela secundaria", opened && flow.closed());
    }

    ScreenFlow pending;
    pending.chooseMain(MainOption::PlaySolo);
    pending.back();
    pending.chooseMain(MainOption::Profile);
    pending.profileChosen();
    t.check("telas: desistir de jogar limpa o pedido", pending.current() == UiScreen::MainMenu);

    ScreenFlow room;
    room.chooseMain(MainOption::JoinRoom);
    room.roomReady();
    t.check("telas: entrar na sala segue para o perfil", room.current() == UiScreen::Profile);
}

void testNavigationMapping(TestRun& t) {
    t.check("nav: esquerdo avanca", navFromControls(pressed(ArmSide::Left)) == NavAction::Next);
    t.check("nav: direito confirma", navFromControls(pressed(ArmSide::Right)) == NavAction::Confirm);
    t.check("nav: sem gesto nao navega", navFromControls(FlightControls{}) == NavAction::None);
    t.check("nav: seta para baixo avanca", navFromKey(SDLK_DOWN, false) == NavAction::Next);
    t.check("nav: seta para cima volta", navFromKey(SDLK_UP, false) == NavAction::Previous);
    t.check("nav: Enter confirma", navFromKey(SDLK_RETURN, true) == NavAction::Confirm);
    t.check("nav: Esc volta", navFromKey(SDLK_ESCAPE, false) == NavAction::Back);
    t.check("nav: espaco confirma fora do campo", navFromKey(SDLK_SPACE, false) == NavAction::Confirm);
    t.check("nav: espaco escreve no campo", navFromKey(SDLK_SPACE, true) == NavAction::None);
    t.check("nav: Q do teclado nao e gesto", !isBodyGesture(pressed(ArmSide::Left, false)));
    t.check("nav: braco erguido e gesto", isBodyGesture(pressed(ArmSide::Left)));

    MenuCursor cursor;
    cursor.setCount(3);
    cursor.previous();
    t.check("nav: lista e circular para tras", cursor.index() == 2);
    cursor.next();
    t.check("nav: lista e circular para frente", cursor.index() == 0);
    cursor.select(2);
    cursor.setCount(2);
    t.check("nav: cursor acompanha a lista menor", cursor.index() == 1);
}

void testTextField(TestRun& t) {
    TextField field(6);
    field.insert("ação");
    t.check("texto: guarda acentos", field.text() == "ação");
    field.insert("é");
    t.check("texto: nao corta acento no limite", field.text() == "ação");
    field.backspace();
    t.check("texto: apagar remove o caractere", field.text() == "açã");
    field.backspace();
    t.check("texto: apagar remove acento inteiro", field.text() == "aç");
    field.insert("\n");
    t.check("texto: ignora caractere de controle", field.text() == "aç");
    field.clear();
    field.backspace();
    t.check("texto: apagar campo vazio nao falha", field.text().empty());
}

void testUiNavigation(TestRun& t) {
    TempDir dir("nav");
    DemoGameplaySource demo;
    GameUi ui(testConfig(dir), demo);
    ui.updateMenus(pressed(ArmSide::Left));
    t.check("ui: gesto esquerdo move a opcao", ui.selectedRow() == 1);
    sendKey(ui, SDLK_UP);
    t.check("ui: seta move a opcao", ui.selectedRow() == 0);
    sendKeys(ui, SDLK_DOWN, 4);
    ui.updateMenus(pressed(ArmSide::Right));
    t.check("ui: gesto direito confirma", ui.screen() == UiScreen::Profile);

    sendText(ui, "Qe");
    ui.updateMenus(pressed(ArmSide::Left, false));
    t.check("ui: Q digitado fica no campo", ui.selectedRow() == 0 && ui.rows()[0].value == "Qe");
    ui.updateMenus(pressed(ArmSide::Left));
    t.check("ui: gesto sai do campo de texto", ui.selectedRow() == 1);
    sendKey(ui, SDLK_ESCAPE);
    t.check("ui: Esc volta ao menu", ui.screen() == UiScreen::MainMenu);
    sendKey(ui, SDLK_ESCAPE);
    t.check("ui: Esc no menu nao encerra", !ui.closed());

    sendKeys(ui, SDLK_UP, 1);
    t.check("ui: ultima opcao e Sair", ui.rows()[ui.selectedRow()].label == "Sair");
    sendKey(ui, SDLK_RETURN);
    t.check("ui: Sair encerra o jogo", ui.closed());
}

void testUiPauseSaves(TestRun& t) {
    TempDir dir("pausa");
    DemoGameplaySource demo;
    GameUi ui(testConfig(dir), demo);
    sendKey(ui, SDLK_RETURN);
    sendText(ui, "Ana");
    sendKey(ui, SDLK_RETURN);
    t.check("ui: perfil novo inicia a partida", demo.profilesStarted() == 1 && demo.view().playerName == "Ana");
    t.check("ui: sem webcam a calibracao explica", ui.screen() == UiScreen::Calibration && ui.rows()[0].label == "Continuar pelo teclado");
    sendKey(ui, SDLK_RETURN);
    t.check("ui: teclado segue sem webcam", ui.playing());

    sendKey(ui, SDLK_ESCAPE);
    t.check("ui: Esc pausa e salva", ui.screen() == UiScreen::Pause && demo.saves() == 1);
    sendKey(ui, SDLK_RETURN);
    t.check("ui: continuar volta ao jogo", ui.playing());
    sendKey(ui, SDLK_ESCAPE);
    sendKey(ui, SDLK_DOWN);
    sendKey(ui, SDLK_RETURN);
    t.check("ui: voltar ao menu salva de novo", ui.screen() == UiScreen::MainMenu && demo.saves() == 3);

    sendKey(ui, SDLK_RETURN);
    t.check("ui: nova partida comeca direto", ui.playing());
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    ui.handleEvent(quit);
    t.check("ui: fechar a janela no jogo salva e encerra", ui.closed() && demo.saves() == 4);
}

void testUiFeedback(TestRun& t) {
    TempDir dir("avisos");
    DemoGameplaySource demo;
    GameUi ui(testConfig(dir), demo);
    demo.push(GameplayFeedback::CashewCollected);
    ui.update(0.016f);
    t.check("ui: avisos sao consumidos fora do jogo", demo.drainFeedback().empty());
    t.check("ui: compoe a primeira imagem", ui.compose(1.0f));
    t.check("ui: nao recompoe sem mudanca", !ui.compose(1.0f));
    sendKey(ui, SDLK_DOWN);
    t.check("ui: recompoe quando a opcao muda", ui.compose(1.0f));
}

void testCalibrationRegions(TestRun& t) {
    t.check("calibracao: esquerda", CalibrationTracker::inRegion(CalRegion::Left, {0.1f, 0.5f}));
    t.check("calibracao: esquerda nao e direita", !CalibrationTracker::inRegion(CalRegion::Right, {0.1f, 0.5f}));
    t.check("calibracao: direita", CalibrationTracker::inRegion(CalRegion::Right, {0.9f, 0.5f}));
    t.check("calibracao: alto", CalibrationTracker::inRegion(CalRegion::Top, {0.5f, 0.1f}));
    t.check("calibracao: baixo", CalibrationTracker::inRegion(CalRegion::Bottom, {0.5f, 0.9f}));
    t.check("calibracao: centro nao marca", !CalibrationTracker::inRegion(CalRegion::Left, {0.5f, 0.5f})
                                                && !CalibrationTracker::inRegion(CalRegion::Top, {0.5f, 0.5f}));
    t.check("calibracao: fora do quadro nao marca", !CalibrationTracker::inRegion(CalRegion::Left, {-0.1f, 0.5f}));

    const cv::Size frame(640, 480);
    Pose body = ScriptedPoseSource::standingPose();
    t.check("calibracao: corpo inteiro aceito", CalibrationTracker::isFullBody(body, frame));
    Pose noFeet = body;
    noFeet.joints[LAnkle].conf = 0.1f;
    t.check("calibracao: sem o tornozelo nao e corpo inteiro", !CalibrationTracker::isFullBody(noFeet, frame));
    Pose cut = body;
    cut.joints[RAnkle].pos.y = 479.0f;
    t.check("calibracao: ponto colado na borda nao conta", !CalibrationTracker::isFullBody(cut, frame));
    t.check("calibracao: pose invalida nao conta", !CalibrationTracker::isFullBody(Pose{}, frame));
}

void testCalibrationTracker(TestRun& t) {
    const cv::Size frame(640, 480);
    const Pose body = ScriptedPoseSource::standingPose();
    CalibrationTracker tracker;
    tracker.update(ScriptedPoseSource::withWrist(body, {40, 240}), frame, 0.05f);
    t.check("calibracao: toque rapido nao marca", !tracker.reached(CalRegion::Left));
    tracker.update(ScriptedPoseSource::withWrist(body, {40, 240}), frame, 0.3f);
    t.check("calibracao: mao mantida marca a regiao", tracker.reached(CalRegion::Left));
    tracker.update(body, frame, 1.0f);
    t.check("calibracao: marca continua depois de sair", tracker.reached(CalRegion::Left) && tracker.bodyConfirmed());
    t.check("calibracao: incompleta nao aceita gesto", !tracker.acceptsGesture(pressed(ArmSide::Right)));

    for (const cv::Point2f& wrist : {cv::Point2f(600, 240), cv::Point2f(320, 30), cv::Point2f(320, 450)})
        tracker.update(ScriptedPoseSource::withWrist(body, wrist), frame, 0.3f);
    t.check("calibracao: completa com as cinco marcas", tracker.complete() && tracker.marksDone() == 5);
    t.check("calibracao: braco ainda erguido nao confirma", !tracker.acceptsGesture(pressed(ArmSide::Right)));
    tracker.acceptsGesture(FlightControls{});
    t.check("calibracao: depois de baixar o braco aceita gesto", tracker.acceptsGesture(pressed(ArmSide::Right)));
    tracker.reset();
    t.check("calibracao: reiniciar limpa as marcas", tracker.marksDone() == 0);
}

void testCalibrationScreen(TestRun& t) {
    TempDir dir("calibra");
    DemoGameplaySource demo;
    GameUi ui(testConfig(dir), demo);
    ScriptedPoseSource camera;
    ui.attachPoseSource(&camera);
    sendKeys(ui, SDLK_DOWN, 3);
    sendKey(ui, SDLK_RETURN);
    t.check("calibracao: tela abre pelo menu", ui.screen() == UiScreen::Calibration && !ui.showsCornerPreview());
    ui.updateMenus(pressed(ArmSide::Right));
    t.check("calibracao: gesto nao sai antes de concluir", ui.screen() == UiScreen::Calibration);
    camera.setPose(ScriptedPoseSource::withWrist(ScriptedPoseSource::standingPose(), {40, 240}));
    ui.update(0.5f);
    t.check("calibracao: tela le a webcam simulada", ui.calibration().reached(CalRegion::Left));
    t.check("calibracao: Enter ainda pode pular", ui.rows()[0].label == "Pular calibração");
    sendKey(ui, SDLK_RETURN);
    t.check("calibracao: pular volta ao menu", ui.screen() == UiScreen::MainMenu);
}

void testProfiles(TestRun& t) {
    TempDir dir("perfil");
    DemoGameplaySource demo;
    ProfilePicker picker(dir.path());
    t.check("perfil: pasta nova sem perfis", picker.names().empty());
    t.check("perfil: nome vazio recusado", picker.create("   ", demo) == ProfileResult::InvalidName);
    t.check("perfil: cria com acento", picker.create("  João ", demo) == ProfileResult::Ok);
    t.check("perfil: nome sem espacos nas pontas", picker.names().size() == 1 && picker.names()[0] == "João");
    t.check("perfil: criar ja inicia a partida", demo.view().playerName == "João" && picker.active() == "João");
    t.check("perfil: nome repetido recusado", picker.create("João", demo) == ProfileResult::Duplicate);
    t.check("perfil: segundo perfil", picker.create("Ana", demo) == ProfileResult::Ok);
    t.check("perfil: escolhe existente", picker.select("João", demo) == ProfileResult::Ok && demo.view().playerName == "João");
    t.check("perfil: inexistente recusado", picker.select("Zeca", demo) == ProfileResult::Missing);
    t.check("perfil: gravado na pasta temporaria", fs::exists(dir.path() / "perfis" / ProfileStore::fileNameFor("Ana")));

    ProfilePicker reopened(dir.path());
    t.check("perfil: lista sobrevive ao reabrir", reopened.names() == std::vector<std::string>({"Ana", "João"}));
}

void testProfileScreen(TestRun& t) {
    TempDir dir("telaperfil");
    DemoGameplaySource demo;
    GameUi ui(testConfig(dir), demo);
    sendKeys(ui, SDLK_DOWN, 4);
    sendKey(ui, SDLK_RETURN);
    sendText(ui, "Ana");
    sendKey(ui, SDLK_RETURN);
    t.check("perfil: tela cria e volta ao menu", ui.screen() == UiScreen::MainMenu && ui.profiles().active() == "Ana");
    sendKey(ui, SDLK_RETURN);
    t.check("perfil: jogar usa o perfil escolhido", ui.screen() == UiScreen::Profile || ui.screen() == UiScreen::Calibration);
    sendKey(ui, SDLK_ESCAPE);
    sendKeys(ui, SDLK_DOWN, 4);
    sendKey(ui, SDLK_RETURN);
    sendKey(ui, SDLK_DOWN);
    sendText(ui, "Ana");
    sendKey(ui, SDLK_RETURN);
    t.check("perfil: repetido mostra erro", ui.screen() == UiScreen::Profile && !ui.errorMessage().empty());
    sendKey(ui, SDLK_UP);
    sendKey(ui, SDLK_RETURN);
    t.check("perfil: escolher existente volta ao menu", ui.screen() == UiScreen::MainMenu);
}

void testRooms(TestRun& t) {
    t.check("salas: IP valido", RoomsModel::isValidAddress("192.168.0.10"));
    for (const char* bad : {"", "192.168.0", "192.168.0.256", "1.2.3.4.5", "1.2.3.", "a.b.c.d", "01.2.3.4"})
        t.check(std::string("salas: IP invalido '") + bad + "'", !RoomsModel::isValidAddress(bad));

    FakeNetSession net;
    RoomsModel rooms(net);
    rooms.refresh();
    t.check("salas: lista pela interface", rooms.rooms().size() == 2);
    t.check("salas: entra pelo IP", rooms.joinAddress(" 10.0.0.2 ") && net.joined() == "10.0.0.2");
    t.check("salas: entra pela lista", rooms.joinRoom(1) && net.joined() == "192.168.0.27");
    t.check("salas: indice invalido recusado", !rooms.joinRoom(9));
    t.check("salas: nome vazio recusado", !rooms.hostRoom("  ") && !rooms.error().empty());
    t.check("salas: hospeda", rooms.hostRoom("Ilha") && net.hosted() == "Ilha");
    net.setAccept(false);
    t.check("salas: falha da rede vira mensagem", !rooms.joinAddress("10.0.0.2") && !rooms.error().empty());
    net.setAccept(false, "A sala está cheia.");
    t.check("salas: mostra o motivo dado pela rede", !rooms.joinAddress("10.0.0.2") && rooms.error() == "A sala está cheia.");
}

// A descoberta real devolve o endereco com a porta, como "10.0.0.5:42917".
class DiscoveredRoomSession : public NetSession {
public:
    bool host(const std::string&) override { return true; }
    bool join(const std::string& address) override { joined = address; return true; }
    std::vector<RoomInfo> discoverRooms() override { return {{"Sala real", "192.168.0.10:42917", 1}}; }
    void leave() override {}
    std::string joined;
};

void testRoomAddresses(TestRun& t) {
    t.check("salas: endereco com porta e valido", RoomsModel::isValidAddress("192.168.0.10:42917"));
    t.check("salas: virgula do teclado numerico vira ponto", RoomsModel::isValidAddress("127,0,0,1"));
    for (const char* bad : {"10.0.0.5:", "10.0.0.5:0", "10.0.0.5:70000", "10.0.0.5:abc", ":42917"})
        t.check(std::string("salas: endereco invalido '") + bad + "'", !RoomsModel::isValidAddress(bad));

    DiscoveredRoomSession net;
    RoomsModel rooms(net);
    rooms.refresh();
    t.check("salas: entra na sala descoberta na rede", rooms.joinRoom(0) && net.joined == "192.168.0.10:42917");
    t.check("salas: IP digitado com virgula entra", rooms.joinAddress("127,0,0,1") && net.joined == "127.0.0.1");
}

void testRoomScreen(TestRun& t) {
    TempDir dir("telasala");
    DemoGameplaySource demo;
    GameUi ui(testConfig(dir), demo);
    sendKeys(ui, SDLK_DOWN, 2);
    sendKey(ui, SDLK_RETURN);
    t.check("salas: tela abre com salas e IP", ui.screen() == UiScreen::Rooms && ui.rows().size() == 5);
    sendKeys(ui, SDLK_DOWN, 2);
    sendText(ui, "999.1.1.1");
    sendKey(ui, SDLK_RETURN);
    t.check("salas: IP errado fica na tela com aviso", ui.screen() == UiScreen::Rooms && !ui.errorMessage().empty());
    for (int i = 0; i < 9; i++) sendKey(ui, SDLK_BACKSPACE);
    sendText(ui, "192.168.0.10");
    sendKey(ui, SDLK_RETURN);
    t.check("salas: entrar segue para o perfil", ui.screen() == UiScreen::Profile);
}

void testAlphaComposition(TestRun& t) {
    cv::Mat straight(1, 3, CV_8UC4);
    straight.at<cv::Vec4b>(0, 0) = {200, 100, 50, 255};
    straight.at<cv::Vec4b>(0, 1) = {200, 100, 50, 128};
    straight.at<cv::Vec4b>(0, 2) = {200, 100, 50, 0};
    const cv::Mat source = premultiplied(straight);
    t.check("alfa: opaco nao muda", near(pixel(source, 0, 0), {200, 100, 50, 255}, 0));
    t.check("alfa: metade multiplica a cor", near(pixel(source, 1, 0), {100, 50, 25, 128}));
    t.check("alfa: transparente zera a cor", near(pixel(source, 2, 0), {0, 0, 0, 0}, 0));

    cv::Mat target(1, 3, CV_8UC4, cv::Scalar(20, 40, 80, 255));
    compositeOver(target, source, {0, 0});
    t.check("alfa: opaco cobre o fundo", near(pixel(target, 0, 0), {200, 100, 50, 255}, 0));
    t.check("alfa: metade mistura com o fundo", near(pixel(target, 1, 0), {110, 70, 65, 255}));
    t.check("alfa: transparente preserva o fundo", near(pixel(target, 2, 0), {20, 40, 80, 255}, 0));

    cv::Mat empty(1, 1, CV_8UC4, cv::Scalar::all(0));
    compositeOver(empty, premultiplied(cv::Mat(1, 1, CV_8UC4, cv::Scalar(200, 100, 50, 128))), {0, 0});
    t.check("alfa: sobre camada vazia mantem o alfa", near(pixel(empty, 0, 0), {100, 50, 25, 128}));

    cv::Mat faded(1, 1, CV_8UC4, cv::Scalar(20, 40, 80, 255));
    compositeOver(faded, cv::Mat(1, 1, CV_8UC4, cv::Scalar(200, 100, 50, 255)), {0, 0}, 0.5f);
    t.check("alfa: opacidade reduz a imagem inteira", near(pixel(faded, 0, 0), {110, 70, 65, 255}));

    cv::Mat clipped(2, 2, CV_8UC4, cv::Scalar(1, 2, 3, 255));
    const cv::Mat white(2, 2, CV_8UC4, cv::Scalar::all(255));
    compositeOver(clipped, white, {1, 1});
    compositeOver(clipped, white, {-5, -5});
    t.check("alfa: recorta nas bordas", near(pixel(clipped, 1, 1), {255, 255, 255, 255}, 0) && near(pixel(clipped, 0, 0), {1, 2, 3, 255}, 0));

    Canvas canvas(4, 4);
    canvas.fill({0, 0, 4, 4}, cv::Scalar(100, 200, 40), 0.5f);
    t.check("alfa: painel translucido pre-multiplicado", near(pixel(canvas.pixels(), 1, 1), {50, 100, 20, 128}));
    canvas.clear();
    t.check("alfa: limpar deixa transparente", near(pixel(canvas.pixels(), 1, 1), {0, 0, 0, 0}, 0));

    cv::Mat arrow(5, 5, CV_8UC4, cv::Scalar::all(0));
    arrow.at<cv::Vec4b>(0, 2) = {255, 255, 255, 255};
    const cv::Mat turned = rotatedClockwise(arrow, static_cast<float>(CV_PI / 2));
    t.check("alfa: giro positivo e horario", pixel(turned, 4, 2)[3] > 200 && pixel(turned, 0, 2)[3] < 40);
}

bool sameImage(const cv::Mat& a, const cv::Mat& b) { return cv::norm(a, b, cv::NORM_INF) == 0; }

cv::Mat hudImage(UiKit& kit, const HudScene& scene) {
    kit.canvas.clear();
    drawHudScene(kit, scene);
    return kit.canvas.pixels().clone();
}

void testCleanHud(TestRun& t) {
    Canvas shade(10, 4);
    shade.gradient({0, 0, 10, 4}, cv::Scalar(100, 100, 100), 1.0f, 0.0f, true);
    t.check("degrade: comeca opaco e termina transparente",
            pixel(shade.pixels(), 0, 1)[3] == 255 && pixel(shade.pixels(), 9, 1)[3] == 0);

    UiKit kit(assetsDirectory());
    GameplayView view = DemoGameplaySource::flightSample();
    HudScene scene;
    scene.view = &view;
    view.leadingRecord = false;
    const cv::Mat following = hudImage(kit, scene);
    t.check("hud: em voo nada escurece o topo da tela",
            pixel(following, 420, 4)[3] == 0 && pixel(following, 900, 60)[3] == 0 && pixel(following, 640, 150)[3] == 0);
    view.leadingRecord = true;
    t.check("hud: liderar o record nao deixa mensagem fixa na tela", sameImage(following, hudImage(kit, scene)));

    scene.bannerOpacity = 1.0f;
    t.check("hud: bater o record mostra a notificacao", !sameImage(following, hudImage(kit, scene)));
    scene.bannerOpacity = 0.0f;
    t.check("hud: a notificacao some sozinha", sameImage(following, hudImage(kit, scene)));

    scene.coinsOpacity = 1.0f;
    t.check("hud: moedas aparecem quando mudam", !sameImage(following, hudImage(kit, scene)));
    scene.coinsOpacity = 0.0f;

    view.menu = DemoGameplaySource::nestRootSample().menu;
    scene.showPreview = true;
    const cv::Mat nest = hudImage(kit, scene);
    t.check("hud: o menu do ninho nao escurece a webcam",
            pixel(nest, kPreviewArea.x + 20, kPreviewArea.y + 20)[3] == 0);
}

void testAssets(TestRun& t) {
    UiAssets assets(assetsDirectory());
    for (int i = 0; i < static_cast<int>(UiIcon::Count); i++) {
        const auto icon = static_cast<UiIcon>(i);
        const std::string name = iconFileName(icon);
        const cv::Mat raw = cv::imread((assetsDirectory() / name).string(), cv::IMREAD_UNCHANGED);
        t.check("assets: " + name + " tem 4 canais", raw.channels() == 4 && raw.depth() == CV_8U);
        std::vector<cv::Mat> channels;
        cv::split(raw, channels);
        double low = 0, high = 0;
        cv::minMaxLoc(channels[3], &low, &high);
        t.check("assets: " + name + " tem fundo transparente e desenho opaco", low == 0 && high == 255);
    }
    t.check("assets: fonte presente", fs::exists(assets.fontPath()));
    t.check("assets: imagem reduzida no tamanho pedido", assets.icon(UiIcon::Coin, 40).size() == cv::Size(40, 40));

    t.check("assets: icone de velocidade", iconForOption("Velocidade") == UiIcon::UpgradeSpeed);
    t.check("assets: icone de carga", iconForOption("Capacidade de carga") == UiIcon::UpgradeCargo);
    t.check("assets: icone de cofre em maiusculas", iconForOption("COFRE") == UiIcon::UpgradeVault);
    t.check("assets: opcao desconhecida sem icone", !iconForOption("Decolar").has_value());
    bool missingThrows = false;
    try {
        UiAssets broken("/caminho/que/nao/existe");
    } catch (const std::exception&) {
        missingThrows = true;
    }
    t.check("assets: pasta ausente gera erro claro", missingThrows);
}

void testText(TestRun& t) {
    UiAssets assets(assetsDirectory());
    TextPainter painter(assets.fontPath());
    const auto inkAbove = [&](const std::string& text) {
        Canvas canvas(120, 90);
        TextStyle style;
        style.size = 40;
        style.outline = false;
        painter.draw(canvas, text, {20, 70}, style);
        cv::Mat alpha;
        cv::extractChannel(canvas.pixels()(cv::Rect(0, 0, 120, 45)), alpha, 3);
        return cv::countNonZero(alpha);
    };
    t.check("texto: acento aparece acima da letra", inkAbove("ã") > inkAbove("a") + 10);
    t.check("texto: cedilha tem largura", painter.width("ç", 30) > 0);
    t.check("texto: quebra linhas pela largura", painter.wrap("uma frase bem comprida para quebrar", 24, 180).size() >= 2);
    Canvas canvas(100, 40);
    TextStyle style;
    painter.drawFit(canvas, "Um nome de perfil muito comprido mesmo para caber aqui", {4, 30}, 90, style);
    cv::Mat rightEdge;
    cv::extractChannel(canvas.pixels()(cv::Rect(97, 0, 3, 40)), rightEdge, 3);
    t.check("texto: nome longo nao passa do limite", cv::countNonZero(rightEdge) == 0);
    const size_t before = painter.cachedSprites();
    painter.draw(canvas, "Repetido", {0, 30}, style);
    painter.draw(canvas, "Repetido", {0, 30}, style);
    t.check("texto: reaproveita imagem pronta", painter.cachedSprites() == before + 1);
}

void testRecordBanner(TestRun& t) {
    RecordBanner banner;
    banner.update(false, 1.0f);
    t.check("banner: oculto sem record", !banner.visible() && banner.opacity() == 0.0f);
    banner.update(true, 0.0f);
    t.check("banner: aparece quando o record cai", banner.visible());
    banner.update(true, 0.1f);
    const float entering = banner.opacity();
    banner.update(true, 0.5f);
    t.check("banner: entra aos poucos", entering > 0.0f && entering < banner.opacity() && banner.opacity() == 1.0f);
    for (int i = 0; i < 20; i++) banner.update(true, 1.0f);
    t.check("banner: fica enquanto a partida pedir", banner.visible() && banner.opacity() == 1.0f);

    RecordBanner quick;
    quick.update(true, 0.0f);
    quick.update(false, 1.0f);
    t.check("banner: respeita o tempo minimo de leitura", quick.visible() && quick.opacity() == 1.0f);
    quick.update(false, 2.4f);
    quick.update(false, 0.3f);
    t.check("banner: some aos poucos", quick.visible() && quick.opacity() < 1.0f && quick.opacity() > 0.0f);
    quick.update(false, 1.0f);
    t.check("banner: termina depois de sumir", !quick.visible());
    quick.update(false, 1.0f);
    t.check("banner: nao reaparece sozinho", !quick.visible());
    quick.update(true, 0.0f);
    t.check("banner: novo record mostra de novo", quick.visible());
}

void testSounds(TestRun& t) {
    {
        SoundManager silent(SoundManager::Output::Silent);
        for (int i = 0; i < static_cast<int>(UiSound::Count); i++) silent.play(static_cast<UiSound>(i));
        t.check("som: modo silencioso nao falha", !silent.hasDevice());
    }
    {
        SDL_setenv("SDL_AUDIODRIVER", "driver_inexistente", 1);
        SoundManager missing;
        missing.play(GameplayFeedback::RecordBeaten);
        t.check("som: sem dispositivo o jogo continua", !missing.hasDevice());
        unsetenv("SDL_AUDIODRIVER");
    }
    for (int i = 0; i < static_cast<int>(UiSound::Count); i++) {
        const std::vector<int16_t> samples = SoundManager::synthesize(static_cast<UiSound>(i));
        const auto [low, high] = std::minmax_element(samples.begin(), samples.end());
        const bool audible = !samples.empty() && *high > 1500 && *low < -1500;
        const bool notClipped = !samples.empty() && *high < 32767 && *low > -32767;
        const bool shortEnough = samples.size() < static_cast<size_t>(SoundManager::sampleRate());
        t.check("som: efeito " + std::to_string(i) + " audivel, sem saturar e curto", audible && notClipped && shortEnough);
    }
    const GameplayFeedback all[] = {GameplayFeedback::CashewCollected, GameplayFeedback::CashewDelivered, GameplayFeedback::UpgradeBought,
                                    GameplayFeedback::UpgradeDenied, GameplayFeedback::RecordBeaten, GameplayFeedback::MenuMove,
                                    GameplayFeedback::NestLanded, GameplayFeedback::TakeOff, GameplayFeedback::CargoLost,
                                    GameplayFeedback::CashewStolen, GameplayFeedback::NestAlarm, GameplayFeedback::RivalKnocked};
    std::vector<UiSound> mapped;
    for (const GameplayFeedback feedback : all) mapped.push_back(soundFor(feedback));
    std::sort(mapped.begin(), mapped.end());
    t.check("som: cada aviso da partida tem efeito proprio", std::unique(mapped.begin(), mapped.end()) == mapped.end());
}
}

void testAmbience(TestRun& t) {
    for (int i = 0; i < kAmbientLayers; i++) {
        const std::vector<int16_t> loop = synthesizeAmbientLoop(static_cast<AmbientLayer>(i), SoundManager::sampleRate());
        const auto [low, high] = std::minmax_element(loop.begin(), loop.end());
        const bool long8s = loop.size() == static_cast<size_t>(8 * SoundManager::sampleRate());
        const bool calm = !loop.empty() && *high > 3000 && *high < 15000 && *low > -15000;
        // A emenda nao pode saltar mais que o maior passo entre duas amostras dentro do proprio loop.
        int largestStep = 0;
        for (size_t k = 1; k < loop.size(); k++) largestStep = std::max(largestStep, std::abs(loop[k] - loop[k - 1]));
        const int seam = std::abs(static_cast<int>(loop.back()) - static_cast<int>(loop.front()));
        t.check("ambiente: camada " + std::to_string(i) + " tem 8 s, volume contido e emenda sem estalo",
                long8s && calm && seam <= largestStep);
    }
    AmbientListener menu;
    const AmbientMix calm = ambientMixFor(menu);
    t.check("ambiente: nos menus so vento e mar, baixos", calm.wind > 0 && calm.wind < 0.3f && calm.rush == 0 && calm.climb == 0);

    AmbientListener cruise;
    cruise.playing = true;
    cruise.position = glm::vec3(0, 60, 0);
    cruise.speed = 24;
    const AmbientMix level = ambientMixFor(cruise);
    t.check("ambiente: o vento de fundo esta sempre presente no voo", level.wind >= 0.45f);
    AmbientListener fast = cruise;
    fast.speed = 70;
    t.check("ambiente: o zunido cresce com a velocidade", ambientMixFor(fast).rush > level.rush + 0.6f);
    AmbientListener rising = cruise;
    rising.verticalSpeed = 8;
    AmbientListener diving = fast;
    diving.verticalSpeed = -30;
    t.check("ambiente: subir liga o tom de subida, e so ele", ambientMixFor(rising).climb > 0.7f && ambientMixFor(rising).dive == 0 &&
                                                               level.climb == 0);
    t.check("ambiente: mergulhar liga o assobio, e so ele", ambientMixFor(diving).dive > 0.9f && ambientMixFor(diving).climb == 0);
    bool fluid = true;
    float previous = ambientMixFor(cruise).climb;
    for (float vy = 0; vy <= 12; vy += 0.25f) {
        AmbientListener step = cruise;
        step.verticalSpeed = vy;
        const float now = ambientMixFor(step).climb;
        fluid = fluid && now >= previous && now - previous < 0.08f;
        previous = now;
    }
    t.check("ambiente: a subida aumenta aos poucos, sem saltos", fluid);
    AmbientListener coast = cruise;
    coast.position = glm::vec3(950, 20, 0);
    t.check("ambiente: o mar so aparece perto da costa", ambientMixFor(coast).sea > 0.8f && level.sea == 0.0f);

    SoundManager silent(SoundManager::Output::Silent);
    AmbientMix wanted;
    wanted.wind = 2.0f;
    wanted.sea = 0.4f;
    silent.setAmbience(wanted);
    silent.play(UiSound::Flap, 0.5f);
    t.check("ambiente: sem audio guarda o pedido, limitado a 1",
            silent.ambience().wind == 1.0f && std::abs(silent.ambience().sea - 0.4f) < 1e-6f);
}

void testBirdChorus(TestRun& t) {
    BirdChorus chorus;
    const glm::vec3 me(0, 50, 0);
    int far = 0;
    for (int i = 0; i < 1200; i++) far += static_cast<int>(chorus.update(1.0f / 60.0f, me, {{1, me + glm::vec3(200, 0, 0), BirdVoice::Small}}).size());
    t.check("passaros: longe nao canta nenhum", far == 0);

    std::vector<SingingBird> flock;
    for (uint32_t i = 0; i < 12; i++) flock.push_back({i, me + glm::vec3(10.0f + i, 0, 0), BirdVoice::Small});
    std::vector<ChirpRequest> heard;
    float lastAt = -10, smallestGap = 100, time = 0;
    for (int i = 0; i < 60 * 30; i++) {
        time += 1.0f / 60.0f;
        for (const ChirpRequest& chirp : chorus.update(1.0f / 60.0f, me, flock)) {
            heard.push_back(chirp);
            smallestGap = std::min(smallestGap, time - lastAt);
            lastAt = time;
        }
    }
    t.check("passaros: um bando por perto canta, mas com folga entre os cantos",
            heard.size() >= 10 && heard.size() <= 45 && smallestGap >= BirdChorus::kMinGap - 1e-3f);
    const bool smallTypes = std::all_of(heard.begin(), heard.end(), [](const ChirpRequest& c) {
        return c.sound == UiSound::ChirpTweet || c.sound == UiSound::ChirpTrill;
    });
    t.check("passaros: os pequenos piam ou trinam", smallTypes && chirpFor(BirdVoice::Small, 1) != chirpFor(BirdVoice::Small, 2));
    t.check("passaros: cada porte tem o proprio canto",
            chirpFor(BirdVoice::Medium, 1) == UiSound::ChirpWhistle && chirpFor(BirdVoice::Large, 1) == UiSound::ChirpCaw);

    BirdChorus near, distant;
    float nearVolume = 0, distantVolume = 1;
    for (int i = 0; i < 60 * 20; i++) {
        for (const ChirpRequest& c : near.update(1.0f / 60.0f, me, {{1, me + glm::vec3(5, 0, 0), BirdVoice::Medium}})) nearVolume = c.volume;
        for (const ChirpRequest& c : distant.update(1.0f / 60.0f, me, {{1, me + glm::vec3(70, 0, 0), BirdVoice::Medium}})) distantVolume = c.volume;
    }
    t.check("passaros: de perto o canto e mais alto", nearVolume > 0.8f && distantVolume < 0.3f);
}

void testFlapSound(TestRun& t) {
    PlayerBird bird;
    FlightControls key;
    key.flaps = 1;
    key.flapStrength = 0.8f;
    bird.applyControls(key);
    const float keyboard = bird.takeFlap();
    t.check("batida: o Espaco faz o som da asa uma vez", keyboard > 0.7f && bird.takeFlap() == 0.0f);

    FlightControls body;
    body.bodyControlled = true;
    int flaps = 0;
    for (float stroke : {0.0f, 0.2f, 0.6f, 0.9f, 0.7f, 0.1f, 0.0f, 0.5f, 0.8f, 0.0f}) {
        body.stroke = stroke;
        bird.applyControls(body);
        flaps += bird.takeFlap() > 0 ? 1 : 0;
    }
    t.check("batida: cada descida dos bracos soa uma unica vez", flaps == 2);
}

int runUiSelfTest() {
    TestRun t;
    testFlowToPlay(t);
    testFlowPause(t);
    testFlowExit(t);
    testNavigationMapping(t);
    testTextField(t);
    testUiNavigation(t);
    testUiPauseSaves(t);
    testUiFeedback(t);
    testAmbience(t);
    testBirdChorus(t);
    testFlapSound(t);
    testCalibrationRegions(t);
    testCalibrationTracker(t);
    testCalibrationScreen(t);
    testProfiles(t);
    testProfileScreen(t);
    testRooms(t);
    testRoomAddresses(t);
    testRoomScreen(t);
    testAlphaComposition(t);
    testCleanHud(t);
    testAssets(t);
    testText(t);
    testRecordBanner(t);
    testSounds(t);
    return t.failures;
}

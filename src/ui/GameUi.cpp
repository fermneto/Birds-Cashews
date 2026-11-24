#include "ui/GameUi.h"
#include "save/RecordManager.h"
#include "shared/GameTypes.h"
#include <cmath>
#include <sstream>

namespace {
constexpr float kCoinsVisibleSeconds = 3.5f, kCoinsFadeSeconds = 0.5f;
constexpr float kMinComposeSeconds = 1.0f / 30.0f;
 
// Usada so quando ninguem informa a rede, como em ferramentas que nao jogam em sala.
class OfflineNetSession : public NetSession {
public:
    bool host(const std::string&) override { return false; }
    bool join(const std::string&) override { return false; }
    std::vector<RoomInfo> discoverRooms() override { return {}; }
    void leave() override {}
    std::string lastError() const override { return "A rede não está disponível neste modo."; }
};

NetSession& offlineSession() {
    static OfflineNetSession session;
    return session;
}

int quantized(float value, float steps) {
    return static_cast<int>(std::lround(value * steps));
}
}

GameUi::GameUi(const UiConfig& config, GameplaySource& gameplay)
    : kit_(config.assetsDir), sound_(config.sound), gameplay_(&gameplay),
      net_(config.net ? config.net : &offlineSession()), savesDir_(config.savesDir), profiles_(config.savesDir),
      rooms_(*net_) {
    enterScreen(UiScreen::MainMenu);
}

// ---- Transicoes ----

void GameUi::saveIfNeeded(UiScreen previous, UiScreen next) {
    if (!flow_.hasProfile()) return;
    const bool wasInMatch = previous == UiScreen::Playing || previous == UiScreen::Pause;
    const bool leftMatch = next == UiScreen::Pause || next == UiScreen::MainMenu || next == UiScreen::Closed;
    if (wasInMatch && leftMatch) gameplay_->saveNow();
}

void GameUi::syncScreen() {
    const UiScreen next = flow_.current();
    if (next == shown_) return;
    saveIfNeeded(shown_, next);
    // Voltar ao menu principal ou fechar o jogo sai da sala, se houver uma.
    if (next == UiScreen::MainMenu || next == UiScreen::Closed) rooms_.leave();
    shown_ = next;
    enterScreen(next);
}

void GameUi::enterScreen(UiScreen screen) {
    error_.clear();
    rooms_.clearError();
    raisedArm_ = ArmSide::None;
    armProgress_ = 0;
    cursor_.select(0);
    if (screen == UiScreen::MainMenu) {
        const RecordManager record(savesDir_);
        recordFooter_ = record.record() > 0
            ? "Record: " + std::to_string(record.record()) + " cajus (" + record.holder() + ")" : "Nenhum record ainda";
    }
    if (screen == UiScreen::Profile) profiles_.refresh();
    if (screen == UiScreen::Rooms) {
        rooms_.refresh();
        if (roomName_.text().empty()) roomName_.set(profiles_.active().empty() ? "Minha sala" : "Sala de " + profiles_.active());
    }
    if (screen == UiScreen::Calibration) {
        tracker_.reset();
        calibrationFrame_.release();
    }
    refreshRows();
}

void GameUi::refreshRows() {
    cursor_.setCount(static_cast<int>(rows().size()));
}

void GameUi::closeWindow() {
    flow_.closeWindow();
    syncScreen();
}

// ---- Listas de opcoes ----

std::vector<MenuRow> GameUi::mainRows() const {
    return {{"Jogar sozinho", "", false}, {"Hospedar sala", "", false}, {"Entrar em sala", "", false},
            {"Calibrar", "", false}, {"Perfil", profiles_.active(), false}, {"Sair", "", false}};
}

std::vector<MenuRow> GameUi::profileRows() const {
    std::vector<MenuRow> rows;
    for (const std::string& name : profiles_.names())
        rows.push_back({name, name == profiles_.active() ? "em uso" : "", false});
    rows.push_back({"Novo perfil", profileName_.text(), true});
    rows.push_back({"Voltar", "", false});
    return rows;
}

std::vector<MenuRow> GameUi::roomRows() const {
    std::vector<MenuRow> rows;
    if (flow_.roomMode() == RoomMode::Host) {
        rows.push_back({"Nome da sala", roomName_.text(), true});
        rows.push_back({"Criar sala", "", false});
    } else {
        for (const RoomInfo& room : rooms_.rooms())
            rows.push_back({room.name, room.address + "  " + std::to_string(room.players) + "/" + std::to_string(kMaxPlayers), false});
        rows.push_back({"IP do host", hostAddress_.text(), true});
        rows.push_back({"Atualizar lista", "", false});
    }
    rows.push_back({"Voltar", "", false});
    return rows;
}

std::vector<MenuRow> GameUi::calibrationRows() const {
    const char* const proceed = !poseSource_ ? "Continuar pelo teclado" : tracker_.complete() ? "Continuar" : "Pular calibração";
    return {{proceed, "", false}, {"Voltar", "", false}};
}

std::vector<MenuRow> GameUi::rows() const {
    switch (flow_.current()) {
        case UiScreen::MainMenu: return mainRows();
        case UiScreen::Calibration: return calibrationRows();
        case UiScreen::Profile: return profileRows();
        case UiScreen::Rooms: return roomRows();
        case UiScreen::Pause: return {{"Continuar", "", false}, {"Voltar ao menu", "", false}};
        default: return {};
    }
}

const TextField* GameUi::activeField() const {
    const int index = cursor_.index();
    if (flow_.current() == UiScreen::Profile && index == static_cast<int>(profiles_.names().size())) return &profileName_;
    if (flow_.current() != UiScreen::Rooms) return nullptr;
    if (flow_.roomMode() == RoomMode::Host) return index == 0 ? &roomName_ : nullptr;
    return index == static_cast<int>(rooms_.rooms().size()) ? &hostAddress_ : nullptr;
}

TextField* GameUi::activeField() {
    return const_cast<TextField*>(static_cast<const GameUi*>(this)->activeField());
}

// ---- Entrada ----

void GameUi::handleKey(const SDL_KeyboardEvent& key) {
    const SDL_Keycode code = key.keysym.sym;
    if (flow_.current() == UiScreen::Playing) {
        if (code == SDLK_ESCAPE && !key.repeat) apply(NavAction::Back);
        return;
    }
    TextField* field = activeField();
    if (field && code == SDLK_BACKSPACE) {
        field->backspace();
        error_.clear();
        return;
    }
    const NavAction action = navFromKey(code, field != nullptr);
    const bool repeatable = action == NavAction::Next || action == NavAction::Previous;
    if (!key.repeat || repeatable) apply(action);
}

void GameUi::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_QUIT) closeWindow();
    if (event.type == SDL_KEYDOWN) handleKey(event.key);
    if (event.type == SDL_TEXTINPUT) {
        if (TextField* field = activeField()) {
            field->insert(event.text.text);
            error_.clear();
        }
    }
}

void GameUi::updateMenus(const FlightControls& controls) {
    raisedArm_ = controls.raisedArm;
    armProgress_ = controls.menuProgress;
    const bool calibrating = flow_.current() == UiScreen::Calibration && poseSource_;
    const bool gestureAllowed = !calibrating || tracker_.acceptsGesture(controls);
    const NavAction action = navFromControls(controls);
    if (action == NavAction::None || !gestureAllowed) return;
    if (activeField() && !isBodyGesture(controls)) return;
    apply(action);
}

void GameUi::apply(NavAction action) {
    switch (action) {
        case NavAction::None: return;
        case NavAction::Next: cursor_.next(); sound_.play(UiSound::Move); break;
        case NavAction::Previous: cursor_.previous(); sound_.play(UiSound::Move); break;
        case NavAction::Confirm: sound_.play(UiSound::Confirm); activate(); break;
        case NavAction::Back:
            if (flow_.current() == UiScreen::MainMenu) return;
            sound_.play(UiSound::Back);
            flow_.back();
            break;
    }
    syncScreen();
}

void GameUi::activate() {
    const int index = cursor_.index();
    switch (flow_.current()) {
        case UiScreen::MainMenu: activateMain(index); break;
        case UiScreen::Calibration: activateCalibration(index); break;
        case UiScreen::Profile: activateProfile(index); break;
        case UiScreen::Rooms: activateRooms(index); break;
        case UiScreen::Pause: activatePause(index); break;
        default: break;
    }
}

void GameUi::activateMain(int index) {
    flow_.chooseMain(static_cast<MainOption>(index));
}

void GameUi::activateCalibration(int index) {
    if (index == 0) flow_.calibrationFinished();
    else flow_.back();
}

void GameUi::activateProfile(int index) {
    const auto& names = profiles_.names();
    const int count = static_cast<int>(names.size());
    if (index > count) {
        flow_.back();
        return;
    }
    const ProfileResult result = index < count ? profiles_.select(names[index], *gameplay_)
                                               : profiles_.create(profileName_.text(), *gameplay_);
    error_ = ProfilePicker::messageFor(result);
    if (result != ProfileResult::Ok) {
        profiles_.refresh();
        refreshRows();
        return;
    }
    profileName_.clear();
    flow_.profileChosen();
}

void GameUi::finishRoom(bool ready) {
    error_ = rooms_.error();
    if (ready) flow_.roomReady();
}

void GameUi::activateHostRow(int index) {
    if (index <= 1) finishRoom(rooms_.hostRoom(roomName_.text()));
    else flow_.back();
}

void GameUi::activateJoinRow(int index) {
    const int count = static_cast<int>(rooms_.rooms().size());
    if (index < count) {
        finishRoom(rooms_.joinRoom(static_cast<size_t>(index)));
    } else if (index == count) {
        finishRoom(rooms_.joinAddress(hostAddress_.text()));
    } else if (index == count + 1) {
        rooms_.refresh();
        refreshRows();
    } else {
        flow_.back();
    }
}

void GameUi::activateRooms(int index) {
    if (flow_.roomMode() == RoomMode::Host) activateHostRow(index);
    else activateJoinRow(index);
}

void GameUi::activatePause(int index) {
    if (index == 0) flow_.resume();
    else flow_.leaveToMenu();
}

// ---- Atualizacao por quadro ----

void GameUi::updateCalibration(float seconds) {
    if (!poseSource_) return;
    cv::Mat frame;
    Pose pose;
    if (poseSource_->latestPose(frame, pose)) {
        calibrationFrame_ = frame;
        calibrationPose_ = pose;
        calibrationSerial_++;
    }
    if (calibrationFrame_.empty()) return;
    const int before = tracker_.marksDone();
    tracker_.update(calibrationPose_, calibrationFrame_.size(), seconds);
    if (tracker_.marksDone() > before) sound_.play(UiSound::Mark);
}

// Depois de mudar, o saldo fica visivel por alguns segundos e some aos poucos.
void GameUi::updateCoins(float seconds) {
    const int coins = gameplay_->view().coins;
    coinsHold_ = coins != lastCoins_ ? kCoinsVisibleSeconds : std::max(0.0f, coinsHold_ - seconds);
    lastCoins_ = coins;
}

float GameUi::coinsOpacity() const { return std::min(coinsHold_ / kCoinsFadeSeconds, 1.0f); }

void GameUi::update(float seconds) {
    clock_ += seconds;
    const bool inMatch = flow_.current() == UiScreen::Playing;
    for (const GameplayFeedback feedback : gameplay_->drainFeedback())
        if (inMatch) sound_.play(feedback);
    if (inMatch) banner_.update(gameplay_->view().recordBeatenNow, seconds);
    if (inMatch) updateCoins(seconds);
    if (flow_.current() == UiScreen::Calibration) updateCalibration(seconds);
}

// ---- Desenho ----

std::string GameUi::menuTitle() const {
    switch (flow_.current()) {
        case UiScreen::Profile: return "Perfil";
        case UiScreen::Rooms: return flow_.roomMode() == RoomMode::Host ? "Hospedar sala" : "Entrar em sala";
        case UiScreen::Pause: return "Pausa";
        default: return "Birds&Cashews";
    }
}

std::string GameUi::menuSubtitle() const {
    switch (flow_.current()) {
        case UiScreen::Profile: return "Escolha quem vai jogar ou digite um nome novo.";
        case UiScreen::Rooms: return flow_.roomMode() == RoomMode::Host ? "Crie uma sala na sua rede local."
                                                                       : "Escolha uma sala ou digite o IP de quem hospeda.";
        case UiScreen::Pause: return "Partida salva. Respire e volte a voar quando quiser.";
        default: return "Voe com o corpo, colete cajus e leve tudo para o ninho.";
    }
}

std::string GameUi::mainFooter() const {
    const std::string profile = profiles_.active().empty() ? "Nenhum perfil escolhido" : "Perfil: " + profiles_.active();
    return profile + "  ·  " + recordFooter_;
}

std::string GameUi::pauseFooter() const {
    const GameplayView& view = gameplay_->view();
    return "Pontuação " + std::to_string(view.totalDelivered) + "  ·  Record " + std::to_string(view.record);
}

MenuScene GameUi::menuScene() const {
    MenuScene scene;
    scene.title = menuTitle();
    scene.subtitle = menuSubtitle();
    scene.rows = rows();
    scene.selected = cursor_.index();
    scene.error = error_;
    if (flow_.current() == UiScreen::MainMenu) scene.footer = mainFooter();
    if (flow_.current() == UiScreen::Pause) scene.footer = pauseFooter();
    scene.hero = flow_.current() == UiScreen::MainMenu;
    scene.showPreview = showsCornerPreview();
    scene.caretOn = activeField() && static_cast<int>(clock_ * 2.0f) % 2 == 0;
    scene.gesture = {raisedArm_, armProgress_, true};
    return scene;
}

HudScene GameUi::hudScene() const {
    HudScene scene;
    scene.view = &gameplay_->view();
    scene.bannerOpacity = banner_.opacity();
    scene.bannerScale = banner_.scale();
    scene.bodyLost = poseSource_ && !poseSource_->bodyDetected();
    scene.showPreview = showsCornerPreview();
    scene.coinsOpacity = coinsOpacity();
    return scene;
}

void GameUi::redraw() {
    kit_.canvas.clear();
    const UiScreen screen = flow_.current();
    if (screen == UiScreen::Playing) drawHudScene(kit_, hudScene());
    if (screen == UiScreen::Calibration) {
        CalibrationScene scene;
        scene.hasCamera = poseSource_ != nullptr;
        scene.frame = calibrationFrame_;
        scene.pose = calibrationPose_;
        scene.tracker = &tracker_;
        scene.rows = calibrationRows();
        scene.selected = cursor_.index();
        scene.gesture = {raisedArm_, armProgress_, !poseSource_ || tracker_.complete()};
        drawCalibrationScene(kit_, scene);
    } else if (screen != UiScreen::Playing && screen != UiScreen::Closed) {
        drawMenuScene(kit_, menuScene());
    }
}

// Resume tudo que aparece na tela. Enquanto nada mudar, a camada pronta continua valendo.
std::string GameUi::frameKey() const {
    std::ostringstream key;
    const UiScreen screen = flow_.current();
    key << static_cast<int>(screen) << '|' << cursor_.index() << '|' << error_ << '|' << static_cast<int>(raisedArm_)
        << '|' << quantized(armProgress_, 24.0f);
    if (const TextField* field = activeField()) key << '|' << field->text() << '|' << (static_cast<int>(clock_ * 2.0f) % 2);
    for (const MenuRow& row : rows()) key << '|' << row.label << '=' << row.value;
    if (screen == UiScreen::Calibration) key << '|' << calibrationSerial_ << '|' << tracker_.marksDone() << tracker_.bodyVisibleNow();
    if (screen != UiScreen::Playing && screen != UiScreen::Pause) return key.str();

    const GameplayView& v = gameplay_->view();
    key << '|' << v.carried << ',' << v.capacity << ',' << v.coins << ',' << v.totalDelivered << ',' << v.record << ','
        << v.recordHolder << ',' << v.leadingRecord << ',' << v.perchedOnNest << ',' << v.cargoLostNow << ',' << v.nearBoundary << ',' << quantized(coinsOpacity(), 20.0f) << ',' << std::lround(v.nestDistance) << ','
        << quantized(v.nestBearing, 60.0f) << ',' << quantized(banner_.opacity(), 30.0f) << ','
        << quantized(banner_.scale(), 60.0f) << ',' << (poseSource_ && !poseSource_->bodyDetected()) << ','
        << static_cast<int>(v.cargoLossCause) << ',' << v.knockedRivalNow << ',' << v.alarmNow << ',' << v.whaleNow << ','
        << quantized(v.thiefBearing, 60.0f) << ',' << std::lround(v.thiefDistance);
    const RobView& rob = v.robbery;
    key << '|' << rob.active << ',' << quantized(rob.progress, 60.0f) << ',' << static_cast<int>(rob.blocked) << ',' << rob.stolenNow;
    const NestMenuView& menu = v.menu;
    key << '|' << static_cast<int>(menu.page) << ',' << menu.selected << ',' << menu.leftLabel << ',' << menu.rightLabel << ','
        << static_cast<int>(menu.raisedArm) << ',' << quantized(menu.holdProgress, 24.0f);
    for (const UpgradeOptionView& o : menu.options)
        key << '|' << o.name << ',' << o.level << ',' << o.maxLevel << ',' << o.price << ',' << o.affordable;
    return key.str();
}

bool GameUi::compose(float seconds) {
    sinceCompose_ += seconds;
    if (sinceCompose_ < kMinComposeSeconds) return false;
    std::string key = frameKey();
    if (key == lastKey_) return false;
    lastKey_ = std::move(key);
    sinceCompose_ = 0;
    redraw();
    return true;
}

void GameUi::forceCompose() {
    lastKey_ = frameKey();
    sinceCompose_ = 0;
    redraw();
}

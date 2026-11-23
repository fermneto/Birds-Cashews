#include "core/Game.h"
#include "core/BirdShots.h"
#include "game/CashewModel.h"
#include "game/NameTags.h"
#include "game/NestModel.h"
#include "shared/BirdFactory.h"
#include "input/BodyInput.h"
#include "input/KeyboardInput.h"
#include "shared/LocalAuthority.h"
#include "ui/CalibratedBodyInput.h"
#include "ui/FakeNetSession.h"
#include "ui/UiShowcase.h"
#include "ui/UiAssets.h"
#include "vision/Clock.h"
#include "vision/VisionOverlay.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace {
constexpr float kFixedStep = 1.0f / 120.0f;
constexpr float kTelemetryInterval = 0.1f;
const cv::Size kPreviewSize(320, 240);
constexpr PlayerId kLocalPlayer = 0;
constexpr float kWorldSaveInterval = 30.0f;   // Salva o mundo de tempos em tempos, caso o jogo feche de repente.
const char* const kDefaultProfile = "Jogador";

// Jogo local com um jogador parado a mais, para fotografar o ninho de um rival sem abrir a rede.
class GhostRivalAuthority final : public LocalAuthority {
public:
    explicit GhostRivalAuthority(const PlayerPublicState& rival) : rival_(rival) {}
    std::vector<PlayerPublicState> players() const override {
        std::vector<PlayerPublicState> list = LocalAuthority::players();
        list.push_back(rival_);
        return list;
    }
    std::string playerName(PlayerId id) const override { return id == rival_.id ? "Rival de teste" : ""; }

private:
    PlayerPublicState rival_;
};

std::filesystem::path saveDirFor(const GameOptions& options) {
    if (!options.saveDir.empty()) return options.saveDir;
    char* base = SDL_GetBasePath();
    const std::filesystem::path path = std::filesystem::path(base ? base : "./") / "saves";
    SDL_free(base);
    return path;
}
}

Game::Game(const GameOptions& options)
    : options_(options), window_("Birds&Cashews", 1280, 720, options.hidden), world_(options.seed),
      renderer_(world_.buildMesh(), world_.buildCloudMesh(), world_.spawnPoint()), cashews_(world_, options.seed),
      match_(options.seed, options.port),
      session_(std::make_unique<GameplaySession>(world_, options.seed, cashews_, player_, saveDirFor(options), kLocalPlayer)),
      npcs_(world_, options.seed) {
    match_.setCashewCount(cashews_.size());
    // O host confere se o ladrao esta mesmo no ninho roubado. chooseNestSite e caro: cada lugar e calculado uma vez.
    auto perches = std::make_shared<std::array<std::optional<glm::vec3>, kMaxPlayers>>();
    match_.setNestPerch([this, perches](PlayerId owner) -> std::optional<glm::vec3> {
        if (owner >= kMaxPlayers) return std::nullopt;
        auto& perch = (*perches)[owner];
        if (!perch) perch = chooseNestSite(world_, options_.seed, owner).perch;
        return perch;
    });
    renderer_.setCashewMeshes(buildCashewMesh(), buildCashewFarMesh(), buildCashewStems(cashews_));
    renderer_.setNestBeaconMesh(buildNestBeaconMesh());
    renderer_.setRivalBeaconMesh(buildRivalBeaconMesh());
    player_.spawn(world_.spawnPoint(), 0.0f);
    window_.drawableSize(width_, height_);
    renderer_.resize(width_, height_);
}

// Sem webcam disponivel, o jogo continua no teclado.
void Game::createInput() {
    if (!options_.keyboard) {
        auto body = std::make_unique<CalibratedBodyInput>(options_.cameraIndex, options_.modelPath);
        if (body->start()) {
            poseSource_ = body.get();
            input_ = std::move(body);
            return;
        }
        std::cerr << "Webcam indisponivel, usando o teclado\n";
    }
    input_ = std::make_unique<KeyboardInput>();
}

void Game::createUi(const std::filesystem::path& savesDir, SoundManager::Output sound) {
    ui_ = std::make_unique<GameUi>(UiConfig{assetsDirectory(), savesDir, sound, &match_}, gameplay());
    ui_->attachPoseSource(poseSource_);
}

// Retorna false quando o jogador sai. O Esc agora pertence as telas.
bool Game::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (ui_) ui_->handleEvent(event);
        else if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11) window_.toggleFullscreen();
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            window_.drawableSize(width_, height_);
            renderer_.resize(width_, height_);
        }
    }
    return !ui_ || !ui_->closed();
}

// Todo evento confirmado passa pela sessao aqui, porque pollEvents entrega cada um uma unica vez.
void Game::updateCashews(float frameSeconds) { session_->update(frameSeconds); }

// Os NPCs so existem de verdade no host (ou jogando sozinho); o cliente so desenha o que recebe.
void Game::updateNpcs(float frameSeconds) {
    session_->publishLocalPlayer();
    if (match_.mode() == net::MatchMode::Client) return;
    GameAuthority& authority = session_->authority();
    npcs_.advance(frameSeconds, authority.players(), cashews_, authority);
    if (net::HostAuthority* host = match_.hostAuthority()) host->setNpcs(npcs_.publicStates());
}

// A rede roda a todo quadro, ate nos menus, para a conexao nao cair por falta de resposta.
// A voz so funciona em sala: jogando sozinho o microfone fica sem destino.
void Game::startVoice() {
    voice_ = std::make_unique<VoiceChat>();
    if (!voice_->start()) std::cerr << "Voz indisponivel: sem microfone ou saida de audio\n";
}

void Game::updateVoice(float frameSeconds) {
    if (!voice_) return;
    VoiceLink* link = match_.voice();
    if (link != voiceLink_) {
        voiceLink_ = link;
        voice_->attach(link, link ? match_.localPlayer() : kNoPlayer);
    }
    const FlightModel& flight = player_.flight();
    VoiceListener me;
    me.position = flight.position();
    me.velocity = flight.velocity();
    me.yaw = flight.yaw();
    std::vector<PlayerPublicState> others;
    if (const auto authority = match_.authority())
        for (const PlayerPublicState& player : authority->players())
            if (player.id != match_.localPlayer()) others.push_back(player);
    voice_->update(frameSeconds, me, others);
    // Falar abre o bico, mais ou menos conforme o volume da voz.
    const float beak = voice_->localSpeaking() ? std::min(1.0f, 0.45f + voice_->localLevel()) : 0.0f;
    player_.setBeakTarget(BeakSource::Voice, beak);
}

void Game::syncMatch(float frameSeconds) {
    if (const Profile* profile = session_->profile()) match_.setLocalIdentity(profile->name(), profile->birdSeed());
    match_.update(frameSeconds);
    if (match_.consumeChange()) rebuildSession();
}

// Entrar ou sair de uma sala troca a autoridade e o PlayerId, entao a partida e refeita com o mesmo perfil.
// O cliente parte com todos os cajus no lugar; o host manda logo em seguida os que ja foram coletados.
void Game::rebuildSession() {
    const std::string profile = session_->profile() ? session_->profile()->name() : "";
    session_->saveNow();
    saveWorld();
    cashews_ = CashewField(world_, options_.seed);
    std::unique_ptr<GameAuthority> authority;
    if (auto shared = match_.authority()) authority = std::make_unique<net::SharedAuthority>(std::move(shared));
    session_ = std::make_unique<GameplaySession>(world_, options_.seed, cashews_, player_, saveDirFor(options_),
                                                 match_.localPlayer(), std::move(authority));
    if (!profile.empty()) session_->startProfile(profile);
    restoreWorld();
    if (ui_) ui_->setGameplay(*session_);
    shownNestRevision_ = shownRivalRevision_ = -1;
    const char* const modes[] = {"sozinho", "hospedando a sala", "na sala como cliente"};
    std::cout << "Partida refeita: " << modes[static_cast<int>(match_.mode())] << ", jogador "
              << static_cast<int>(match_.localPlayer()) << "\n";
}

// So quem e dono do mundo carrega mundo.txt: o cliente recebe os cajus colhidos do host.
// Um arquivo de outra ilha (outra semente) e ignorado.
void Game::restoreWorld() {
    ownsWorld_ = match_.mode() != net::MatchMode::Client;
    worldSaveTimer_ = 0;
    if (!ownsWorld_) return;
    const WorldState state = WorldStore(saveDirFor(options_)).load(options_.seed);
    if (state.seed != options_.seed) return;
    worldElapsed_ = state.elapsedSeconds;
    cashews_.restoreUnavailable(state.unavailable);
    if (net::HostAuthority* host = match_.hostAuthority()) {
        std::vector<CashewId> ids;
        for (const UnavailableCashew& cashew : state.unavailable) ids.push_back(cashew.id);
        host->restoreCollected(ids);
    }
}

void Game::saveWorld() {
    if (!ownsWorld_) {
        worldSaveTimer_ = 0;
        return;
    }
    WorldState state;
    state.seed = options_.seed;
    state.elapsedSeconds = worldElapsed_ + worldSaveTimer_;
    state.unavailable = cashews_.unavailable();
    worldElapsed_ = state.elapsedSeconds;
    worldSaveTimer_ = 0;
    if (!WorldStore(saveDirFor(options_)).save(state)) std::cerr << "Nao foi possivel salvar o mundo\n";
}

// A fisica usa passo fixo para se comportar igual em qualquer taxa de quadros.
void Game::advance(float frameSeconds) {
    session_->applyControls(input_->poll());
    accumulator_ = std::min(accumulator_ + frameSeconds, 0.1f);
    while (accumulator_ >= kFixedStep) {
        session_->stepBird(kFixedStep);
        accumulator_ -= kFixedStep;
    }
    updateCashews(frameSeconds);
    updateNpcs(frameSeconds);
    player_.animate(frameSeconds);
    time_ += frameSeconds;
    telemetryTimer_ += frameSeconds;
}

void Game::refreshPreview() {
    cv::Mat image;
    if (input_->preview(image)) cv::resize(image, previewBase_, kPreviewSize, 0, 0, cv::INTER_AREA);
    if (previewBase_.empty() || telemetryTimer_ < kTelemetryInterval) return;
    telemetryTimer_ = 0;
    renderer_.setPreview(previewBase_);
}

// Fora da partida o mundo fica parado ao fundo e os comandos do jogador movem os menus.
void Game::updateScreens(float frameSeconds) {
    if (ui_->playing()) {
        advance(frameSeconds);
    } else {
        ui_->updateMenus(input_->poll());
        // Em sala o mundo nao para: eventos, cajus e NPCs seguem enquanto o jogador esta nos menus.
        if (match_.online()) {
            updateCashews(frameSeconds);
            updateNpcs(frameSeconds);
        }
        time_ += frameSeconds;
        telemetryTimer_ += frameSeconds;
    }
    ui_->update(frameSeconds);
    const FlightModel& flight = player_.flight();
    AmbientListener listener;
    listener.position = flight.position();
    listener.speed = glm::length(flight.velocity());
    listener.groundHeight = world_.island().heightAt(listener.position.x, listener.position.z);
    listener.playing = ui_->playing();
    ui_->setAmbience(ambientMixFor(listener));
}

void Game::presentUi(float frameSeconds) {
    renderer_.setPreviewVisible(ui_->showsCornerPreview());
    if (ui_->compose(frameSeconds)) renderer_.setOverlay(ui_->overlay());
}

Camera3D Game::buildCamera() const {
    const FlightModel& flight = player_.flight();
    const float shake = player_.animation().shake, kick = player_.animation().kick;
    Camera3D camera;
    camera.position = flight.position() + glm::vec3(0, kick * 0.2f, 0);
    camera.yaw = flight.yaw();
    camera.pitch = flight.pitch() + kick * 0.05f + shake * 0.04f * std::sin(time_ * 47.0f);
    camera.roll = flight.roll() + shake * 0.05f * std::sin(time_ * 61.0f);
    const float vibration = player_.animation().speedVibration;
    camera.pitch += vibration * 0.0020f * std::sin(time_ * 16.0f);
    camera.roll += vibration * 0.0030f * std::sin(time_ * 12.0f);
    camera.fovDegrees = player_.animation().fov;
    camera.aspect = static_cast<float>(width_) / static_cast<float>(std::max(height_, 1));
    return camera;
}

void Game::prepareCashews(const Camera3D& camera) {
    const glm::mat4 viewProjection = camera.viewProjection();
    renderer_.setCashewInstances(visibleCashews(cashews_, viewProjection, camera.position, 0.0f, kCashewDetailDistance),
                                 visibleCashews(cashews_, viewProjection, camera.position, kCashewDetailDistance, kCashewDrawDistance));
}

// Os ninhos so sao remontados quando o nivel de Tamanho ou a lista de jogadores muda.
void Game::prepareNest(const Camera3D& camera) {
    if (shownNestRevision_ != session_->nestRevision()) {
        shownNestRevision_ = session_->nestRevision();
        renderer_.setNestMesh(buildNestMesh(session_->nest(), session_->nestSizeLevel()));
    }
    renderer_.setNestBeacon(nestBeaconTransform(session_->nest(), session_->nestSizeLevel(), camera.position, time_));

    const RivalNests& rivals = session_->rivalNests();
    if (shownRivalRevision_ != rivals.revision()) {
        shownRivalRevision_ = rivals.revision();
        renderer_.setRivalNestMesh(buildRivalNestsMesh(rivals.nests()));
    }
    std::vector<glm::mat4> beacons;
    for (const RivalNest& nest : rivals.nests())
        beacons.push_back(rivalBeaconTransform(nest.site, nest.sizeLevel, camera.position, time_));
    renderer_.setRivalBeacons(std::move(beacons));
}

// NPCs e outros jogadores sao passaros procedurais pela semente. No cliente os NPCs vem do host.
void Game::prepareBirds() {
    std::vector<BirdInstance> birds;
    birds.reserve(npcs_.birds().size() + kMaxPlayers);
    const auto addNpc = [&](const net::NpcPublicState& npc, size_t index) {
        BirdInstance bird{npc.seed, npc.position, npc.yaw, npc.pitch, npc.roll, kBirdScale, npc.mode};
        bird.key = static_cast<uint32_t>(index);
        bird.perched = npc.perched;
        bird.glideFlaps = npc.role < kNpcRoleCount ? npcGlideFlaps(static_cast<NpcRole>(npc.role)) : 0.4f;
        birds.push_back(bird);
    };
    if (const net::ClientAuthority* client = match_.clientAuthority()) {
        const auto npcs = client->npcs();
        for (size_t i = 0; i < npcs.size(); i++) addNpc(npcs[i], i);
    } else {
        for (size_t i = 0; i < npcs_.birds().size(); i++) addNpc(npcPublicState(npcs_.birds()[i]), i);
    }
    if (match_.online()) {
        for (const PlayerPublicState& other : session_->authority().players())
            if (other.id != match_.localPlayer() && other.id != kNoPlayer) {
                BirdInstance bird{other.birdSeed, other.position, other.yaw, other.pitch, other.roll, kBirdScale, other.mode};
                bird.key = 1000u + other.id;
                birds.push_back(bird);
            }
    }
    renderer_.setBirdInstances(std::move(birds));
}

// Nome em uma pilula escura e translucida, legivel sobre o ceu e sobre as copas.
const cv::Mat& Game::nameLabel(const std::string& name) {
    const auto found = labelImages_.find(name);
    if (found != labelImages_.end()) return found->second;
    if (!labelText_) labelText_ = std::make_unique<TextPainter>((assetsDirectory() / "font.ttf").string());
    constexpr int kSize = 18, kPadX = 12, kHeight = 30;
    const int width = std::min(labelText_->width(name, kSize), 260) + 2 * kPadX;
    Canvas canvas(width, kHeight);
    canvas.fill({0, 0, width, kHeight}, cv::Scalar(26, 32, 18), 0.62f, kHeight / 2);
    TextStyle style;
    style.size = kSize;
    style.align = TextAlign::Center;
    style.outline = false;
    labelText_->drawFit(canvas, name, {width / 2, 21}, width - 2 * kPadX, style);
    return labelImages_.emplace(name, canvas.pixels().clone()).first->second;
}

// O nome de cada jogador proximo aparece acima do passaro dele. Sozinho a lista so tem o proprio jogador.
void Game::prepareNameTags(const Camera3D& camera) {
    std::vector<WorldLabel> labels;
    GameAuthority& authority = session_->authority();
    const auto tags = visibleNameTags(camera.position, authority.players(), session_->localPlayer(),
                                      [&](PlayerId id) { return authority.playerName(id); });
    for (const NameTag& tag : tags) labels.push_back({tag.name, nameLabel(tag.name), tag.anchor, tag.opacity});
    renderer_.setLabels(std::move(labels));
}

void Game::renderFrame() {
    const Camera3D camera = buildCamera();
    prepareCashews(camera);
    prepareNest(camera);
    prepareBirds();
    prepareNameTags(camera);
    renderer_.render(camera, player_.animation(), time_);
    window_.swap();
}

cv::Mat Game::captureFrame(float time) {
    const Camera3D camera = buildCamera();
    prepareCashews(camera);
    prepareNest(camera);
    prepareBirds();
    prepareNameTags(camera);
    return renderer_.capture(camera, player_.animation(), time);
}

int Game::run() {
    createInput();
    createUi(saveDirFor(options_), SoundManager::Output::Device);
    startVoice();
    restoreWorld();
    double last = nowSeconds();
    while (handleEvents()) {
        const double now = nowSeconds();
        const float frameSeconds = static_cast<float>(now - last);
        last = now;
        worldSaveTimer_ += frameSeconds;
        if (worldSaveTimer_ >= kWorldSaveInterval) saveWorld();
        syncMatch(frameSeconds);
        updateScreens(frameSeconds);
        updateVoice(frameSeconds);
        if (ui_->closed()) break;
        refreshPreview();
        presentUi(frameSeconds);
        renderFrame();
    }
    session_->saveNow();
    saveWorld();
    if (voice_) voice_->attach(nullptr, kNoPlayer);
    match_.leave();
    return 0;
}

// Fotografa pontos de vista conhecidos e mede o custo de desenhar um quadro.
int Game::renderTest(const std::string& outputDir) {
    session_->startProfile(kDefaultProfile);
    struct Shot { const char* name; glm::vec3 position; float yaw, pitch, roll; FlightMode mode; };
    const CashewTree& tree = world_.trees().front();
    const glm::vec3 nearTree = tree.base() + glm::vec3(0, tree.height() * 0.5f, tree.reach() + 130.0f);
    const std::vector<Shot> shots = {
        {"1_inicio", world_.spawnPoint(), 0.0f, -0.12f, 0.0f, FlightMode::Glide},
        {"2_cajueiro", nearTree, 0.0f, 0.0f, 0.0f, FlightMode::Glide},
        {"3_curva", world_.spawnPoint() + glm::vec3(0, 60, 0), 0.5f, -0.2f, 0.7f, FlightMode::Glide},
        {"4_mergulho", world_.spawnPoint(), 0.1f, -0.75f, 0.0f, FlightMode::Dive},
        {"5_praia", glm::vec3(0, 28, 930), 0.0f, -0.08f, 0.0f, FlightMode::Glide},
        {"6_mar", glm::vec3(0, 40, 900), 3.14f, -0.05f, 0.0f, FlightMode::Glide},
        {"8_passaros", world_.spawnPoint(), 0.0f, 0.0f, 0.0f, FlightMode::Glide},
    };
    renderer_.resize(1280, 720);
    width_ = 1280;
    height_ = 720;
    for (const Shot& shot : shots) {
        player_.spawn(shot.position, shot.yaw);
        player_.flight().setAttitude(shot.pitch, shot.roll);
        FlightControls controls;
        controls.mode = shot.mode;
        player_.applyControls(controls);
        for (int i = 0; i < 40; i++) player_.animate(1.0f / 60.0f);
        cv::imwrite(outputDir + "/" + shot.name + ".png", captureFrame(3.0f));
    }
    captureDiveShot(outputDir);
    captureBirdShots(renderer_, world_, options_.seed, outputDir);
    player_.spawn(world_.spawnPoint(), 0.0f);
    measureFrames("Quadro 1280x720");
    captureCashewShots(outputDir);
    captureNestShots(outputDir);
    captureUiShots(outputDir);
    captureLiveNestShots(outputDir);
    captureRivalNestShots(outputDir);
    return 0;
}

// Fotografa todas as telas com dados de exemplo, em uma pasta de saves descartavel.
void Game::captureUiShots(const std::string& outputDir) {
    const std::filesystem::path saves = std::filesystem::path(outputDir) / "saves_render_test";
    std::filesystem::remove_all(saves);
    DemoGameplaySource demo;
    ScriptedPoseSource camera;
    FakeNetSession rooms;   // Salas de exemplo nas fotos, sem abrir a rede.
    GameUi ui(UiConfig{assetsDirectory(), saves, SoundManager::Output::Silent, &rooms}, demo);
    player_.spawn(world_.spawnPoint(), 0.0f);
    player_.flight().setAttitude(-0.12f, 0.0f);
    player_.applyControls(FlightControls{});
    cv::resize(camera.frame(), previewBase_, kPreviewSize, 0, 0, cv::INTER_AREA);
    renderer_.setPreview(previewBase_);

    runUiShowcase(ui, demo, camera, [&](const std::string& name) {
        ui.forceCompose();
        renderer_.setOverlay(ui.overlay());
        renderer_.setPreviewVisible(ui.showsCornerPreview());
        cv::imwrite(outputDir + "/" + name + ".png", captureFrame(3.0f));
    });

    sendKey(ui, SDLK_ESCAPE);
    ui.forceCompose();
    renderer_.setOverlay(ui.overlay());
    measureFrames("Quadro com HUD pronto");
    measureHudFrames(ui, demo);
    std::filesystem::remove_all(saves);
}

// Confere o painel do ninho com a sessao real da partida, e nao com os dados de exemplo.
void Game::captureLiveNestShots(const std::string& outputDir) {
    GameUi ui(UiConfig{assetsDirectory(), saveDirFor(options_), SoundManager::Output::Silent}, *session_);
    for (int i = 0; i < 3; i++) sendKey(ui, SDLK_RETURN);   // Jogar, primeiro perfil e seguir sem webcam.
    const auto shoot = [&](const char* name) {
        ui.update(1.0f / 60.0f);
        ui.forceCompose();
        renderer_.setOverlay(ui.overlay());
        cv::imwrite(outputDir + "/" + name, captureFrame(3.0f));
    };
    player_.spawn(session_->nest().perch + glm::vec3(0, 6, 0), session_->nest().yaw);
    for (int i = 0; i < 60; i++) updateCashews(1.0f / 60.0f);
    shoot("ui_18_sessao_ninho.png");

    FlightControls press;
    press.menuPressed = ArmSide::Left;
    session_->applyControls(press);
    updateCashews(1.0f / 60.0f);
    shoot("ui_19_sessao_comer.png");
}

// Pior caso: os dados mudam a todo quadro e a camada inteira e redesenhada e reenviada.
void Game::measureHudFrames(GameUi& ui, DemoGameplaySource& demo) {
    std::vector<double> samples;
    GameplayView view = DemoGameplaySource::nestEatSample();
    for (int i = 0; i < 90; i++) {
        view.nestDistance = 100.0f + static_cast<float>(i);
        view.nestBearing = i * 0.1f;
        demo.setView(view);
        const double before = nowSeconds();
        ui.update(1.0f / 60.0f);
        if (ui.compose(1.0f)) renderer_.setOverlay(ui.overlay());
        captureFrame(i / 60.0f);
        samples.push_back((nowSeconds() - before) * 1000.0);
    }
    std::sort(samples.begin(), samples.end());
    std::cout << std::fixed << std::setprecision(1) << "Quadro redesenhando o HUD: mediana " << samples[45] << " ms, p95 "
              << samples[85] << " ms\n";
}

void Game::measureFrames(const std::string& label) {
    std::vector<double> samples;
    for (int i = 0; i < 90; i++) {
        const double before = nowSeconds();
        captureFrame(i / 60.0f);
        samples.push_back((nowSeconds() - before) * 1000.0);
    }
    std::sort(samples.begin(), samples.end());
    std::cout << std::fixed << std::setprecision(1) << label << ": mediana " << samples[45] << " ms, p95 "
              << samples[85] << " ms\n";
}

void Game::aimAt(glm::vec3 eye, glm::vec3 target) {
    const glm::vec3 view = target - eye;
    player_.spawn(eye, std::atan2(view.x, -view.z));
    player_.flight().setAttitude(std::atan2(view.y, glm::length(glm::vec2(view.x, view.z))), 0.0f);
    player_.applyControls(FlightControls{});
    player_.setBeakTarget(BeakSource::Cashew, session_->collector().beakTarget(eye));
    for (int i = 0; i < 40; i++) player_.animate(1.0f / 60.0f);
}

// Fotografa um caju de varias distancias e confere a coleta e o indicador de carga.
void Game::captureCashewShots(const std::string& outputDir) {
    const CashewSpot& spot = cashews_.spots().front();
    const glm::vec3 base = world_.trees()[spot.tree].base();
    const glm::vec3 outward = glm::normalize(glm::vec3(spot.position.x - base.x, 0, spot.position.z - base.z));
    const auto eyeAt = [&](float distance, float rise) { return spot.position + outward * distance + glm::vec3(0, rise, 0); };

    aimAt(eyeAt(240.0f, 110.0f), spot.position);
    cv::imwrite(outputDir + "/8_cajus_longe.png", captureFrame(3.0f));
    measureFrames("Quadro olhando os cajus");
    aimAt(eyeAt(70.0f, 8.0f), spot.position);
    cv::imwrite(outputDir + "/9_cajus_medio.png", captureFrame(3.0f));
    aimAt(eyeAt(15.0f, 2.0f), spot.position);
    cv::imwrite(outputDir + "/10_caju_perto.png", captureFrame(3.0f));

    player_.flight().position() = spot.position;
    updateCashews(0.0f);
    updateCashews(0.0f);
    previewBase_ = cv::Mat(kPreviewSize, CV_8UC3, cv::Scalar(45, 35, 30));
    renderer_.setPreview(previewBase_);
    aimAt(eyeAt(15.0f, 2.0f), spot.position);
    cv::imwrite(outputDir + "/11_caju_coletado.png", captureFrame(3.0f));
    std::cout << "Carga depois da coleta: " << session_->collector().carried() << "/" << session_->collector().stats().capacity << "\n";
}

// Fotografa o ninho de longe, de perto, pousado e no tamanho maximo.
void Game::captureNestShots(const std::string& outputDir) {
    const NestSite& nest = session_->nest();
    const glm::vec3 heading(std::sin(nest.yaw), 0, -std::cos(nest.yaw));
    const auto shoot = [&](const char* name, float distance, float rise) {
        aimAt(nest.perch + heading * distance + glm::vec3(0, rise, 0), nest.perch);
        cv::imwrite(outputDir + "/" + name, captureFrame(3.0f));
    };
    shoot("12_ninho_longe.png", 620.0f, 70.0f);
    shoot("13_ninho_medio.png", 170.0f, 45.0f);

    player_.spawn(nest.perch + glm::vec3(0, 6, 0), nest.yaw);
    for (int i = 0; i < 60; i++) {
        updateCashews(1.0f / 60.0f);
        player_.animate(1.0f / 60.0f);
    }
    cv::imwrite(outputDir + "/14_ninho_pousado.png", captureFrame(3.0f));
    std::cout << "Pousado no ninho: " << (session_->perched() ? "sim" : "nao") << "\n";

    ProfileStore(saveDirFor(options_)).save(Profile::restored("Ninho maximo", 1, 0, 0, {}, {0, 0, 0, kMaxUpgradeLevel}));
    session_->startProfile("Ninho maximo");
    shoot("15_ninho_maximo.png", 170.0f, 45.0f);
}

// Fotografa o proprio ninho ao lado do ninho de outro jogador, o rival mais proximo, com Tamanho 3.
void Game::captureRivalNestShots(const std::string& outputDir) {
    const NestSite own = chooseNestSite(world_, options_.seed, kLocalPlayer);
    PlayerPublicState rival;
    float closest = 1.0e9f;
    for (PlayerId id = 1; id < kMaxPlayers; id++) {
        const float distance = glm::length(chooseNestSite(world_, options_.seed, id).base - own.base);
        if (distance < closest) {
            closest = distance;
            rival.id = id;
        }
    }
    rival.position = world_.spawnPoint() + glm::vec3(0, 200, 0);
    rival.nestLevel = 3;
    session_ = std::make_unique<GameplaySession>(world_, options_.seed, cashews_, player_, saveDirFor(options_), kLocalPlayer,
                                                 std::make_unique<GhostRivalAuthority>(rival));
    session_->startProfile(kDefaultProfile);
    updateCashews(0.0f);
    shownNestRevision_ = shownRivalRevision_ = -1;
    const RivalNest* nest = session_->rivalNests().find(rival.id);
    if (!nest) {
        std::cout << "Ninho rival: nao apareceu\n";
        return;
    }
    renderer_.setOverlay(cv::Mat::zeros(height_, width_, CV_8UC4));   // Sem o HUD das fotos anteriores.
    renderer_.setPreviewVisible(false);
    std::cout << "Ninho rival: jogador " << static_cast<int>(rival.id) << " a " << std::lround(closest)
              << " m do proprio, Tamanho " << nest->sizeLevel << "\n";

    const glm::vec3 middle = (own.base + nest->site.base) * 0.5f, across = nest->site.base - own.base;
    const glm::vec3 side = glm::normalize(glm::vec3(-across.z, 0, across.x));
    aimAt(middle + side * std::max(closest * 1.1f, 300.0f) + glm::vec3(0, 130, 0), middle + glm::vec3(0, 20, 0));
    cv::imwrite(outputDir + "/16_ninho_rival_lado.png", captureFrame(3.0f));
    const glm::vec3 heading(std::sin(nest->site.yaw), 0, -std::cos(nest->site.yaw));
    aimAt(nest->site.perch + heading * 150.0f + glm::vec3(0, 40, 0), nest->site.perch);
    cv::imwrite(outputDir + "/17_ninho_rival_perto.png", captureFrame(3.0f));

    // O nome do rival aparece acima dele so quando esta perto.
    aimAt(rival.position + glm::vec3(0, 6, 60), rival.position + glm::vec3(0, 5, 0));
    cv::imwrite(outputDir + "/18_nome_do_jogador.png", captureFrame(3.0f));
    aimAt(rival.position + glm::vec3(0, 6, 200), rival.position);
    cv::imwrite(outputDir + "/19_nome_longe_some.png", captureFrame(3.0f));
}

// Simula um mergulho real para conferir o campo de visao em alta velocidade.
void Game::captureDiveShot(const std::string& outputDir) {
    player_.spawn(world_.spawnPoint() + glm::vec3(0, 240, 0), 0.0f);
    FlightControls controls;
    controls.mode = FlightMode::Dive;
    player_.applyControls(controls);
    for (int i = 0; i < 300; i++) {
        player_.step(world_, kFixedStep);
        player_.animate(kFixedStep);
    }
    cv::imwrite(outputDir + "/7_mergulho_rapido.png", captureFrame(3.0f));
    std::cout << "FOV no mergulho: " << player_.animation().fov << " graus\n";
}

// Roda jogo e visao juntos para medir se um atrapalha o outro.
int Game::soakTest(int seconds) {
    session_->startProfile(kDefaultProfile);
    auto body = std::make_unique<BodyInput>(options_.cameraIndex, options_.modelPath);
    if (!body->start()) {
        std::cerr << "Nao foi possivel abrir a webcam\n";
        return 1;
    }
    BodyInput* vision = body.get();
    input_ = std::move(body);
    std::vector<double> frames;
    const double start = nowSeconds();
    double last = start;
    while (nowSeconds() - start < seconds) {
        handleEvents();
        const double now = nowSeconds();
        advance(static_cast<float>(now - last));
        last = now;
        refreshPreview();
        captureFrame(time_);
        const double used = (nowSeconds() - now) * 1000.0;
        frames.push_back(used);
        if (used < 16.6) std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>((16.6 - used) * 1000)));
    }
    const VisionMetrics m = vision->metrics();
    std::sort(frames.begin(), frames.end());
    std::cout << std::fixed << std::setprecision(1)
              << "Jogo: " << frames.size() / static_cast<double>(seconds) << " FPS, quadro mediano "
              << frames[frames.size() / 2] << " ms, p95 " << frames[frames.size() * 95 / 100] << " ms\n"
              << "Visao: " << m.processed / m.seconds << " FPS, inferencia media " << m.inferenceMs / m.processed
              << " ms, latencia " << m.latencyMs / m.processed << " ms\n";
    return 0;
}

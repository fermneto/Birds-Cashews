#include "core/Game.h"
#include "game/CashewModel.h"
#include "input/BodyInput.h"
#include "input/KeyboardInput.h"
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
}

Game::Game(const GameOptions& options)
    : options_(options), window_("Birds&Cashews", 1280, 720, options.hidden), world_(options.seed),
      renderer_(world_.buildMesh(), world_.buildCloudMesh(), world_.spawnPoint()), cashews_(world_, options.seed),
      collector_(cashews_, authority_, kLocalPlayer) {
    renderer_.setCashewMeshes(buildCashewMesh(), buildCashewFarMesh(), buildCashewStems(cashews_));
    player_.spawn(world_.spawnPoint(), 0.0f);
    window_.drawableSize(width_, height_);
    renderer_.resize(width_, height_);
}

// Sem webcam disponivel, o jogo continua no teclado.
void Game::createInput() {
    if (!options_.keyboard) {
        auto body = std::make_unique<BodyInput>(options_.cameraIndex, options_.modelPath);
        if (body->start()) {
            input_ = std::move(body);
            return;
        }
        std::cerr << "Webcam indisponivel, usando o teclado\n";
    }
    input_ = std::make_unique<KeyboardInput>();
}

bool Game::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) return false;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11) window_.toggleFullscreen();
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            window_.drawableSize(width_, height_);
            renderer_.resize(width_, height_);
        }
    }
    return true;
}

// Todo evento confirmado passa por aqui, porque pollEvents entrega cada um uma unica vez.
void Game::updateCashews(float frameSeconds) {
    for (const GameEvent& event : authority_.pollEvents()) collector_.handle(event);
    const glm::vec3 position = player_.flight().position();
    collector_.update(frameSeconds, position);
    player_.setBeakTarget(BeakSource::Cashew, collector_.beakTarget(position));
}

// A fisica usa passo fixo para se comportar igual em qualquer taxa de quadros.
void Game::advance(float frameSeconds) {
    player_.applyControls(input_->poll());
    accumulator_ = std::min(accumulator_ + frameSeconds, 0.1f);
    while (accumulator_ >= kFixedStep) {
        player_.step(world_, kFixedStep);
        accumulator_ -= kFixedStep;
    }
    updateCashews(frameSeconds);
    player_.animate(frameSeconds);
    time_ += frameSeconds;
    telemetryTimer_ += frameSeconds;
}

void Game::drawTelemetry(cv::Mat& image) const {
    const FlightModel& flight = player_.flight();
    const float ground = std::max(world_.island().heightAt(flight.position().x, flight.position().z), 0.0f);
    const std::string text = "ALT " + std::to_string(static_cast<int>(flight.position().y - ground)) + " m   VEL "
                           + std::to_string(static_cast<int>(flight.speed() * 3.6f)) + " km/h";
    drawOutlinedText(image, text, {10, image.rows - 12}, 0.5, cv::Scalar(255, 255, 255));
    const std::string cargo = "CAJUS " + std::to_string(collector_.carried()) + "/" + std::to_string(collector_.stats().capacity);
    drawOutlinedText(image, cargo, {10, image.rows - 36}, 0.6, cv::Scalar(90, 210, 255));
    if (player_.nearBoundary()) drawOutlinedText(image, "VOLTE PARA A ILHA", {10, 28}, 0.7, cv::Scalar(70, 90, 255));
}

void Game::refreshPreview() {
    cv::Mat image;
    if (input_->preview(image)) cv::resize(image, previewBase_, kPreviewSize, 0, 0, cv::INTER_AREA);
    if (previewBase_.empty() || telemetryTimer_ < kTelemetryInterval) return;
    telemetryTimer_ = 0;
    cv::Mat composed = previewBase_.clone();
    drawTelemetry(composed);
    renderer_.setPreview(composed);
}

Camera3D Game::buildCamera() const {
    const FlightModel& flight = player_.flight();
    const float shake = player_.animation().shake, kick = player_.animation().kick;
    Camera3D camera;
    camera.position = flight.position() + glm::vec3(0, kick * 0.2f, 0);
    camera.yaw = flight.yaw();
    camera.pitch = flight.pitch() + kick * 0.05f + shake * 0.04f * std::sin(time_ * 47.0f);
    camera.roll = flight.roll() + shake * 0.05f * std::sin(time_ * 61.0f);
    camera.fovDegrees = player_.animation().fov;
    camera.aspect = static_cast<float>(width_) / static_cast<float>(std::max(height_, 1));
    return camera;
}

void Game::prepareCashews(const Camera3D& camera) {
    const glm::mat4 viewProjection = camera.viewProjection();
    renderer_.setCashewInstances(visibleCashews(cashews_, viewProjection, camera.position, 0.0f, kCashewDetailDistance),
                                 visibleCashews(cashews_, viewProjection, camera.position, kCashewDetailDistance, kCashewDrawDistance));
}

void Game::renderFrame() {
    const Camera3D camera = buildCamera();
    prepareCashews(camera);
    renderer_.render(camera, player_.animation(), time_);
    window_.swap();
}

cv::Mat Game::captureFrame(float time) {
    const Camera3D camera = buildCamera();
    prepareCashews(camera);
    return renderer_.capture(camera, player_.animation(), time);
}

int Game::run() {
    createInput();
    double last = nowSeconds();
    while (handleEvents()) {
        const double now = nowSeconds();
        advance(static_cast<float>(now - last));
        last = now;
        refreshPreview();
        renderFrame();
    }
    return 0;
}

// Fotografa pontos de vista conhecidos e mede o custo de desenhar um quadro.
int Game::renderTest(const std::string& outputDir) {
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
    player_.spawn(world_.spawnPoint(), 0.0f);
    measureFrames("Quadro 1280x720");
    captureCashewShots(outputDir);
    return 0;
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
    player_.setBeakTarget(BeakSource::Cashew, collector_.beakTarget(eye));
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

    collector_.update(0.0f, spot.position);
    for (const GameEvent& event : authority_.pollEvents()) collector_.handle(event);
    previewBase_ = cv::Mat(kPreviewSize, CV_8UC3, cv::Scalar(45, 35, 30));
    cv::Mat composed = previewBase_.clone();
    drawTelemetry(composed);
    renderer_.setPreview(composed);
    aimAt(eyeAt(15.0f, 2.0f), spot.position);
    cv::imwrite(outputDir + "/11_caju_coletado.png", captureFrame(3.0f));
    std::cout << "Carga depois da coleta: " << collector_.carried() << "/" << collector_.stats().capacity << "\n";
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

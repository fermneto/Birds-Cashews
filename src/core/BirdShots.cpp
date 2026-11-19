#include "core/BirdShots.h"
#include "ai/NpcEcosystem.h"
#include "game/CashewField.h"
#include "shared/BirdFactory.h"
#include "shared/LocalAuthority.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "vision/Clock.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace {
constexpr float kFrame = 1.0f / 60.0f;
constexpr uint32_t kPreySeed = 777;
constexpr uint32_t kPreyKey = 1000;

Camera3D lookFrom(glm::vec3 eye, glm::vec3 target) {
    const glm::vec3 view = target - eye;
    Camera3D camera;
    camera.position = eye;
    camera.yaw = std::atan2(view.x, -view.z);
    camera.pitch = std::atan2(view.y, glm::length(glm::vec2(view.x, view.z)));
    return camera;
}

std::vector<BirdInstance> instancesOf(const NpcEcosystem& ecosystem) {
    std::vector<BirdInstance> birds;
    for (size_t i = 0; i < ecosystem.birds().size(); i++) {
        const NpcBird& npc = ecosystem.birds()[i];
        BirdInstance bird{npc.seed, npc.position, npc.yaw, npc.pitch, npc.roll, kBirdScale, npc.mode};
        bird.key = static_cast<uint32_t>(i);
        bird.perched = npc.perched;
        bird.glideFlaps = npcGlideFlaps(npc.role);
        birds.push_back(bird);
    }
    return birds;
}

const NpcBird& firstOf(const NpcEcosystem& ecosystem, NpcRole role, int slot = -1) {
    for (const NpcBird& bird : ecosystem.birds())
        if (bird.role == role && (slot < 0 || bird.slot == slot)) return bird;
    return ecosystem.birds().front();
}

// Grade de passaros parados no ceu, todos no mesmo planeio, para comparar formas e cores.
void gallery(Renderer& renderer, glm::vec3 center, const std::vector<uint32_t>& seeds, const std::string& path,
             int view) {
    constexpr int kColumns = 6;
    const int rows = static_cast<int>((seeds.size() + kColumns - 1) / kColumns);
    std::vector<BirdInstance> birds;
    for (size_t i = 0; i < seeds.size(); i++) {
        const float c = static_cast<float>(i % kColumns) - (kColumns - 1) * 0.5f;
        const float r = static_cast<float>(i / kColumns) - (rows - 1) * 0.5f;
        BirdInstance bird{seeds[i], center, 0, 0, 0, kBirdScale, FlightMode::Glide};
        bird.key = 5000u + static_cast<uint32_t>(view * 100 + i);   // Chave propria: nada herdado do jogo.
        if (view == 0) {           // Perfil, de lado.
            bird.position += glm::vec3(c * 12.5f, -r * 10.0f, 0);
            bird.yaw = 1.5708f;
        } else if (view == 1) {    // De cima, asas abertas.
            bird.position += glm::vec3(c * 26.0f, 0, r * 23.0f);
        } else {                   // De baixo, contra o ceu.
            bird.position += glm::vec3(c * 26.0f, 0, -r * 23.0f);
            bird.yaw = 3.14159f;
        }
        birds.push_back(bird);
    }
    renderer.setBirdInstances(birds);
    Camera3D camera;
    if (view == 0) camera = lookFrom(center + glm::vec3(0, -10, 34), center + glm::vec3(0, 2, 0));
    else if (view == 1) camera = lookFrom(center + glm::vec3(0, 60, 9), center);
    else camera = lookFrom(center + glm::vec3(0, -60, 9), center);
    cv::imwrite(path, renderer.capture(camera, BirdAnimation{}, 3.0f));
}

// Persegue o cacador com a camera durante um ataque e monta uma tira de 6 quadros.
void hunterStrip(Renderer& renderer, const World& world, uint32_t seed, const std::string& outputDir) {
    NpcEcosystem ecosystem(world, seed);
    CashewField field(world, seed);
    LocalAuthority authority;
    const NpcBird& hunter = firstOf(ecosystem, NpcRole::Hunter);
    PlayerPublicState prey;
    prey.id = 0;
    prey.carried = 3;
    prey.birdSeed = kPreySeed;
    prey.position = hunter.position + glm::vec3(90, -35, -60);
    prey.velocity = glm::vec3(-6, 0, -18);
    std::vector<cv::Mat> frames;
    int dropFrame = -1, firstClose = -1;
    for (int frame = 0; frame < 600 && frames.size() < 6; frame++) {
        prey.position += prey.velocity * kFrame;
        prey.yaw = std::atan2(prey.velocity.x, -prey.velocity.z);
        ecosystem.advance(kFrame, {prey}, field, authority);
        for (const auto& event : authority.pollEvents())
            if (event.type == GameEventType::CashewDropped && dropFrame < 0) {
                dropFrame = frame;
                prey.carried = 0;
            }
        auto birds = instancesOf(ecosystem);
        BirdInstance player{kPreySeed, prey.position, prey.yaw, 0, 0, kBirdScale, FlightMode::Flap};
        player.key = kPreyKey;
        birds.push_back(player);
        renderer.setBirdInstances(birds);
        const glm::vec3 forward = glm::normalize(hunter.velocity + glm::vec3(0.001f, 0, 0));
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        const Camera3D camera = lookFrom(hunter.position - forward * 17.0f + right * 9.0f + glm::vec3(0, 5, 0),
                                         hunter.position * 0.7f + prey.position * 0.3f);
        const float time = static_cast<float>(frame) * kFrame;
        if (firstClose < 0 && glm::length(prey.position - hunter.position) < 45.0f) firstClose = frame;
        cv::Mat image = renderer.capture(camera, BirdAnimation{}, time);
        if (firstClose >= 0 && (frame - firstClose) % 22 == 0) {
            cv::Mat small;
            cv::resize(image, small, cv::Size(640, 360), 0, 0, cv::INTER_AREA);
            frames.push_back(small);
        }
    }
    while (frames.size() < 6) frames.push_back(cv::Mat(360, 640, CV_8UC3, cv::Scalar(0, 0, 0)));
    cv::Mat top, bottom, strip;
    cv::hconcat(std::vector<cv::Mat>(frames.begin(), frames.begin() + 3), top);
    cv::hconcat(std::vector<cv::Mat>(frames.begin() + 3, frames.end()), bottom);
    cv::vconcat(top, bottom, strip);
    cv::imwrite(outputDir + "/passaros_cacador_em_acao.png", strip);
    std::cout << "Cacador em acao: carga derrubada no quadro " << dropFrame << "\n";
}

// Mede o custo dos passaros no mesmo quadro: todos os NPCs mais 6 jogadores contra nenhum passaro.
void measureBirds(Renderer& renderer, const World& world, uint32_t seed) {
    NpcEcosystem ecosystem(world, seed);
    CashewField field(world, seed);
    LocalAuthority authority;
    std::vector<BirdInstance> crowd = instancesOf(ecosystem);
    for (uint32_t id = 0; id < 6; id++) {
        BirdInstance player{9000u + id, world.spawnPoint() + glm::vec3(id * 12.0f - 30.0f, -20, -80), 0, 0, 0, kBirdScale, FlightMode::Flap};
        player.key = 1000u + id;
        crowd.push_back(player);
    }
    const Camera3D camera = lookFrom(world.spawnPoint(), world.spawnPoint() + glm::vec3(0, -40, -150));
    const auto median = [&](const std::vector<BirdInstance>& birds) {
        renderer.setBirdInstances(birds);
        std::vector<double> samples;
        for (int i = 0; i < 60; i++) {
            const double before = nowSeconds();
            renderer.capture(camera, BirdAnimation{}, i * kFrame);
            samples.push_back((nowSeconds() - before) * 1000.0);
        }
        std::sort(samples.begin(), samples.end());
        return samples[30];
    };
    const double empty = median({}), full = median(crowd);
    double simulation = 0;
    for (int i = 0; i < 300; i++) {
        const double before = nowSeconds();
        ecosystem.advance(1.0f / 30.0f, {}, field, authority);
        simulation += (nowSeconds() - before) * 1000.0;
    }
    std::cout << std::fixed << std::setprecision(2) << "Passaros: quadro sem passaros " << empty << " ms, com "
              << crowd.size() << " passaros " << full << " ms; passo da IA dos " << ecosystem.birds().size()
              << " NPCs " << simulation / 300.0 << " ms\n";
}
}

void captureBirdShots(Renderer& renderer, const World& world, uint32_t seed, const std::string& outputDir) {
    renderer.setViewModelVisible(false);
    renderer.setPreviewVisible(false);
    NpcEcosystem ecosystem(world, seed);
    std::vector<uint32_t> seeds;
    for (const NpcBird& bird : ecosystem.birds()) seeds.push_back(bird.seed);
    seeds.push_back(1u);           // Dois passaros de jogador, pelo birdSeed.
    seeds.push_back(424242u);
    const glm::vec3 sky = world.spawnPoint() + glm::vec3(0, 320, 0);
    gallery(renderer, sky, seeds, outputDir + "/passaros_galeria_perfil.png", 0);
    gallery(renderer, sky, seeds, outputDir + "/passaros_galeria_cima.png", 1);
    gallery(renderer, sky, seeds, outputDir + "/passaros_galeria_baixo.png", 2);

    hunterStrip(renderer, world, seed, outputDir);
    measureBirds(renderer, world, seed);

    // Assustados pousados no alto das copas.
    renderer.setBirdInstances(instancesOf(ecosystem));
    const NpcBird& perched = firstOf(ecosystem, NpcRole::Skittish);
    glm::vec3 eye = perched.position + glm::vec3(9, 5, 9);
    world.resolve(eye, 3.0f);
    cv::imwrite(outputDir + "/passaros_pousado.png",
                renderer.capture(lookFrom(eye, perched.position), BirdAnimation{}, 3.0f));

    // Bando em V e planador depois de alguns segundos de voo.
    CashewField field(world, seed);
    LocalAuthority authority;
    for (int frame = 0; frame < 240; frame++) {
        ecosystem.advance(kFrame, {}, field, authority);
        authority.pollEvents();
        renderer.setBirdInstances(instancesOf(ecosystem));
        const NpcBird& leader = firstOf(ecosystem, NpcRole::Migrant, 0);
        const glm::vec3 forward = glm::normalize(glm::vec3(leader.velocity.x, 0, leader.velocity.z));
        const glm::vec3 right(-forward.z, 0, forward.x);
        const Camera3D camera = lookFrom(leader.position - forward * 58.0f + right * 6.0f + glm::vec3(0, 34, 0),
                                         leader.position - forward * 14.0f);
        const cv::Mat image = renderer.capture(camera, BirdAnimation{}, frame * kFrame);
        if (frame == 239) cv::imwrite(outputDir + "/passaros_bando_v.png", image);
    }
    const NpcBird& glider = firstOf(ecosystem, NpcRole::Glider);
    cv::imwrite(outputDir + "/passaros_planador.png",
                renderer.capture(lookFrom(glider.position + glm::vec3(18, 7, 18), glider.position), BirdAnimation{}, 4.0f));
    renderer.setViewModelVisible(true);
    renderer.setPreviewVisible(true);
}

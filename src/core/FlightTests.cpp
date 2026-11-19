#include "core/FlightTests.h"
#include "core/TestRun.h"
#include "entity/PlayerBird.h"
#include <cmath>
#include <functional>

namespace {
constexpr float kStep = 1.0f / 120.0f;
constexpr uint32_t kSeed = 20261207;

FlightControls mode(FlightMode m, float bank = 0) {
    FlightControls controls;
    controls.mode = m;
    controls.bank = bank;
    return controls;
}

// Sobre o mar, longe das arvores, para isolar a fisica.
PlayerBird birdAt(glm::vec3 position, float yaw = 0) {
    PlayerBird bird;
    bird.spawn(position, yaw);
    return bird;
}

void fly(PlayerBird& bird, const World& world, const FlightControls& controls, float seconds,
         const std::function<void()>& each = {}) {
    bird.applyControls(controls);
    FlightControls hold = controls;
    hold.flaps = 0;
    bird.applyControls(hold);
    for (float t = 0; t < seconds; t += kStep) {
        bird.step(world, kStep);
        if (each) each();
    }
}

void flapFor(PlayerBird& bird, const World& world, int flaps, float strength) {
    FlightControls controls = mode(FlightMode::Flap);
    controls.flaps = 1;
    controls.flapStrength = strength;
    for (int i = 0; i < flaps; i++) {
        bird.applyControls(controls);
        FlightControls hold = mode(FlightMode::Flap);
        bird.applyControls(hold);
        for (float t = 0; t < 0.8f; t += kStep) bird.step(world, kStep);
    }
}

const glm::vec3 kOpenSky(0, 300, 1000);

void testFlightModes(TestRun& run, const World& world) {
    PlayerBird glide = birdAt(kOpenSky);
    fly(glide, world, mode(FlightMode::Glide), 5.0f);
    const float glideLoss = kOpenSky.y - glide.flight().position().y;
    run.check("planar perde altura devagar", glideLoss > 9.0f && glideLoss < 13.0f);

    PlayerBird flap = birdAt(kOpenSky);
    flapFor(flap, world, 10, 0.6f);
    run.check("bater os bracos ganha altura", flap.flight().position().y > kOpenSky.y + 25.0f);

    PlayerBird strong = birdAt(kOpenSky);
    flapFor(strong, world, 10, 1.0f);
    run.check("batida mais ampla sobe mais rapido", strong.flight().position().y > flap.flight().position().y);

    PlayerBird dive = birdAt(kOpenSky);
    fly(dive, world, mode(FlightMode::Dive), 3.0f);
    run.check("mergulho desce e acelera", dive.flight().verticalSpeed() < -20.0f && dive.flight().speed() > 32.0f);

    float bestClimb = -100;
    fly(dive, world, mode(FlightMode::Glide), 3.0f, [&] { bestClimb = std::max(bestClimb, dive.flight().verticalSpeed()); });
    run.check("sair do mergulho converte velocidade em subida", bestClimb > -1.0f);
}

void testFlapResponse(TestRun& run, const World& world) {
    PlayerBird falling = birdAt(kOpenSky);
    fly(falling, world, mode(FlightMode::Dive), 2.0f);
    FlightControls once = mode(FlightMode::Flap);
    once.flaps = 1;
    once.flapStrength = 0.55f;
    falling.applyControls(once);
    run.check("uma batida fraca interrompe a queda na hora", falling.flight().verticalSpeed() > 5.0f);

    auto cameraBird = birdAt(kOpenSky);
    fly(cameraBird, world, mode(FlightMode::Dive), 1.0f);
    once.bodyControlled = true;
    once.stroke = 1;
    const float startingHeight = cameraBird.flight().position().y;
    cameraBird.applyControls(once);
    run.check("evento de batida isolado nao aplica impulso na camera", cameraBird.flight().verticalSpeed() < 0);
    for (int beat = 0; beat < 4; ++beat) fly(cameraBird, world, once, 0.8f);
    run.check("batidas pela camera sustentam subida sem bracos erguidos",
              cameraBird.flight().position().y > startingHeight + 15);

    for (float period : {0.6f, 0.8f, 1.2f}) {
        PlayerBird bird = birdAt({0, 120, 1000});   // Longe do teto de voo.
        FlightControls flap = mode(FlightMode::Flap);
        flap.flaps = 1;
        flap.flapStrength = 0.6f;
        float lowestClimb = 100, lowestPitch = 100;
        for (int beat = 0; beat < 10; beat++) {
            bird.applyControls(flap);
            bird.applyControls(mode(FlightMode::Flap));
            for (float t = 0; t < period; t += kStep) {
                bird.step(world, kStep);
                lowestClimb = std::min(lowestClimb, bird.flight().verticalSpeed());
                if (beat > 0) lowestPitch = std::min(lowestPitch, bird.flight().pitch());
            }
        }
        const std::string name = "batendo a cada " + std::to_string(static_cast<int>(period * 1000)) + " ms";
        run.check(name + " o passaro nunca cai", lowestClimb > 2.0f);
        run.check(name + " o nariz fica para cima", lowestPitch > 0.05f);
    }

    PlayerBird flapping = birdAt(kOpenSky);
    flapFor(flapping, world, 8, 0.7f);
    PlayerBird gliding = birdAt(kOpenSky);
    fly(gliding, world, mode(FlightMode::Glide), 6.4f);
    run.check("bater as asas voa mais rapido que planar", flapping.flight().speed() > gliding.flight().speed() + 4.0f);
}

float settledFov(PlayerBird& bird, const World& world, const FlightControls& controls, float seconds) {
    bird.applyControls(controls);
    for (float t = 0; t < seconds; t += kStep) {
        bird.step(world, kStep);
        bird.animate(kStep);
    }
    return bird.animation().fov;
}

void testFieldOfView(TestRun& run, const World& world) {
    PlayerBird glide = birdAt(kOpenSky);
    const float glideFov = settledFov(glide, world, mode(FlightMode::Glide), 4.0f);
    PlayerBird flap = birdAt(kOpenSky);
    const float flapFov = settledFov(flap, world, mode(FlightMode::Flap), 4.0f);
    PlayerBird dive = birdAt({0, 460, 1000});
    const float diveFov = settledFov(dive, world, mode(FlightMode::Dive), 4.0f);
    run.check("o FOV abre ao ganhar velocidade", flapFov > glideFov + 4.0f);
    run.check("o mergulho abre bem o FOV", diveFov > flapFov + 12.0f && diveFov > 95.0f);
    const float recovered = settledFov(dive, world, mode(FlightMode::Glide), 8.0f);
    run.check("o FOV volta ao normal depois do mergulho", recovered < glideFov + 5.0f);
}

void testTurning(TestRun& run, const World& world) {
    PlayerBird right = birdAt(kOpenSky);
    fly(right, world, mode(FlightMode::Glide, 0.7f), 1.0f);
    run.check("inclinar para a direita vira para a direita", right.flight().heading().x > 0.3f);
    run.check("a camera rola para o lado da curva", right.flight().roll() > 0.3f);

    PlayerBird left = birdAt(kOpenSky);
    fly(left, world, mode(FlightMode::Glide, -0.7f), 1.0f);
    run.check("inclinar para a esquerda vira para a esquerda", left.flight().heading().x < -0.3f);

    PlayerBird lost = birdAt(kOpenSky);
    fly(lost, world, mode(FlightMode::Lost, 1.0f), 1.0f);
    run.check("sem jogador o passaro segue reto", std::abs(lost.flight().heading().x) < 0.001f);
}

void testCollisions(TestRun& run, const World& world) {
    const CashewTree& tree = world.trees().front();
    const glm::vec3 start = tree.base() + glm::vec3(0, tree.height() * 0.2f, 120.0f);
    PlayerBird bird = birdAt(start);
    float closest = 1e9f;
    fly(bird, world, mode(FlightMode::Flap), 12.0f, [&] {
        const glm::vec3 p = bird.flight().position();
        closest = std::min(closest, glm::length(glm::vec2(p.x - tree.base().x, p.z - tree.base().z)));
    });
    run.check("o passaro nao atravessa o tronco", closest > tree.trunkRadius() * 0.55f);

    PlayerBird sea = birdAt({0, 12, 1040});
    float lowest = 1e9f;
    fly(sea, world, mode(FlightMode::Dive), 10.0f, [&] { lowest = std::min(lowest, sea.flight().position().y); });
    run.check("o mar devolve o passaro para o ar", lowest >= PlayerBird::kRadius - 0.01f);

    PlayerBird land = birdAt(world.spawnPoint());
    bool underground = false;
    fly(land, world, mode(FlightMode::Dive), 25.0f, [&] {
        const glm::vec3 p = land.flight().position();
        underground = underground || p.y < world.island().heightAt(p.x, p.z);
    });
    run.check("o passaro nao entra no chao", !underground);
}

void testGroundAndLimits(TestRun& run, const World& world) {
    const float x = 300, z = -100;
    const auto normal = world.island().normalAt(x, z);
    PlayerBird bird = birdAt({x, world.island().heightAt(x, z) + 3.0f, z},
                             std::atan2(-normal.x, normal.z));
    fly(bird, world, mode(FlightMode::Glide), 6.0f);
    run.check("o passaro pousa e para no chao", bird.flight().grounded() && bird.flight().speed() < 1.0f);
    const float groundY = bird.flight().position().y;
    flapFor(bird, world, 3, 0.8f);
    run.check("bater os bracos decola do chao", bird.flight().position().y > groundY + 3.0f);

    PlayerBird away = birdAt({0, 200, 1160});
    away.flight().place({0, 200, 1160}, 3.14159f, 24.0f);
    fly(away, world, mode(FlightMode::Flap), 40.0f);
    const glm::vec3 p = away.flight().position();
    run.check("o vento traz o passaro de volta para a ilha", glm::length(glm::vec2(p.x, p.z)) < 1250.0f);

    PlayerBird high = birdAt({0, 460, 1000});
    flapFor(high, world, 40, 1.0f);
    run.check("existe um teto de voo", high.flight().position().y <= FlightModel::kHardCeiling);
}

void testWorldGeneration(TestRun& run, const World& world) {
    const World same(kSeed), other(kSeed + 1);
    bool identical = same.trees().size() == world.trees().size();
    for (size_t i = 0; identical && i < world.trees().size(); i++)
        identical = same.trees()[i].base() == world.trees()[i].base() && same.trees()[i].height() == world.trees()[i].height();
    run.check("a mesma semente gera o mesmo mundo", identical && same.island().heightAt(123, 45) == world.island().heightAt(123, 45));
    run.check("sementes diferentes geram mundos diferentes", other.island().heightAt(123, 45) != world.island().heightAt(123, 45));
    run.check("a ilha tem cajueiros suficientes", world.trees().size() >= 22);

    bool onLand = true, spaced = true;
    for (size_t i = 0; i < world.trees().size(); i++) {
        const glm::vec3 base = world.trees()[i].base();
        onLand = onLand && base.y > 0;
        for (size_t j = i + 1; j < world.trees().size(); j++) {
            const glm::vec3 d = world.trees()[j].base() - base;
            spaced = spaced && glm::length(glm::vec2(d.x, d.z)) >= 190.0f;
        }
    }
    run.check("os cajueiros ficam em terra firme", onLand);
    run.check("os cajueiros mantem distancia entre si", spaced);
    run.check("o centro e terra e a borda e mar", world.island().heightAt(0, 0) > 0 && world.island().heightAt(1090, 0) < 0);

    const glm::vec3 spawn = world.spawnPoint();
    glm::vec3 probe = spawn;
    run.check("o ponto inicial esta livre", !world.resolve(probe, PlayerBird::kRadius).hit());
}
}

int runFlightSelfTest() {
    TestRun run;
    const World world(kSeed);
    testFlightModes(run, world);
    FlightModel glider;
    glider.place({0, 250, 0}, 0, FlightModel::kCruiseSpeed);
    FlightControls raised;
    raised.bodyControlled = true;
    raised.pullUp = 1;
    for (int i = 0; i < 120; ++i) glider.step(raised, kStep);
    run.check("erguer os bracos troca velocidade por altitude", glider.position().y > 250 && glider.speed() < FlightModel::kCruiseSpeed);
    run.check("puxada completa sobe mais do que avanca", glider.verticalSpeed() > glider.speed());
    FlightModel partial;
    partial.place({0, 250, 0}, 0, FlightModel::kCruiseSpeed);
    raised.pullUp = 0.5f;
    for (int i = 0; i < 120; ++i) partial.step(raised, kStep);
    run.check("meia puxada conserva mais velocidade horizontal", partial.speed() > glider.speed()
              && partial.verticalSpeed() > 0 && partial.verticalSpeed() < glider.verticalSpeed());
    raised.pullUp = 0;
    const float beforeRelease = glider.verticalSpeed();
    glider.step(raised, kStep);
    run.check("soltar a puxada preserva a continuidade", std::abs(glider.verticalSpeed() - beforeRelease) < 1);
    testFlapResponse(run, world);
    testFieldOfView(run, world);
    testTurning(run, world);
    testCollisions(run, world);
    testGroundAndLimits(run, world);
    testWorldGeneration(run, world);
    return run.failures;
}

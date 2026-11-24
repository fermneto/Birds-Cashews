#include "events/SkyPlaneTests.h"
#include "core/TestRun.h"
#include "entity/PlayerBird.h"
#include "events/SkyPlane.h"
#include "events/SkyPlaneModel.h"
#include "events/SkyPlaneSound.h"
#include "game/CashewField.h"
#include "game/GameplaySession.h"
#include "net/ClientAuthority.h"
#include "net/HostAuthority.h"
#include "shared/LocalAuthority.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <thread>

namespace {
using namespace std::chrono_literals;
constexpr float kDt = 1.0f / 60.0f;
constexpr uint32_t kWorld = 20261207;
const glm::vec3 kHigh(0, kPlaneTriggerHeight + 20.0f, 0);

int countOf(const std::vector<GameEvent>& log, GameEventType type) {
    return static_cast<int>(std::count_if(log.begin(), log.end(), [&](const GameEvent& e) { return e.type == type; }));
}

const GameEvent* lastOf(const std::vector<GameEvent>& log, GameEventType type) {
    for (auto it = log.rbegin(); it != log.rend(); ++it)
        if (it->type == type) return &*it;
    return nullptr;
}

GameEvent collect(PlayerId player, CashewId cashew) {
    GameEvent event;
    event.type = GameEventType::CashewCollected;
    event.actor = player;
    event.cashew = cashew;
    event.value = 1;
    return event;
}

PlayerPublicState stateOf(PlayerId id, const PlayerBird& bird, uint8_t carried) {
    PlayerPublicState state;
    state.id = id;
    state.position = bird.flight().position();
    state.velocity = bird.flight().velocity();
    state.yaw = bird.flight().yaw();
    state.carried = carried;
    return state;
}

// Um jogador com a sessao reduzida ao que o aviao usa: autoridade, passaro e o evento do teto.
// "hover" mantem o passaro no lugar, voando devagar em frente, ate a batida.
struct Pilot {
    Pilot(GameAuthority& authority, PlayerId id) : authority(authority), id(id), planes(id) {}
    void frame(float dt, uint8_t carried) {
        if (planes.stunned()) hover = false;   // Depois da batida a fisica assume, ate o proximo hoverOver.
        if (hover && !planes.stunned()) {
            hoverAt += bird.flight().heading() * 8.0f * dt;
            bird.flight().place(hoverAt, bird.flight().yaw(), FlightModel::kCruiseSpeed);
        } else {
            bird.flight().step(planes.stunned() ? planes.stunControls() : FlightControls{}, dt);
        }
        authority.publishLocalPlayer(stateOf(id, bird, carried));
        for (const GameEvent& event : authority.pollEvents()) {
            log.push_back(event);
            planes.handle(event);
        }
        planes.update(dt, bird, authority.players(), authority, false);
    }
    void hoverOver(glm::vec3 where) {
        hover = true;
        hoverAt = where;
        bird.spawn(where, 0.0f);
    }

    GameAuthority& authority;
    PlayerId id;
    SkyPlaneEvent planes;
    PlayerBird bird;
    std::vector<GameEvent> log;
    bool hover = true;
    glm::vec3 hoverAt{0};
};

bool runUntil(const std::function<void()>& frame, const std::function<bool()>& done, float seconds) {
    for (float t = 0; t < seconds; t += kDt) {
        if (done()) return true;
        frame();
    }
    return done();
}

void testTriggerAndStrike(TestRun& t) {
    LocalAuthority authority;
    Pilot pilot(authority, 0);
    authority.request(collect(0, 10));
    authority.request(collect(0, 11));
    pilot.hoverOver(glm::vec3(0, 300, 0));
    const auto frame = [&] { pilot.frame(kDt, 2); };
    runUntil(frame, [] { return false; }, 2.0f);
    t.check("aviao: abaixo do teto nao aparece", countOf(pilot.log, GameEventType::PlaneLaunched) == 0);

    pilot.hoverOver(kHigh);
    runUntil(frame, [&] { return countOf(pilot.log, GameEventType::PlaneLaunched) > 0; }, 1.0f);
    const GameEvent* launch = lastOf(pilot.log, GameEventType::PlaneLaunched);
    const glm::vec3 toPlane = launch ? launch->position - kHigh : glm::vec3(0);
    const float distance = glm::length(glm::vec2(toPlane.x, toPlane.z));
    const float ahead = glm::dot(glm::normalize(glm::vec2(toPlane.x, toPlane.z)), glm::vec2(0, -1));   // Yaw 0 olha para -Z.
    t.check("aviao: no teto aparece longe, a frente e acima", launch && launch->target == 0 && std::abs(distance - kPlaneSpawnDistance) < 5.0f &&
                                                               ahead > std::cos(glm::radians(56.0f)) && toPlane.y > 0);
    t.check("aviao: um so por vez e com aviso de que vem", countOf(pilot.log, GameEventType::PlaneLaunched) == 1 && pilot.planes.incoming());

    float warned = 0;
    runUntil([&] { frame(); warned += kDt; }, [&] { return countOf(pilot.log, GameEventType::PlaneStruck) > 0; }, 30.0f);
    t.check("aviao: chega depois do aviso e bate (s)", countOf(pilot.log, GameEventType::PlaneStruck) == 1 && warned > 4.0f);
    std::cout << "       aviso antes da batida: " << warned << " s\n";
    const GameEvent* drop = lastOf(pilot.log, GameEventType::CashewDropped);
    t.check("aviao: a batida derruba toda a carga no mundo",
            drop && drop->actor == kSkyPlaneActor && drop->target == 0 && drop->value == 2 &&
                countOf(pilot.log, GameEventType::CashewSpilled) == 2);
    t.check("aviao: o passaro fica tonto e e jogado para baixo",
            pilot.planes.stunned() && pilot.bird.flight().verticalSpeed() < -40.0f && pilot.planes.hitRecently() &&
                !pilot.planes.incoming());
    const float before = pilot.bird.flight().position().y;
    runUntil(frame, [] { return false; }, 3.4f);
    t.check("aviao: perde altura de repente e volta a voar", before - pilot.bird.flight().position().y > 180.0f && !pilot.planes.stunned());
    t.check("aviao: depois da batida o aviao vai embora", !pilot.planes.planes().empty() &&
                                                         pilot.planes.planes().front().phase == SkyPlanePhase::Departing);

    // Intervalo: mesmo de volta ao teto, nada de outro aviao ate o fim do intervalo.
    pilot.hoverOver(kHigh);
    runUntil(frame, [] { return false; }, kPlaneCooldown - 4.5f);
    t.check("aviao: intervalo impede outro logo em seguida", countOf(pilot.log, GameEventType::PlaneLaunched) == 1);
    t.check("aviao: o aviao some depois de passar", pilot.planes.planes().empty());
    runUntil(frame, [&] { return countOf(pilot.log, GameEventType::PlaneLaunched) == 2; }, 5.0f);
    t.check("aviao: passado o intervalo vem outro", countOf(pilot.log, GameEventType::PlaneLaunched) == 2);

    // Quem desce abaixo de kPlaneFloor antes da chegada ve o aviao passar por cima.
    pilot.hoverOver(glm::vec3(0, kPlaneFloor - 30.0f, 0));
    runUntil(frame, [&] { return !pilot.planes.incoming(); }, 30.0f);
    runUntil(frame, [] { return false; }, 1.0f);
    t.check("aviao: quem desce a tempo escapa e o aviao passa",
            countOf(pilot.log, GameEventType::PlaneStruck) == 1 && !pilot.planes.incoming() && !pilot.planes.stunned() &&
                !pilot.planes.planes().empty() && pilot.planes.planes().front().phase == SkyPlanePhase::Departing);
}

// O aviao e sempre mais rapido: nem fugindo em linha reta no mesmo nivel o passaro escapa.
void testChaseAlwaysArrives(TestRun& t) {
    GameEvent launch;
    launch.type = GameEventType::PlaneLaunched;
    launch.value = 1;
    launch.target = 0;
    launch.position = kHigh + glm::vec3(0, kPlaneSpawnRise, 700);
    SkyPlane plane = SkyPlane::fromLaunch(launch);
    glm::vec3 bird = kHigh;
    float seconds = 0;
    bool arrived = false;
    while (seconds < kPlaneApproachLimit && !arrived) {
        bird += glm::vec3(0, 0, -62.0f) * kDt;   // Velocidade maxima com o upgrade inteiro.
        arrived = plane.chase(&bird, kDt);
        seconds += kDt;
    }
    t.check("aviao: alcanca ate o passaro mais rapido fugindo", arrived && glm::length(plane.position - bird) <= kPlaneHitRadius);
}

void testLedger(TestRun& t) {
    SkyPlaneLedger ledger;
    PlayerPublicState target;
    target.id = 2;
    target.position = kHigh;
    GameEvent request;
    request.type = GameEventType::PlaneLaunched;
    request.target = 2;
    GameEvent copy = request;
    t.check("aviao no host: sem estado do alvo recusa", !ledger.acceptLaunch(copy, 0, nullptr, true));
    PlayerPublicState low = target;
    low.position.y = 200;
    copy = request;
    t.check("aviao no host: alvo baixo recusa", !ledger.acceptLaunch(copy, 0, &low, true));
    GameEvent first = request;
    t.check("aviao no host: alvo no teto aceita", ledger.acceptLaunch(first, 0, &target, true) && first.value != 0 &&
                                                  first.actor == kSkyPlaneActor);
    copy = request;
    t.check("aviao no host: nao repete antes do intervalo", !ledger.acceptLaunch(copy, kPlaneRelaunchInterval - 1.0, &target, true));

    GameEvent strike;
    strike.type = GameEventType::PlaneStruck;
    strike.target = 2;
    strike.value = static_cast<uint8_t>(first.value + 1);
    t.check("aviao no host: batida de outro aviao recusa", !ledger.acceptStrike(strike, 5, true));
    strike.value = first.value;
    t.check("aviao no host: batida do aviao certo aceita", ledger.acceptStrike(strike, 5, true));
    t.check("aviao no host: a mesma batida nao vale duas vezes", !ledger.acceptStrike(strike, 6, true));

    GameEvent second = request;
    t.check("aviao no host: passado o intervalo aceita de novo", ledger.acceptLaunch(second, kPlaneRelaunchInterval + 1.0, &target, true));
    strike.value = second.value;
    t.check("aviao no host: batida tarde demais recusa",
            !ledger.acceptStrike(strike, kPlaneRelaunchInterval + 1.0 + kPlaneStrikeWindow + 1.0, true));
}

void testSoundAndModel(TestRun& t) {
    SkyPlane plane;
    plane.position = glm::vec3(0, 400, -600);
    plane.forward = glm::vec3(0, 0, 1);   // Vindo na direcao do ouvinte, na origem.
    const PlaneEngineMix coming = planeEngineFor(glm::vec3(0, 400, 0), glm::vec3(0), {plane});
    plane.forward = glm::vec3(0, 0, -1);
    const PlaneEngineMix going = planeEngineFor(glm::vec3(0, 400, 0), glm::vec3(0), {plane});
    plane.position.z = -100;
    const PlaneEngineMix near = planeEngineFor(glm::vec3(0, 400, 0), glm::vec3(0), {plane});
    plane.position.z = -3000;
    const PlaneEngineMix far = planeEngineFor(glm::vec3(0, 400, 0), glm::vec3(0), {plane});
    t.check("som do aviao: Doppler agudo chegando e grave indo", coming.rate > 1.2f && going.rate < 0.85f);
    t.check("som do aviao: alto perto, audivel longe e mudo muito longe", near.volume > 0.7f && coming.volume > 0.1f && far.volume == 0);
    const std::vector<int16_t> loop = synthesizePlaneEngineLoop(44100);
    const auto [low, high] = std::minmax_element(loop.begin(), loop.end());
    int seamJump = loop.empty() ? 99999 : std::abs(loop.front() - loop.back()), largestStep = 0;
    for (size_t k = 1; k < loop.size(); k++) largestStep = std::max(largestStep, std::abs(loop[k] - loop[k - 1]));
    t.check("som do aviao: motor em loop sem estalo na emenda", !loop.empty() && *high > 5000 && *low < -5000 && seamJump <= largestStep);
    t.check("aviao: modelo low-poly com corpo e helice", buildSkyPlaneMesh().vertices().size() > 300 &&
                                                        !buildSkyPlanePropeller().vertices().empty());
}

// A partida de verdade: aviso no HUD, tontura que ignora os comandos e o motivo da carga perdida.
void testSession(TestRun& t) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("birds-aviao-" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    {
        const World world(kWorld);
        CashewField field(world, kWorld);
        PlayerBird bird;
        GameplaySession session(world, kWorld, field, bird, dir, 0);
        session.startProfile("Teste");
        session.authority().request(collect(0, 10));
        bird.spawn(kHigh + glm::vec3(0, 15, 0), 0.0f);
        bool warned = false, ignoredFlap = false;
        FlightControls flap;
        flap.mode = FlightMode::Flap;
        flap.flaps = 1;
        flap.flapStrength = 1.0f;
        for (int frame = 0; frame < 60 * 14; frame++) {
            const bool stunned = session.skyPlane().stunned();
            session.applyControls(stunned ? flap : FlightControls{});
            session.stepBird(kDt * 0.5f);
            session.stepBird(kDt * 0.5f);
            if (stunned) ignoredFlap = ignoredFlap || bird.flight().verticalSpeed() < -20.0f;
            session.update(kDt);
            session.publishLocalPlayer();
            warned = warned || session.view().planeIncoming;
            if (session.view().cargoLostNow) break;
        }
        t.check("aviao na partida: o HUD avisa que o aviao vem", warned);
        t.check("aviao na partida: a carga cai e o HUD diz que foi o aviao",
                session.view().cargoLostNow && session.view().cargoLossCause == CargoLossCause::Plane && session.view().carried == 0);
        t.check("aviao na partida: tonto, bater as asas nao adianta", ignoredFlap && session.view().planeHitNow);
    }
    std::filesystem::remove_all(dir);
}

// ---- Rede: host e um cliente em loopback ----

uint16_t freePort() {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    uint16_t port = 0;
    if (fd >= 0 && bind(fd, reinterpret_cast<const sockaddr*>(&address), size) == 0 &&
        getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0) port = ntohs(address.sin_port);
    if (fd >= 0) close(fd);
    return port;
}

void testOverNetwork(TestRun& t) {
    const uint16_t port = freePort();
    net::UdpSession hostUdp(port), clientUdp(port);
    net::HostAuthority host(hostUdp, net::HostConfig{kWorld, 100, net::kProtocolVersion, {}});
    if (!hostUdp.host("Sala do aviao") || !clientUdp.startJoin("127.0.0.1:" + std::to_string(port))) {
        t.check("aviao em rede: sala abre", false);
        return;
    }
    for (int i = 0; i < 600 && clientUdp.joinStatus() == net::JoinStatus::Pending; i++) {
        host.update(kDt);
        clientUdp.poll();
        std::this_thread::sleep_for(1ms);
    }
    net::ClientAuthority client(clientUdp, net::ClientConfig{kWorld, "Piloto", 5});
    for (int i = 0; i < 600 && client.status() == net::ClientStatus::Connecting; i++) {
        host.update(kDt);
        client.update(kDt);
        std::this_thread::sleep_for(500us);
    }
    if (client.status() != net::ClientStatus::Ready) {
        t.check("aviao em rede: cliente entra", false);
        return;
    }
    const PlayerId me = client.localPlayer();
    Pilot onHost(host, 0), onClient(client, me);
    onHost.hoverOver(glm::vec3(300, 100, 0));
    onClient.hoverOver(glm::vec3(0, 200, 0));
    const auto frame = [&] {
        host.update(kDt);
        onHost.frame(kDt, 0);
        client.update(kDt);
        onClient.frame(kDt, 1);
        std::this_thread::sleep_for(300us);
    };
    client.request(collect(me, 10));

    // Pedidos fora da regra: aviao para outro jogador e aviao com o passaro baixo.
    GameEvent forged;
    forged.type = GameEventType::PlaneLaunched;
    forged.target = 0;
    client.request(forged);
    forged.target = me;
    runUntil(frame, [] { return false; }, 0.5f);
    client.request(forged);
    runUntil(frame, [] { return false; }, 0.5f);
    t.check("aviao em rede: host recusa aviao para outro jogador e com o passaro baixo",
            countOf(onHost.log, GameEventType::PlaneLaunched) == 0);

    onClient.hoverOver(kHigh);
    runUntil(frame, [&] { return countOf(onClient.log, GameEventType::PlaneLaunched) > 0 &&
                                 countOf(onHost.log, GameEventType::PlaneLaunched) > 0; }, 6.0f);
    const GameEvent* hostLaunch = lastOf(onHost.log, GameEventType::PlaneLaunched);
    const GameEvent* clientLaunch = lastOf(onClient.log, GameEventType::PlaneLaunched);
    // O log cresce depois: guarda o numero do aviao agora, porque o ponteiro deixa de valer.
    const uint8_t launchSerial = hostLaunch ? hostLaunch->value : 0;
    t.check("aviao em rede: o host confirma e o cliente recebe o mesmo aviao",
            hostLaunch && clientLaunch && hostLaunch->target == me && clientLaunch->value == hostLaunch->value &&
                glm::length(clientLaunch->position - hostLaunch->position) < 0.01f);

    float worst = 0;
    runUntil([&] {
        frame();
        if (!onHost.planes.planes().empty() && !onClient.planes.planes().empty() &&
            onClient.planes.planes().front().phase == SkyPlanePhase::Approach)
            worst = std::max(worst, glm::length(onHost.planes.planes().front().position - onClient.planes.planes().front().position));
    }, [&] { return countOf(onHost.log, GameEventType::PlaneStruck) > 0; }, 30.0f);
    std::cout << "       maior distancia entre o aviao do host e o do cliente: " << worst << " m\n";
    t.check("aviao em rede: os dois computadores veem o aviao no mesmo lugar", worst > 0 && worst < 15.0f);

    runUntil(frame, [&] { return countOf(onClient.log, GameEventType::CashewSpilled) > 0; }, 2.0f);
    const GameEvent* hostDrop = lastOf(onHost.log, GameEventType::CashewDropped);
    const GameEvent* clientDrop = lastOf(onClient.log, GameEventType::CashewDropped);
    t.check("aviao em rede: o dono decide a batida e o host confirma para todos",
            countOf(onHost.log, GameEventType::PlaneStruck) == 1 && countOf(onClient.log, GameEventType::PlaneStruck) == 1);
    t.check("aviao em rede: a carga do alvo cai para todos",
            hostDrop && clientDrop && hostDrop->target == me && hostDrop->actor == kSkyPlaneActor && hostDrop->value == 1 &&
                clientDrop->value == 1 && countOf(onHost.log, GameEventType::CashewSpilled) == 1);
    t.check("aviao em rede: o host ve a batida e o aviao indo embora",
            !onHost.planes.planes().empty() && onHost.planes.planes().front().struck &&
                onHost.planes.planes().front().phase == SkyPlanePhase::Departing && onHost.planes.takeImpacts().size() == 1);
    t.check("aviao em rede: so o alvo fica tonto", onClient.planes.stunned() && !onHost.planes.stunned());

    GameEvent again;
    again.type = GameEventType::PlaneStruck;
    again.target = me;
    again.value = launchSerial;
    client.request(again);
    runUntil(frame, [] { return false; }, 0.5f);
    t.check("aviao em rede: a mesma batida repetida e recusada", countOf(onHost.log, GameEventType::PlaneStruck) == 1);
}
}

int runSkyPlaneSelfTest() {
    TestRun test;
    testTriggerAndStrike(test);
    testChaseAlwaysArrives(test);
    testLedger(test);
    testSoundAndModel(test);
    testSession(test);
    testOverNetwork(test);
    return test.failures;
}

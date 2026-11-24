#include "events/WhaleTests.h"
#include "core/TestRun.h"
#include "events/BoundaryWhale.h"
#include "events/WhaleGate.h"
#include "game/GameplaySession.h"
#include "gfx/WhaleView.h"
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
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
constexpr uint32_t kSeed = 20261207;
constexpr float kFrame = 1.0f / 60.0f;
const glm::vec3 kFarAway(0, 400, 0);
constexpr float kPi = 3.14159265f;

float radiusOf(glm::vec3 p) { return glm::length(glm::vec2(p.x, p.z)); }

// Ponto a um raio e angulo do centro, olhando para fora do mapa.
glm::vec3 pointAt(float radius, float angle, float height) { return {radius * std::cos(angle), height, radius * std::sin(angle)}; }
float yawOutward(glm::vec3 p) { return whale::yawToCenter(p) + kPi; }

class TempDir {
public:
    TempDir() {
        std::error_code error;
        std::random_device random;
        path_ = fs::temp_directory_path() / ("birds-whale-test-" + std::to_string(::getpid()) + "-" + std::to_string(random()));
        fs::create_directories(path_, error);
    }
    ~TempDir() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

CashewField cashewsAt(const std::vector<glm::vec3>& positions) {
    std::vector<CashewSpot> spots;
    for (const glm::vec3& position : positions) {
        CashewSpot spot;
        spot.position = position;
        spots.push_back(spot);
    }
    return CashewField(spots);
}

// Partida sozinho com o mesmo caminho de chamadas do laco do jogo, ate a publicacao do estado.
struct Rig {
    Rig(const World& world, const fs::path& dir)
        : field(cashewsAt({kFarAway + glm::vec3(0, 0, -2), kFarAway + glm::vec3(1, 0, -2)})),
          session(world, kSeed, field, bird, dir) {
        bird.spawn(kFarAway, 0.0f);
        bird.flight().place(kFarAway, 0.0f, 0.0f);
        session.startProfile("Baleia");
    }
    void tick(const FlightControls& controls = FlightControls{}) {
        session.applyControls(controls);
        for (int i = 0; i < 2; i++) session.stepBird(kFrame / 2.0f);
        session.update(kFrame);
        session.publishLocalPlayer();
        bird.animate(kFrame);
        const auto drained = session.whale().drainCues();
        cues.insert(cues.end(), drained.begin(), drained.end());
    }
    void ticks(int frames) {
        for (int i = 0; i < frames; i++) tick();
    }
    bool tickUntil(const std::function<bool()>& done, int frames) {
        for (int i = 0; i < frames && !done(); i++) tick();
        return done();
    }
    void placeOut(glm::vec3 position) { bird.flight().place(position, yawOutward(position), FlightModel::kCruiseSpeed); }
    int count(WhaleCueKind kind) const {
        return static_cast<int>(std::count_if(cues.begin(), cues.end(), [&](const WhaleCue& c) { return c.kind == kind; }));
    }

    CashewField field;
    PlayerBird bird;
    GameplaySession session;
    std::vector<WhaleCue> cues;
};

bool isFree(const World& world, glm::vec3 point) {
    const glm::vec3 before = point;
    return !world.resolve(point, PlayerBird::kRadius).hit() && point == before;
}

// ---- Animacao: so depende da posicao do evento e do tempo ----

void testTimeline(TestRun& t) {
    const glm::vec3 bite(0, 30, 1160);
    const whale::Pose atBite = whale::poseAt(bite, whale::kBiteTime);
    const whale::HeldBird held = whale::heldAt(bite, bite + glm::vec3(0, 0, 4), 0.0f, whale::kBiteTime);
    t.check("baleia: na mordida a boca chega ao passaro",
            glm::length(atBite.holdPoint() - bite) < 6.0f && glm::length(held.position - atBite.holdPoint()) < 0.01f);
    t.check("baleia: salta de baixo da agua e some de novo no mar",
            whale::poseAt(bite, 0).mouth.y < -10.0f && whale::poseAt(bite, whale::kEndTime).mouth.y < -20.0f &&
                atBite.mouth.y > 20.0f);
    t.check("baleia: vem de fora do mapa e olha para a ilha",
            radiusOf(whale::poseAt(bite, 0).mouth) > radiusOf(bite) + 15.0f && atBite.forward.z < -0.2f);
    const auto jaw = [&](float age) { return whale::poseAt(bite, age).jaw; };
    float chewOpen = 0, chewClosed = 1;
    for (float age = whale::kFallEnd; age < whale::kSpitTime; age += 0.01f) {
        chewOpen = std::max(chewOpen, jaw(age));
        chewClosed = std::min(chewClosed, jaw(age));
    }
    t.check("baleia: abre a boca no salto, fecha na mordida, mastiga e abre para cuspir",
            jaw(0.9f) > 0.5f && jaw(1.2f) < 0.1f && chewOpen > 0.2f && chewClosed < 0.02f && jaw(3.2f) > 0.6f);

    // Sem saltos de um quadro para o outro, nem quando muda de fase.
    float worstMouth = 0, worstHeld = 0;
    const glm::vec3 start = bite + glm::vec3(3, 10, -5);
    for (float age = 0; age < whale::kEndTime; age += 1.0f / 240.0f) {
        worstMouth = std::max(worstMouth, glm::length(whale::poseAt(bite, age + 1.0f / 240.0f).mouth - whale::poseAt(bite, age).mouth));
        if (age + 1.0f / 240.0f < whale::kSpitTime)
            worstHeld = std::max(worstHeld, glm::length(whale::heldAt(bite, start, 0, age + 1.0f / 240.0f).position -
                                                        whale::heldAt(bite, start, 0, age).position));
    }
    t.check("baleia: a animacao e continua entre as fases", worstMouth < 1.5f && worstHeld < 1.5f);
    const glm::vec3 high(0, 380, 1160);
    t.check("baleia: passaro alto e puxado ate a boca, que nao salta acima do limite",
            whale::poseAt(high, whale::kBiteTime).mouth.y <= whale::kMaxLeap + 0.01f &&
                glm::length(whale::heldAt(high, high, 0, whale::kBiteTime).position - whale::poseAt(high, whale::kBiteTime).holdPoint()) < 0.01f);
}

void testModel(TestRun& t) {
    const MeshBuilder body = WhaleView::buildBody(), jaw = WhaleView::buildJaw();
    float longest = 0;
    for (const Vertex& v : body.vertices()) longest = std::max(longest, -v.position.z);
    bool marks = true;
    for (int i = 0; i < static_cast<int>(SeaMarkKind::Count); i++) marks = marks && !WhaleView::buildMark(static_cast<SeaMarkKind>(i)).vertices().empty();
    t.check("baleia: modelo low-poly com corpo, mandibula e marcas no mar",
            body.vertices().size() > 300 && jaw.vertices().size() > 60 && longest > 45.0f && marks);
    const glm::vec4 tip = WhaleView::jawTransform(0.6f) * glm::vec4(0, 0, 0, 1);
    t.check("baleia: a mandibula gira na dobradica e abre para baixo",
            tip.y < -6.0f && std::abs(glm::length(glm::vec3(tip) - glm::vec3(0, 0, WhaleView::kHingeZ)) - 13.0f) < 0.01f);
}

// ---- Regra da autoridade ----

GameEvent biteOf(PlayerId id, glm::vec3 position) {
    GameEvent event;
    event.type = GameEventType::WhaleBite;
    event.actor = event.target = id;
    event.position = position;
    return event;
}

void testGate(TestRun& t) {
    WhaleGate gate;
    const glm::vec3 out(0, 30, 1160), warning(0, 30, 1100), inside(0, 30, 900);
    t.check("baleia: a autoridade recusa quem nao passou do limite", !gate.accept(biteOf(2, warning), std::nullopt));
    GameEvent other = biteOf(2, out);
    other.actor = 3;
    t.check("baleia: ninguem pede a baleia para outro jogador", !gate.accept(other, std::nullopt));
    t.check("baleia: recusa quando o estado conhecido esta dentro da ilha", !gate.accept(biteOf(2, out), inside));
    t.check("baleia: aceita quem passou do limite", gate.accept(biteOf(2, out), out));
    t.check("baleia: uma mordida por saida", !gate.accept(biteOf(2, out), out));
    gate.observe(2, warning);
    t.check("baleia: na zona de aviso ainda nao rearma", !gate.armed(2));
    gate.observe(2, inside);
    t.check("baleia: rearma quando o passaro volta para a ilha", gate.armed(2) && gate.accept(biteOf(2, out), out));
    gate.forget(2);
    t.check("baleia: quem sai da sala volta armado", gate.armed(2));
}

// ---- Partida sozinho ----

void testSolo(TestRun& t, const World& world) {
    TempDir dir;
    Rig rig(world, dir.path());
    rig.ticks(3);
    t.check("baleia sozinho: o passaro comeca com 2 cajus", rig.session.view().carried == 2);

    for (int i = 0; i < 40; i++) {   // Parado na zona de aviso.
        rig.placeOut(glm::vec3(0, 30, 1100));
        rig.tick();
    }
    const BoundaryWhale& whale = rig.session.whale();
    const WhaleScene warned = whale.scene();
    const bool shadow = std::any_of(warned.marks.begin(), warned.marks.end(), [](const SeaMark& m) { return m.kind == SeaMarkKind::Shadow; });
    t.check("baleia sozinho: na zona de aviso a sombra ronda e o ronco avisa",
            rig.session.view().nearBoundary && whale.warning() > 0.2f && shadow && rig.count(WhaleCueKind::Rumble) > 0 &&
                warned.whales.empty() && !whale.busy());
    for (int i = 0; i < 90; i++) {
        rig.placeOut(glm::vec3(0, 30, 1125));
        rig.tick();
    }
    t.check("baleia sozinho: perto do limite ela espirra pelo espiraculo", rig.count(WhaleCueKind::Spout) > 0 && !whale.busy());

    rig.placeOut(glm::vec3(0, 30, 1160));
    t.check("baleia sozinho: passar do limite chama a baleia", rig.tickUntil([&] { return whale.holdsLocal(); }, 4));
    const GameplayView& view = rig.session.view();
    t.check("baleia sozinho: a carga cai no mar e o aviso mostra a baleia",
            view.carried == 0 && view.whaleNow && view.cargoLostNow && view.cargoLossCause == CargoLossCause::Whale && !view.nearBoundary &&
                rig.field.activeLoose().empty());
    t.check("baleia sozinho: todos veem a baleia saltando", whale.scene().whales.size() == 1);

    rig.tickUntil([&] { return whale.attacks().front().age >= whale::kBiteTime; }, 200);
    const whale::Pose bitten = whale::poseAt(whale.attacks().front().bite, whale::kBiteTime);
    t.check("baleia sozinho: na mordida o passaro esta entre as mandibulas",
            glm::length(rig.bird.flight().position() - bitten.holdPoint()) < 4.0f && rig.count(WhaleCueKind::Bite) == 1 &&
                rig.count(WhaleCueKind::Splash) == 1);
    rig.tickUntil([&] { return whale.attacks().front().age >= whale::kFallEnd + 0.05f; }, 200);
    rig.cues.clear();
    float shake = 0;
    bool stayed = true;
    while (whale.holdsLocal() && whale.attacks().front().age < whale::kSpitTime - 0.05f) {
        rig.tick(FlightControls{FlightMode::Flap, 0.0f, 3, 1.0f});   // Bater as asas nao solta da boca.
        shake = std::max(shake, rig.bird.animation().shake);
        const whale::Pose pose = whale::poseAt(whale.attacks().front().bite, whale.attacks().front().age);
        stayed = stayed && glm::length(rig.bird.flight().position() - pose.holdPoint()) < 1.0f;
    }
    t.check("baleia sozinho: o passaro fica preso na boca enquanto ela mastiga",
            stayed && rig.count(WhaleCueKind::Chew) >= 3 && shake > 0.4f);

    rig.tickUntil([&] { return !whale.holdsLocal(); }, 60);
    const FlightModel& flight = rig.bird.flight();
    const glm::vec3 toCenter = -whale::outward(flight.position());
    t.check("baleia sozinho: cospe o passaro rapido e voando para o centro da ilha",
            rig.count(WhaleCueKind::Spit) == 1 && flight.speed() > 60.0f && flight.verticalSpeed() > 0.0f &&
                glm::dot(flight.heading(), toCenter) > 0.99f && !rig.session.view().whaleNow);

    bool free = true, inward = true;
    for (int i = 0; i < 240; i++) {
        rig.tick();
        free = free && isFree(world, flight.position());
        if (i < 60) inward = inward && glm::dot(flight.velocity(), -whale::outward(flight.position())) > 0;
    }
    const glm::vec3 end = flight.position();
    t.check("baleia sozinho: depois de cuspido fica dentro do mapa, no ar e livre",
            radiusOf(end) < World::kBoundaryRadius - 50.0f && inward && free && end.y > std::max(world.island().heightAt(end.x, end.z), 0.0f) + 5.0f);
    t.check("baleia sozinho: o passaro volta a voar com os comandos", !rig.bird.flight().grounded() && !whale.holdsLocal());

    // Cooldown: logo depois, mesmo voltando para fora, a baleia nao ataca de novo.
    rig.tickUntil([&] { return !whale.busy(); }, 200);
    const float cooldown = whale.cooldown();
    rig.placeOut(glm::vec3(0, 30, 1170));
    rig.ticks(60);
    t.check("baleia sozinho: cooldown depois de cuspir", cooldown > 1.0f && !whale.busy() && rig.bird.nearBoundary());
    t.check("baleia sozinho: nunca passa da parede invisivel", radiusOf(rig.bird.flight().position()) <= World::kBoundaryRadius + 180.5f);

    rig.bird.flight().place(glm::vec3(0, 60, 900), 0.0f, FlightModel::kCruiseSpeed);
    rig.tickUntil([&] { return whale.cooldown() <= 0; }, static_cast<int>(whale::kCooldown / kFrame) + 10);
    rig.bird.flight().place(glm::vec3(0, 60, 900), 0.0f, FlightModel::kCruiseSpeed);
    rig.ticks(2);
    rig.placeOut(glm::vec3(0, 30, 1140));
    rig.ticks(2);
    rig.placeOut(glm::vec3(0, 30, 1165));
    t.check("baleia sozinho: passado o cooldown e de volta a ilha, ela ataca de novo", rig.tickUntil([&] { return whale.holdsLocal(); }, 4));
}

// De varios lados da ilha, alto ou baixo, o passaro cuspido nunca fica fora do mapa nem preso.
void testSpitEverywhere(TestRun& t, const World& world) {
    bool allInside = true, allFree = true, allTriggered = true;
    for (int i = 0; i < 8; i++) {
        TempDir dir;
        Rig rig(world, dir.path());
        const float angle = kPi * 2.0f * i / 8.0f + 0.2f;
        rig.placeOut(pointAt(i % 2 ? 1300.0f : 1155.0f, angle, i % 3 == 0 ? 300.0f : 4.0f + 10.0f * i));
        if (!rig.tickUntil([&] { return rig.session.whale().holdsLocal(); }, 4)) {
            allTriggered = false;
            continue;
        }
        rig.tickUntil([&] { return !rig.session.whale().holdsLocal(); }, 400);
        for (int f = 0; f < 300; f++) {
            rig.tick();
            allFree = allFree && isFree(world, rig.bird.flight().position());
        }
        allInside = allInside && radiusOf(rig.bird.flight().position()) < World::kBoundaryRadius - 50.0f;
    }
    t.check("baleia: de qualquer lado e altura ela ataca, cospe para dentro e o passaro fica livre", allTriggered && allInside && allFree);
}

// ---- Rede: host autoritativo e clientes animando a mesma baleia ----

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

// Um computador da sala: autoridade, o proprio passaro e a baleia que ele ve.
struct Peer {
    Peer(GameAuthority& authority, PlayerId id) : authority(authority), whale(id), id(id) {}
    void drain() {
        for (const GameEvent& event : authority.pollEvents()) {
            log.push_back(event);
            whale.handle(event, bird.flight());
        }
    }
    void publish() {
        PlayerPublicState state;
        state.id = id;
        state.position = bird.flight().position();
        state.carried = 2;
        authority.publishLocalPlayer(state);
    }
    int bites(PlayerId target) const {
        return static_cast<int>(std::count_if(log.begin(), log.end(), [&](const GameEvent& e) {
            return e.type == GameEventType::WhaleBite && e.target == target;
        }));
    }
    GameAuthority& authority;
    BoundaryWhale whale;
    PlayerBird bird;
    PlayerId id;
    std::vector<GameEvent> log;
};

void testNetwork(TestRun& t, const World& world) {
    using namespace net;
    const uint16_t port = freePort();
    UdpSession hostUdp(port);
    if (!hostUdp.host("Sala da baleia")) {
        t.check("baleia em rede: abre a sala", false);
        return;
    }
    HostAuthority host(hostUdp, HostConfig{kSeed, 100, kProtocolVersion, {}});
    std::vector<std::unique_ptr<UdpSession>> udps;
    std::vector<std::unique_ptr<ClientAuthority>> clients;
    const auto step = [&] {
        host.update(kFrame);
        for (auto& client : clients) client->update(kFrame);
        std::this_thread::sleep_for(300us);
    };
    for (const char* name : {"Presa", "Plateia"}) {
        udps.push_back(std::make_unique<UdpSession>(port));
        udps.back()->startJoin("127.0.0.1:" + std::to_string(port));
        for (int i = 0; i < 600 && udps.back()->joinStatus() == JoinStatus::Pending; i++) {
            step();
            udps.back()->poll();
        }
        clients.push_back(std::make_unique<ClientAuthority>(*udps.back(), ClientConfig{kSeed, name, 7}));
        for (int i = 0; i < 600 && clients.back()->status() == ClientStatus::Connecting; i++) step();
    }
    if (clients[0]->status() != ClientStatus::Ready || clients[1]->status() != ClientStatus::Ready) {
        t.check("baleia em rede: os clientes entram", false);
        return;
    }
    Peer hostPeer(host, 0), prey(*clients[0], clients[0]->localPlayer()), crowd(*clients[1], clients[1]->localPlayer());
    std::vector<Peer*> peers = {&hostPeer, &prey, &crowd};
    const auto frame = [&](bool preyTriggers) {
        step();
        for (Peer* peer : peers) {
            peer->drain();
            peer->whale.update(kFrame, peer->bird, world, peer->authority, peer == &prey && preyTriggers);
            peer->publish();
        }
    };
    const auto frames = [&](int count, bool preyTriggers = false) { for (int i = 0; i < count; i++) frame(preyTriggers); };

    // A presa leva 2 cajus confirmados pelo host.
    for (CashewId id : {CashewId(10), CashewId(11)}) {
        GameEvent collect;
        collect.type = GameEventType::CashewCollected;
        collect.actor = prey.id;
        collect.cashew = id;
        prey.authority.request(collect);
    }
    hostPeer.bird.flight().place({0, 80, 0}, 0, 0);
    crowd.bird.flight().place(pointAt(1000.0f, kPi * 0.5f, 40.0f), 0, 0);
    prey.bird.flight().place(pointAt(1000.0f, kPi * 0.5f + 0.05f, 40.0f), 0, 0);
    frames(30);

    // Pedido falso: a plateia pede a baleia para a presa, e para si mesma com o estado dentro da ilha.
    GameEvent forged = biteOf(prey.id, pointAt(1170.0f, kPi * 0.5f, 30.0f));
    forged.actor = crowd.id;
    crowd.authority.request(forged);
    crowd.authority.request(biteOf(crowd.id, pointAt(1170.0f, kPi * 0.5f, 30.0f)));
    frames(30);
    t.check("baleia em rede: o host recusa pedido para outro jogador ou de quem esta dentro da ilha",
            hostPeer.bites(prey.id) == 0 && hostPeer.bites(crowd.id) == 0);

    const glm::vec3 out = pointAt(1165.0f, kPi * 0.5f + 0.05f, 30.0f);
    prey.bird.flight().place(out, yawOutward(out), 0.0f);
    frames(6);   // O host recebe o estado novo da presa antes do pedido.
    for (int i = 0; i < 300 && !prey.whale.holdsLocal(); i++) {
        frame(true);
        if (prey.whale.busy()) break;
        prey.bird.flight().place(out, yawOutward(out), 0.0f);
    }
    const auto biteIn = [&](const Peer& peer) {
        return std::find_if(peer.log.begin(), peer.log.end(), [](const GameEvent& e) { return e.type == GameEventType::WhaleBite; });
    };
    frames(3);
    const bool everyone = hostPeer.bites(prey.id) == 1 && prey.bites(prey.id) == 1 && crowd.bites(prey.id) == 1;
    t.check("baleia em rede: o host confirma e todos recebem a mesma mordida", everyone);
    if (!everyone) return;
    const GameEvent seen = *biteIn(crowd), confirmed = *biteIn(hostPeer);
    t.check("baleia em rede: a carga cai no mar, sem cajus soltos no mundo",
            confirmed.value == 2 && seen.value == 2 && std::none_of(crowd.log.begin(), crowd.log.end(), [](const GameEvent& e) {
                return e.type == GameEventType::CashewSpilled;
            }));
    t.check("baleia em rede: a mordida leva alvo e posicao", seen.target == prey.id && glm::length(seen.position - out) < 0.01f);
    t.check("baleia em rede: o dono fica preso e todos veem a baleia",
            prey.whale.holdsLocal() && hostPeer.whale.scene().whales.size() == 1 && crowd.whale.scene().whales.size() == 1 &&
                !crowd.whale.holdsLocal() && !hostPeer.whale.busy());

    // Cada computador anima a mesma baleia: no mesmo tempo desde o evento, a mesma pose.
    const auto ageOf = [](const Peer& peer) { return peer.whale.attacks().empty() ? -1.0f : peer.whale.attacks().front().age; };
    for (int i = 0; i < 300 && ageOf(crowd) < whale::kBiteTime + 0.5f; i++) {
        frame(true);
        prey.publish();
    }
    const float age = ageOf(crowd);
    const whale::Pose there = whale::poseAt(seen.position, age), here = whale::poseAt(prey.whale.attacks().front().bite, age);
    whale::HeldBird drawn;
    const bool held = crowd.whale.heldPose(prey.id, drawn);
    const whale::HeldBird own = whale::heldAt(prey.whale.attacks().front().bite, prey.whale.attacks().front().start,
                                              prey.whale.attacks().front().startYaw, age);
    t.check("baleia em rede: a plateia ve a mesma pose da baleia e o passaro preso na boca",
            glm::length(there.mouth - here.mouth) < 0.01f && held && glm::length(drawn.position - own.position) < 0.01f &&
                std::abs(ageOf(prey) - age) < 0.2f);

    // Uma segunda mordida enquanto a primeira acontece e recusada.
    prey.authority.request(biteOf(prey.id, out));
    frames(10, true);
    t.check("baleia em rede: o host nao repete a mordida na mesma saida", hostPeer.bites(prey.id) == 1);

    // A presa e cuspida para dentro: o host ve o estado dela dentro da ilha e rearma.
    for (int i = 0; i < 600 && prey.whale.busy(); i++) {
        step();
        for (Peer* peer : peers) peer->drain();
        for (Peer* peer : peers) peer->whale.update(kFrame, peer->bird, world, peer->authority, false);
        prey.whale.stepBird(prey.bird);
        if (!prey.whale.holdsLocal()) prey.bird.step(world, kFrame);
        for (Peer* peer : peers) peer->publish();
    }
    const glm::vec3 landed = prey.bird.flight().position();
    t.check("baleia em rede: o dono termina dentro do mapa depois da cuspida", radiusOf(landed) < World::kBoundaryRadius);
    prey.bird.flight().place(pointAt(900.0f, kPi * 0.5f, 40.0f), 0, 0);
    frames(20);
    prey.bird.flight().place(out, yawOutward(out), 0.0f);
    frames(8);
    prey.authority.request(biteOf(prey.id, out));
    frames(20);
    t.check("baleia em rede: de volta a ilha, a proxima saida chama a baleia de novo", hostPeer.bites(prey.id) == 2 && crowd.bites(prey.id) == 2);
}
}

int runWhaleSelfTest() {
    TestRun test;
    const World world(kSeed);
    testTimeline(test);
    testModel(test);
    testGate(test);
    testSolo(test, world);
    testSpitEverywhere(test, world);
    testNetwork(test, world);
    return test.failures;
}

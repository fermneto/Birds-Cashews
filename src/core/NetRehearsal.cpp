#include "core/NetRehearsal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <thread>

namespace {
constexpr float kFrame = 1.0f / 60.0f;
constexpr float kPhysicsStep = 1.0f / 120.0f;
const char* const kRoomName = "Teste de rede";

const char* eventName(GameEventType type) {
    switch (type) {
        case GameEventType::CashewCollected: return "caju coletado";
        case GameEventType::CashewDelivered: return "cajus entregues";
        case GameEventType::CashewDropped: return "carga derrubada";
        case GameEventType::CashewRespawned: return "caju reapareceu";
        case GameEventType::NestRobbed: return "ninho roubado";
        case GameEventType::UpgradeBought: return "upgrade comprado";
        case GameEventType::PlayerJoined: return "jogador entrou";
        case GameEventType::PlayerLeft: return "jogador saiu";
    }
    return "?";
}

std::string actorName(PlayerId id) { return id == kNoPlayer ? "NPC" : "jogador " + std::to_string(id); }

void printEvent(const char* who, const GameEvent& event) {
    std::cout << who << ": " << eventName(event.type) << " (" << actorName(event.actor);
    if (event.cashew != kNoCashew) std::cout << ", caju " << event.cashew;
    if (event.type == GameEventType::CashewDelivered || event.type == GameEventType::CashewDropped)
        std::cout << ", quantidade " << static_cast<int>(event.value);
    std::cout << ")\n";
}

// Marca o ritmo de 60 quadros por segundo, como o jogo.
class Pacer {
public:
    float wait() {
        next_ += std::chrono::microseconds(16667);
        std::this_thread::sleep_until(next_);
        return kFrame;
    }

private:
    std::chrono::steady_clock::time_point next_ = std::chrono::steady_clock::now();
};
}

// Anota cada evento confirmado antes de entrega-lo a sessao, que o consome.
class RehearsalPeer::Recorder final : public GameAuthority {
public:
    Recorder(std::shared_ptr<GameAuthority> target, std::vector<GameEvent>& log) : target_(std::move(target)), log_(log) {}
    void request(const GameEvent& event) override { target_->request(event); }
    std::vector<GameEvent> pollEvents() override {
        std::vector<GameEvent> events = target_->pollEvents();
        log_.insert(log_.end(), events.begin(), events.end());
        return events;
    }
    std::vector<PlayerPublicState> players() const override { return target_->players(); }
    void publishLocalPlayer(const PlayerPublicState& state) override { target_->publishLocalPlayer(state); }

private:
    std::shared_ptr<GameAuthority> target_;
    std::vector<GameEvent>& log_;
};

RehearsalPeer::RehearsalPeer(const World& world, uint32_t seed, CashewField field, const std::filesystem::path& saveDir,
                             std::shared_ptr<GameAuthority> authority, PlayerId localPlayer, bool simulatesNpcs,
                             net::HostAuthority* host)
    : world_(world), field_(std::move(field)), authority_(std::move(authority)), npcs_(world, seed),
      simulatesNpcs_(simulatesNpcs), host_(host) {
    session_ = std::make_unique<GameplaySession>(world, seed, field_, bird_, saveDir, localPlayer,
                                                 std::make_unique<Recorder>(authority_, events_));
    bird_.spawn(world.spawnPoint(), 0.0f);
}

int RehearsalPeer::count(GameEventType type, PlayerId actor) const {
    return static_cast<int>(std::count_if(events_.begin(), events_.end(), [&](const GameEvent& event) {
        return event.type == type && event.actor == actor;
    }));
}

// Teleporta para o caju mais proximo ate juntar a meta e depois desce no ninho. Sem meta, circula.
void RehearsalPeer::drive() {
    FlightModel& flight = bird_.flight();
    if (session_->perched()) return;
    if (goal_ == 0) {
        flight.place(flight.position(), flight.yaw() + 0.4f * kFrame, FlightModel::kCruiseSpeed);
        return;
    }
    if (session_->collector().carried() < goal_ && session_->view().totalDelivered < goal_) {
        if (const auto cashew = field_.nearest(flight.position(), 1.0e5f)) flight.place(field_.positionOf(*cashew), 0.0f, 0.0f);
        return;
    }
    if (session_->view().totalDelivered < goal_) flight.place(session_->nest().perch + glm::vec3(0, 6, 0), 0.0f, 0.0f);
}

void RehearsalPeer::step(float dt) {
    drive();
    session_->applyControls(FlightControls{});
    for (float done = 0; done + 1e-6f < dt; done += kPhysicsStep) session_->stepBird(kPhysicsStep);
    session_->update(dt);
    session_->publishLocalPlayer();
    if (!simulatesNpcs_) return;
    GameAuthority& authority = session_->authority();
    npcs_.advance(dt, authority.players(), field_, authority);
    if (!host_) return;
    host_->setNpcs(npcs_.publicStates());
}

namespace {
constexpr uint32_t kDefaultSeed = 20261207;

int hostRehearsal(const std::filesystem::path& saveDir, float seconds) {
    const World world(kDefaultSeed);
    CashewField field(world, kDefaultSeed);
    net::NetMatch match(kDefaultSeed);
    match.setCashewCount(field.size());
    if (!match.host(kRoomName)) {
        std::cout << "Falha ao hospedar: " << match.lastError() << "\n";
        return 1;
    }
    RehearsalPeer peer(world, kDefaultSeed, std::move(field), saveDir, match.authority(), 0, true, match.hostAuthority());
    peer.startProfile("Host");
    std::cout << "Hospedando \"" << kRoomName << "\" na porta " << net::kLanPort << " por ate " << seconds
              << " s. Em outro terminal: ./birds --rede-teste entrar 127.0.0.1\n";

    Pacer pacer;
    size_t shown = 0;
    float elapsed = 0, report = 0;
    bool joined = false, left = false;
    int remoteCollected = 0, remoteDelivered = 0;
    while (elapsed < seconds && !left) {
        const float dt = pacer.wait();
        elapsed += dt;
        report += dt;
        match.update(dt);
        peer.step(dt);
        for (; shown < peer.events().size(); shown++) {
            const GameEvent& event = peer.events()[shown];
            if (event.actor == 0 || event.actor == kNoPlayer) continue;   // So os eventos vindos dos clientes.
            printEvent("Host viu", event);
            joined = joined || event.type == GameEventType::PlayerJoined;
            left = left || (joined && event.type == GameEventType::PlayerLeft);
            if (event.type == GameEventType::CashewCollected) remoteCollected++;
            if (event.type == GameEventType::CashewDelivered) remoteDelivered += event.value;
        }
        if (report >= 1.0f) {
            report = 0;
            for (const PlayerPublicState& player : peer.authority().players()) {
                if (player.id == 0) continue;
                std::cout << "Host ve o jogador " << static_cast<int>(player.id) << " em (" << std::lround(player.position.x)
                          << ", " << std::lround(player.position.y) << ", " << std::lround(player.position.z)
                          << "), carga " << static_cast<int>(player.carried) << ", passaro " << player.birdSeed << "\n";
            }
        }
    }
    std::cout << "Resumo do host: entrou " << (joined ? "sim" : "nao") << ", cajus coletados pelo cliente "
              << remoteCollected << ", entregues " << remoteDelivered << ", saiu " << (left ? "sim" : "nao") << "\n";
    return joined && remoteCollected > 0 && remoteDelivered > 0 && left ? 0 : 1;
}

int joinRehearsal(const std::filesystem::path& saveDir, const std::string& address, uint32_t seed) {
    const World world(kDefaultSeed);
    net::NetMatch match(seed);
    match.setLocalIdentity("Cliente", 0);
    std::cout << "Entrando em " << address << (seed != kDefaultSeed ? " com outra semente do mundo" : "") << "...\n";
    if (!match.join(address)) {
        std::cout << "Entrada recusada: " << match.lastError() << "\n";
        return 2;
    }
    const PlayerId me = match.localPlayer();
    std::cout << "Conectado como jogador " << static_cast<int>(me) << "\n";
    RehearsalPeer peer(world, kDefaultSeed, CashewField(world, kDefaultSeed), saveDir, match.authority(), me, false);
    peer.startProfile("Cliente");
    peer.setGoal(2);

    Pacer pacer;
    size_t shown = 0;
    float elapsed = 0, afterDelivery = 0;
    bool sawHost = false;
    size_t npcs = 0, perched = 0;
    std::set<uint8_t> roles;
    while (elapsed < 30.0f) {
        const float dt = pacer.wait();
        elapsed += dt;
        match.update(dt);
        if (!match.online()) {
            std::cout << "Saiu da sala: " << match.lastError() << "\n";
            return 1;
        }
        peer.step(dt);
        for (; shown < peer.events().size(); shown++) {
            const GameEvent& event = peer.events()[shown];
            if (event.actor == me || event.type == GameEventType::PlayerJoined) printEvent("Cliente recebeu", event);
        }
        for (const PlayerPublicState& player : peer.authority().players()) sawHost = sawHost || player.id == 0;
        const auto received = match.clientAuthority()->npcs();
        npcs = std::max(npcs, received.size());
        size_t resting = 0;
        for (const net::NpcPublicState& npc : received) {
            roles.insert(npc.role);
            resting += npc.perched ? 1 : 0;
        }
        perched = std::max(perched, resting);
        if (peer.delivered()) afterDelivery += dt;
        if (afterDelivery > 1.0f) break;
    }
    const int delivered = peer.session().view().totalDelivered;
    std::cout << "Resumo do cliente: ve o host " << (sawHost ? "sim" : "nao") << ", NPCs recebidos " << npcs
              << " (" << roles.size() << " papeis, ate " << perched << " pousados), cajus entregues " << delivered << "\n";
    match.leave();
    return sawHost && npcs > 0 && delivered >= 2 ? 0 : 1;
}
}

int runNetworkRehearsal(const std::vector<std::string>& args, const std::filesystem::path& saveDir) {
    if (args.size() >= 2 && args[1] == "host")
        return hostRehearsal(saveDir, args.size() >= 3 ? std::stof(args[2]) : 60.0f);
    if (args.size() >= 3 && args[1] == "entrar")
        return joinRehearsal(saveDir, args[2], args.size() >= 4 ? static_cast<uint32_t>(std::stoul(args[3])) : kDefaultSeed);
    std::cout << "Uso: ./birds --rede-teste host [segundos] | ./birds --rede-teste entrar IP [semente]\n";
    return 1;
}

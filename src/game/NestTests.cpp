#include "game/NestTests.h"
#include "core/TestRun.h"
#include "game/GameplaySession.h"
#include "game/NestEconomy.h"
#include "game/NestModel.h"
#include "game/RivalNests.h"
#include "input/GestureControls.h"
#include "shared/LocalAuthority.h"
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kSeed = 20261207;
constexpr float kFrame = 1.0f / 60.0f;
constexpr PlayerId kMe = 0;
const char* const kName = "Teste";
const glm::vec3 kFarAway(0, 400, 0);   // Ceu aberto, longe de qualquer ninho.

// Pasta temporaria exclusiva, apagada ao final. Os testes nunca usam a pasta saves/ real.
class TempDir {
public:
    TempDir() {
        std::error_code error;
        std::random_device random;
        path_ = fs::temp_directory_path() / ("birds-nest-test-" + std::to_string(::getpid()) + "-" + std::to_string(random()));
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

// Autoridade que segura os pedidos ate o teste liberar, como a rede faria.
class HeldAuthority : public GameAuthority {
public:
    void request(const GameEvent& event) override { requests.push_back(event); }
    std::vector<GameEvent> pollEvents() override {
        std::vector<GameEvent> events;
        events.swap(confirmed);
        return events;
    }
    std::vector<PlayerPublicState> players() const override { return {}; }
    void publishLocalPlayer(const PlayerPublicState&) override {}

    void confirmAll() {
        confirmed.insert(confirmed.end(), requests.begin(), requests.end());
        requests.clear();
    }
    int count(GameEventType type) const {
        return static_cast<int>(std::count_if(requests.begin(), requests.end(),
                                              [&](const GameEvent& e) { return e.type == type; }));
    }

    std::vector<GameEvent> requests, confirmed;
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

// Uma partida completa de teste, com o mesmo caminho de chamadas do laco do jogo.
struct Rig {
    Rig(const World& world, const fs::path& dir, const std::vector<glm::vec3>& cashewSpots = {},
        std::unique_ptr<GameAuthority> authority = nullptr)
        : field(cashewsAt(cashewSpots)), session(world, kSeed, field, bird, dir, kMe, std::move(authority)) {
        bird.spawn(kFarAway, 0.0f);
        session.startProfile(kName);
    }

    void tick(const FlightControls& controls = FlightControls{}) {
        session.applyControls(controls);
        for (int i = 0; i < 2; i++) session.stepBird(kFrame / 2.0f);
        session.update(kFrame);
        bird.animate(kFrame);
    }
    void ticks(int frames) {
        for (int i = 0; i < frames; i++) tick();
    }
    void press(ArmSide side) {
        FlightControls controls;
        controls.menuPressed = side;
        tick(controls);
    }
    void flap() {
        FlightControls controls;
        controls.flaps = 1;
        controls.flapStrength = 0.8f;
        controls.mode = FlightMode::Flap;
        tick(controls);
    }
    // Chega ao poleiro e espera terminar o pouso, quando os gestos passam a valer.
    void landOnNest() {
        bird.spawn(session.nest().perch + glm::vec3(0, 6, 0), 0.0f);
        ticks(45);
    }
    int countFeedback(GameplayFeedback wanted) {
        const std::vector<GameplayFeedback> drained = session.drainFeedback();
        return static_cast<int>(std::count(drained.begin(), drained.end(), wanted));
    }

    CashewField field;
    PlayerBird bird;
    GameplaySession session;
};

// Perfil salvo com saldo e niveis escolhidos, sem precisar de atalhos dentro da sessao.
void seedProfile(const fs::path& dir, int coins, const BirdUpgradeLevels& bird = {}, const NestUpgradeLevels& nest = {},
                 int total = 0) {
    ProfileStore(dir).save(Profile::restored(kName, 1, coins, total, bird, nest));
}

bool isFree(const World& world, glm::vec3 point) {
    const glm::vec3 before = point;
    return !world.resolve(point, PlayerBird::kRadius).hit() && point == before;
}

glm::vec3 headingOf(float yaw) { return {std::sin(yaw), 0, -std::cos(yaw)}; }

bool sameSite(const NestSite& a, const NestSite& b) {
    return a.tree == b.tree && a.base == b.base && a.perch == b.perch && a.yaw == b.yaw;
}

// Prova com a colisao real: o poleiro esta livre e da para chegar pelo ceu e pela direcao de decolagem.
bool siteIsReachable(const World& world, const NestSite& site) {
    for (float rise = 0; rise <= 120.0f; rise += 1.0f)
        if (!isFree(world, site.perch + glm::vec3(0, rise, 0))) return false;
    const CashewTree& tree = world.trees()[site.tree];
    const glm::vec2 trunk(tree.base().x, tree.base().z);
    for (float distance = 0; distance < 600.0f; distance += 1.0f) {
        const glm::vec3 point = site.perch + headingOf(site.yaw) * distance;
        if (glm::length(glm::vec2(point.x, point.z) - trunk) > tree.reach() + 20.0f) return true;
        if (!isFree(world, point)) return false;
    }
    return false;
}

void testNestSite(TestRun& run, const World& world) {
    run.check("ninho: mesma semente e jogador repetem o lugar",
              sameSite(chooseNestSite(world, kSeed, 2), chooseNestSite(World(kSeed), kSeed, 2)));
    bool allReachable = true, allOnTree = true, allDistinct = true;
    for (uint32_t seed : {kSeed, 1u, 2u, 77u, 4242u, 900001u}) {
        const World generated(seed);
        std::set<size_t> trees;
        for (PlayerId player = 0; player < kMaxPlayers; player++) {
            const NestSite site = chooseNestSite(generated, seed, player);
            allOnTree = allOnTree && site.tree < generated.trees().size();
            if (site.tree >= generated.trees().size()) continue;
            const CashewTree& tree = generated.trees()[site.tree];
            allOnTree = allOnTree && glm::length(glm::vec2(site.base.x - tree.base().x, site.base.z - tree.base().z)) < tree.reach()
                     && site.base.y > tree.base().y + tree.height() * 0.5f;
            allReachable = allReachable && siteIsReachable(generated, site);
            trees.insert(site.tree);
        }
        allDistinct = allDistinct && trees.size() == kMaxPlayers;
    }
    run.check("ninho: fica sobre um cajueiro em todas as sementes", allOnTree);
    run.check("ninho: poleiro livre e acessivel segundo World::resolve", allReachable);
    run.check("ninho: cada um dos 6 jogadores tem o proprio cajueiro", allDistinct);
}

// Cada computador monta o proprio World: o lugar de cada ninho precisa sair igual em todos.
void testRivalSites(TestRun& run, const World& world) {
    const World elsewhere(kSeed);
    std::set<size_t> trees;
    std::set<std::pair<float, float>> spots;
    bool same = true;
    for (PlayerId player = 0; player < kMaxPlayers; player++) {
        const NestSite site = chooseNestSite(world, kSeed, player);
        same = same && sameSite(site, chooseNestSite(elsewhere, kSeed, player));
        trees.insert(site.tree);
        spots.insert({site.base.x, site.base.z});
    }
    run.check("ninhos da sala: 6 jogadores em lugares distintos", trees.size() == kMaxPlayers && spots.size() == kMaxPlayers);
    run.check("ninhos da sala: o mesmo lugar em todas as maquinas", same);

    // O host (jogador 0) e um cliente (jogador 3) veem os mesmos ninhos dos jogadores que os dois enxergam.
    std::vector<PlayerPublicState> players;
    for (PlayerId player = 0; player < kMaxPlayers; player++) {
        PlayerPublicState state;
        state.id = player;
        state.nestLevel = static_cast<uint8_t>(player % 3);
        players.push_back(state);
    }
    RivalNests host(world, kSeed), client(elsewhere, kSeed);
    host.update(players, 0);
    client.update(players, 3);
    bool agree = host.nests().size() == kMaxPlayers - 1 && client.nests().size() == kMaxPlayers - 1;
    for (const RivalNest& nest : host.nests()) {
        const RivalNest* other = client.find(nest.owner);
        if (nest.owner == 3) continue;
        agree = agree && other && sameSite(nest.site, other->site) && nest.sizeLevel == other->sizeLevel;
    }
    agree = agree && client.find(0) && sameSite(client.find(0)->site, chooseNestSite(world, kSeed, 0));
    run.check("ninhos da sala: host e cliente concordam, inclusive no ninho do host", agree);
    run.check("ninhos da sala: o proprio ninho nao entra na lista dos outros", !host.find(0) && !client.find(3));
}

// Entrar, comprar Tamanho, sair e cair, como chega pela lista replicada de jogadores.
void testRivalRoster(TestRun& run, const World& world) {
    const auto stateOf = [](PlayerId id, int level) {
        PlayerPublicState state;
        state.id = id;
        state.nestLevel = static_cast<uint8_t>(level);
        return state;
    };
    const auto meshSize = [](const RivalNests& rivals) { return buildRivalNestsMesh(rivals.nests()).vertices().size(); };
    RivalNests rivals(world, kSeed);
    const PlayerPublicState me = stateOf(kMe, 2);

    run.check("ninho rival: jogando sozinho nao ha ninho dos outros",
              !rivals.update({me}, kMe) && rivals.nests().empty() && rivals.revision() == 0 && meshSize(rivals) == 0);

    const bool joined = rivals.update({me, stateOf(2, 0)}, kMe);
    const RivalNest* nest = rivals.find(2);
    run.check("ninho rival: aparece quando o jogador entra",
              joined && nest && sameSite(nest->site, chooseNestSite(world, kSeed, 2)) && nest->sizeLevel == 0);
    const size_t small = meshSize(rivals);
    const int revision = rivals.revision();
    bool steady = true;
    for (int frame = 0; frame < 30; frame++) steady = steady && !rivals.update({stateOf(2, 0), me}, kMe);
    run.check("ninho rival: sem mudanca nao remonta a malha", steady && rivals.revision() == revision);

    const bool grew = rivals.update({me, stateOf(2, 3)}, kMe);
    run.check("ninho rival: cresce quando o dono compra Tamanho",
              grew && rivals.find(2)->sizeLevel == 3 && meshSize(rivals) > small && rivals.revision() == revision + 1);
    run.check("ninho rival: nivel invalido vindo da rede fica no maximo",
              rivals.update({me, stateOf(2, 200)}, kMe) && rivals.find(2)->sizeLevel == kMaxUpgradeLevel);

    rivals.update({me, stateOf(2, 3), stateOf(4, 1)}, kMe);
    const bool left = rivals.update({me, stateOf(4, 1)}, kMe);
    run.check("ninho rival: some quando o dono sai e o outro fica",
              left && !rivals.find(2) && rivals.find(4) && rivals.nests().size() == 1);
    const bool dropped = rivals.update({me}, kMe);
    run.check("ninho rival: some quando o dono cai", dropped && rivals.nests().empty() && meshSize(rivals) == 0);

    const NestSite own = chooseNestSite(world, kSeed, kMe), rival = chooseNestSite(world, kSeed, 4);
    const glm::vec3 camera = (own.base + rival.base) * 0.5f + glm::vec3(0, 200, 0);
    const float ownScale = glm::length(glm::vec3(nestBeaconTransform(own, 0, camera, 0.0f)[0]));
    const float rivalScale = glm::length(glm::vec3(rivalBeaconTransform(own, 0, camera, 0.0f)[0]));
    bool bluer = true;
    for (const Vertex& vertex : buildRivalBeaconMesh().vertices()) bluer = bluer && vertex.color.b > vertex.color.r;
    run.check("ninho rival: marcador menor e azul", rivalScale > 0 && rivalScale < ownScale * 0.7f && bluer);
}

void testVisualGrowth(TestRun& run) {
    const auto extent = [](int level) {
        NestSite site;
        float widest = 0;
        for (const Vertex& vertex : buildNestMesh(site, level).vertices())
            widest = std::max(widest, glm::length(glm::vec2(vertex.position.x, vertex.position.z)));
        return widest;
    };
    run.check("ninho: o visual cresce a cada nivel de Tamanho", extent(1) > extent(0) && extent(5) > extent(4) * 1.05f);
    NestSite site;
    const glm::mat4 close = nestBeaconTransform(site, 0, site.base + glm::vec3(0, 50, 10), 0.0f);
    const glm::mat4 far = nestBeaconTransform(site, 0, site.base + glm::vec3(700, 50, 0), 0.0f);
    run.check("ninho: o marcador some de perto e aumenta de longe",
              close == glm::mat4(0.0f) && glm::length(glm::vec3(far[0])) > 3.0f);
}

void testLandingAndDelivery(TestRun& run, const World& world) {
    TempDir dir;
    auto authority = std::make_unique<HeldAuthority>();
    HeldAuthority& held = *authority;
    Rig rig(world, dir.path(), {kFarAway, kFarAway + glm::vec3(1, 0, 0)}, std::move(authority));
    rig.bird.flight().place(kFarAway, 0.0f, 0.0f);
    rig.session.update(kFrame);
    held.confirmAll();
    rig.session.update(kFrame);
    run.check("entrega: carga de partida coletada", rig.session.collector().carried() == 2);
    run.check("entrega: cada caju coletado avisa uma vez", rig.countFeedback(GameplayFeedback::CashewCollected) == 2);

    rig.landOnNest();
    const glm::vec3 perch = rig.session.nest().perch;
    run.check("pouso: chegar perto pousa e abre o menu",
              rig.session.perched() && rig.session.view().perchedOnNest && rig.session.view().menu.page == NestMenuPage::Root);
    run.check("pouso: fica exatamente no poleiro", glm::length(rig.bird.flight().position() - perch) < 0.001f);
    run.check("pouso: avisa uma unica vez", rig.countFeedback(GameplayFeedback::NestLanded) == 1);
    run.check("entrega: pede a autoridade com toda a carga",
              held.count(GameEventType::CashewDelivered) == 1 && held.requests.back().value == 2 && held.requests.back().actor == kMe);
    run.check("entrega: nada muda antes da confirmacao",
              rig.session.collector().carried() == 2 && rig.session.profile()->coins() == 0
                  && rig.session.profile()->totalDelivered() == 0 && rig.session.view().record == 0);
    rig.ticks(30);
    run.check("entrega: nao repete o pedido enquanto espera", held.count(GameEventType::CashewDelivered) == 1);

    held.confirmAll();
    rig.ticks(1);
    run.check("entrega: confirmada vira moeda e pontuacao",
              rig.session.collector().carried() == 0 && rig.session.view().coins == 2 && rig.session.view().totalDelivered == 2);
    run.check("entrega: atualiza o record", rig.session.view().record == 2 && rig.session.view().recordHolder == kName);
    run.check("entrega: salva no perfil", ProfileStore(dir.path()).load(kName)->coins() == 2);
    run.check("entrega: avisa uma unica vez", rig.countFeedback(GameplayFeedback::CashewDelivered) == 1);

    held.confirmed.push_back({GameEventType::CashewDelivered, kMe, kNoPlayer, kNoCashew, 2, perch});
    rig.ticks(1);
    run.check("entrega: confirmacao repetida nao paga de novo", rig.session.view().coins == 2);

    rig.ticks(120);
    run.check("pouso: continua parado no poleiro",
              glm::length(rig.bird.flight().position() - perch) < 0.001f && rig.bird.flight().speed() == 0.0f);
}

void testMenu(TestRun& run, const World& world) {
    TempDir dir;
    seedProfile(dir.path(), 0);
    Rig rig(world, dir.path());
    rig.landOnNest();
    rig.session.drainFeedback();
    const auto menu = [&]() -> const NestMenuView& { return rig.session.view().menu; };
    run.check("menu: raiz oferece Comer e Investir",
              menu().page == NestMenuPage::Root && menu().leftLabel == "Comer" && menu().rightLabel == "Investir" && menu().options.empty());

    rig.press(ArmSide::Left);
    const std::vector<std::string> eat = {"Velocidade", "Agilidade", "Capacidade", "Alcance", "Voltar"};
    bool namesMatch = menu().page == NestMenuPage::Eat && menu().options.size() == eat.size();
    for (size_t i = 0; namesMatch && i < eat.size(); i++) namesMatch = menu().options[i].name == eat[i];
    run.check("menu: esquerdo abre Comer com as opcoes certas", namesMatch);
    run.check("menu: lista explica os dois bracos", menu().leftLabel == "Próxima opção" && menu().rightLabel == "Faltam 3 moedas");

    bool cycles = true;
    for (int i = 1; i < 5; i++) {
        rig.press(ArmSide::Left);
        cycles = cycles && menu().selected == i;
    }
    run.check("menu: esquerdo passa por todas as opcoes", cycles);
    run.check("menu: ultima opcao mostra Voltar", menu().rightLabel == "Voltar" && menu().options[4].maxLevel == 0);
    rig.press(ArmSide::Right);
    run.check("menu: Voltar retorna para a raiz", menu().page == NestMenuPage::Root && menu().selected == 0);

    rig.press(ArmSide::Right);
    const std::vector<std::string> invest = {"Defesa", "Alarme", "Cofre", "Tamanho", "Voltar"};
    namesMatch = menu().page == NestMenuPage::Invest && menu().options.size() == invest.size();
    for (size_t i = 0; namesMatch && i < invest.size(); i++) namesMatch = menu().options[i].name == invest[i];
    run.check("menu: direito abre Investir com as opcoes certas", namesMatch);
    for (int i = 0; i < 5; i++) rig.press(ArmSide::Left);
    run.check("menu: a lista da a volta para a primeira opcao", menu().selected == 0);
    run.check("menu: cada movimento avisa uma vez", rig.countFeedback(GameplayFeedback::MenuMove) == 12);

    FlightControls holding;
    holding.raisedArm = ArmSide::Right;
    holding.menuProgress = 0.4f;
    rig.tick(holding);
    run.check("menu: mostra o braco erguido e o progresso",
              menu().raisedArm == ArmSide::Right && std::abs(menu().holdProgress - 0.4f) < 1e-6f);
    rig.flap();
    run.check("menu: bater as asas fecha o menu", menu().page == NestMenuPage::Closed && menu().leftLabel.empty());
}

void testPurchases(TestRun& run, const World& world) {
    TempDir dir;
    seedProfile(dir.path(), upgradePrice(BirdUpgrade::Speed, 0) + 1);
    Rig rig(world, dir.path());
    rig.landOnNest();
    rig.press(ArmSide::Left);
    rig.session.drainFeedback();
    rig.press(ArmSide::Right);
    run.check("compra: desconta o preco e sobe o nivel",
              rig.session.view().coins == 1 && rig.session.profile()->level(BirdUpgrade::Speed) == 1);
    run.check("compra: avisa uma unica vez", rig.countFeedback(GameplayFeedback::UpgradeBought) == 1);
    const std::optional<Profile> saved = ProfileStore(dir.path()).load(kName);
    run.check("compra: salva na hora", saved && saved->coins() == 1 && saved->level(BirdUpgrade::Speed) == 1);
    run.check("compra: o preco cresce com o nivel", rig.session.view().menu.options[0].price == upgradePrice(BirdUpgrade::Speed, 1)
                                                        && upgradePrice(BirdUpgrade::Speed, 1) > upgradePrice(BirdUpgrade::Speed, 0));

    rig.press(ArmSide::Right);
    run.check("compra: sem moedas e negada", rig.countFeedback(GameplayFeedback::UpgradeDenied) == 1);
    run.check("compra: negada nao gasta nem sobe",
              rig.session.view().coins == 1 && rig.session.profile()->level(BirdUpgrade::Speed) == 1);

    TempDir maxedDir;
    seedProfile(maxedDir.path(), 500, {kMaxUpgradeLevel, 0, 0, 0});
    Rig maxed(world, maxedDir.path());
    maxed.landOnNest();
    maxed.press(ArmSide::Left);
    run.check("compra: nivel maximo aparece no rotulo", maxed.session.view().menu.rightLabel == "Nível máximo");
    maxed.session.drainFeedback();
    maxed.press(ArmSide::Right);
    run.check("compra: acima do maximo e negada", maxed.countFeedback(GameplayFeedback::UpgradeDenied) == 1);
    run.check("compra: acima do maximo nao gasta",
              maxed.session.view().coins == 500 && maxed.session.profile()->level(BirdUpgrade::Speed) == kMaxUpgradeLevel);

    bool rising = true;
    for (int level = 1; level < kMaxUpgradeLevel; level++) {
        for (int i = 0; i < kBirdUpgradeCount; i++)
            rising = rising && upgradePrice(static_cast<BirdUpgrade>(i), level) > upgradePrice(static_cast<BirdUpgrade>(i), level - 1);
        for (int i = 0; i < kNestUpgradeCount; i++)
            rising = rising && upgradePrice(static_cast<NestUpgrade>(i), level) > upgradePrice(static_cast<NestUpgrade>(i), level - 1);
    }
    run.check("economia: todos os precos crescem por nivel", rising);
    run.check("economia: nivel maximo nao tem preco", upgradePrice(NestUpgrade::Size, kMaxUpgradeLevel) == 0);
}

// Compra a opcao indicada de uma lista, navegando como o jogador faria.
void buy(Rig& rig, ArmSide list, int option) {
    rig.press(list);
    for (int i = 0; i < option; i++) rig.press(ArmSide::Left);
    rig.press(ArmSide::Right);
    for (int i = option; i < NestMenu::kBackOption; i++) rig.press(ArmSide::Left);
    rig.press(ArmSide::Right);
}

float cruiseDistance(Rig& rig) {
    rig.bird.spawn(kFarAway, 0.0f);
    rig.ticks(300);
    const glm::vec3 start = rig.bird.flight().position();
    rig.ticks(120);
    const glm::vec3 end = rig.bird.flight().position();
    return glm::length(glm::vec2(end.x - start.x, end.z - start.z));
}

float turnedAngle(Rig& rig) {
    rig.bird.spawn(kFarAway, 0.0f);
    FlightControls controls;
    controls.bank = 1.0f;
    for (int i = 0; i < 60; i++) rig.tick(controls);
    return rig.bird.flight().yaw();
}

void testUpgradeEffects(TestRun& run, const World& world) {
    TempDir dir;
    seedProfile(dir.path(), 500);
    const glm::vec3 spot = kFarAway + glm::vec3(0, 0, 10.5f);
    Rig rig(world, dir.path(), {spot});

    const float cruiseBefore = cruiseDistance(rig), turnBefore = turnedAngle(rig);
    rig.bird.flight().place(kFarAway, 0.0f, 0.0f);
    rig.ticks(1);
    run.check("efeito: caju fora do alcance inicial nao e coletado", rig.session.collector().carried() == 0);

    rig.landOnNest();
    buy(rig, ArmSide::Left, 0);
    buy(rig, ArmSide::Left, 1);
    buy(rig, ArmSide::Left, 2);
    buy(rig, ArmSide::Left, 3);
    run.check("efeito: Velocidade altera FlightStats::speed", rig.bird.flight().stats().speed == speedMultiplier(1));
    run.check("efeito: Agilidade altera FlightStats::agility", rig.bird.flight().stats().agility == agilityMultiplier(1));
    run.check("efeito: Capacidade altera CargoStats", rig.session.collector().stats().capacity == 4 && rig.session.view().capacity == 4);
    run.check("efeito: Alcance altera CargoStats", rig.session.collector().stats().collectRadius > 10.5f);

    rig.flap();
    rig.bird.flight().place(kFarAway, 0.0f, 0.0f);
    rig.ticks(3);
    run.check("efeito: Alcance coleta o caju que antes estava longe", rig.session.collector().carried() == 1);
    run.check("efeito: Velocidade faz voar mais longe", cruiseDistance(rig) > cruiseBefore * 1.05f);
    run.check("efeito: Agilidade faz curvar mais rapido", turnedAngle(rig) > turnBefore * 1.08f);

    TempDir nestDir;
    seedProfile(nestDir.path(), 500);
    Rig nest(world, nestDir.path(), {kFarAway, kFarAway + glm::vec3(1, 0, 0)});
    nest.bird.flight().place(kFarAway, 0.0f, 0.0f);
    nest.ticks(3);
    const int revisionBefore = nest.session.nestRevision();
    nest.landOnNest();
    const int coinsWithoutBonus = nest.session.view().coins;
    run.check("efeito: sem Tamanho a entrega nao tem bonus", coinsWithoutBonus == 502);
    buy(nest, ArmSide::Right, 3);
    run.check("efeito: Tamanho pede novo visual do ninho",
              nest.session.nestSizeLevel() == 1 && nest.session.nestRevision() == revisionBefore + 1);
    buy(nest, ArmSide::Right, 0);
    buy(nest, ArmSide::Right, 1);
    buy(nest, ArmSide::Right, 2);
    const Profile& profile = *nest.session.profile();
    run.check("efeito: Defesa, Alarme e Cofre registram o nivel",
              profile.level(NestUpgrade::Defense) == 1 && profile.level(NestUpgrade::Alarm) == 1 && profile.level(NestUpgrade::Vault) == 1);

    const int coinsBefore = nest.session.view().coins, totalBefore = nest.session.view().totalDelivered;
    nest.flap();
    nest.field.advance(100.0f);
    nest.bird.flight().place(kFarAway, 0.0f, 0.0f);
    nest.ticks(5);
    const int carried = nest.session.collector().carried();
    nest.landOnNest();
    run.check("efeito: Tamanho paga bonus de moedas na entrega",
              carried == 2 && nest.session.view().coins == coinsBefore + carried + sizeCoinBonus(carried, 1));
    run.check("efeito: o bonus nao soma na pontuacao", nest.session.view().totalDelivered == totalBefore + carried);
}

void testTakeOff(TestRun& run, const World& world) {
    TempDir dir;
    Rig rig(world, dir.path());
    rig.bird.spawn(rig.session.nest().perch + glm::vec3(0, 6, 0), 0.0f);
    rig.ticks(5);
    rig.flap();
    run.check("decolagem: batida durante o pouso e ignorada", rig.session.perched());
    rig.ticks(45);
    rig.session.drainFeedback();
    const glm::vec3 perch = rig.session.nest().perch;

    rig.flap();
    run.check("decolagem: bater as asas sai do ninho", !rig.session.perched() && !rig.session.view().perchedOnNest);
    run.check("decolagem: avisa uma unica vez", rig.countFeedback(GameplayFeedback::TakeOff) == 1);
    run.check("decolagem: sai com impulso",
              rig.bird.flight().speed() > FlightModel::kCruiseSpeed && rig.bird.flight().verticalSpeed() > 10.0f);

    bool landedAgain = false;
    for (int i = 0; i < 40; i++) {
        rig.tick();
        landedAgain = landedAgain || rig.session.perched();
    }
    run.check("decolagem: nao pousa de novo logo depois", !landedAgain && rig.bird.flight().position().y > perch.y);

    rig.bird.spawn(perch + glm::vec3(4, 0, 0), 0.0f);
    rig.ticks(1);
    run.check("decolagem: voltar sem se afastar nao reabre o pouso", !rig.session.perched());

    rig.bird.spawn(perch + glm::vec3(0, 60, 0), 0.0f);
    rig.ticks(1);
    rig.bird.spawn(perch + glm::vec3(0, 6, 0), 0.0f);
    rig.ticks(1);
    run.check("decolagem: depois de se afastar o pouso volta a valer", rig.session.perched());
}

void testPersistence(TestRun& run, const World& world) {
    TempDir dir;
    seedProfile(dir.path(), 100);
    {
        Rig rig(world, dir.path());
        rig.landOnNest();
        buy(rig, ArmSide::Left, 0);
        buy(rig, ArmSide::Left, 2);
        buy(rig, ArmSide::Left, 3);
        buy(rig, ArmSide::Right, 3);
        rig.session.saveNow();
    }
    Rig rig(world, dir.path());
    const int spent = upgradePrice(BirdUpgrade::Speed, 0) + upgradePrice(BirdUpgrade::CarryCapacity, 0)
                    + upgradePrice(BirdUpgrade::CollectRange, 0) + upgradePrice(NestUpgrade::Size, 0);
    run.check("persistencia: moedas sobrevivem ao reiniciar", rig.session.view().coins == 100 - spent);
    run.check("persistencia: niveis voltam no perfil", rig.session.profile()->level(BirdUpgrade::Speed) == 1
                                                           && rig.session.profile()->level(NestUpgrade::Size) == 1);
    run.check("persistencia: velocidade e reaplicada ao passaro", rig.bird.flight().stats().speed == speedMultiplier(1));
    run.check("persistencia: carga e reaplicada ao coletor", rig.session.collector().stats().capacity == carryCapacity(1)
                                                                 && rig.session.collector().stats().collectRadius == collectRadius(1));
    run.check("persistencia: tamanho do ninho e reaplicado", rig.session.nestSizeLevel() == 1 && rig.session.nestRevision() > 0);
    run.check("persistencia: perfil novo comeca do zero", (rig.session.startProfile("Outro"), rig.session.view().coins == 0
                                                           && rig.bird.flight().stats().speed == 1.0f && rig.session.nestSizeLevel() == 0));
    run.check("persistencia: nome invalido mantem o perfil atual", (rig.session.startProfile(""), rig.session.view().playerName == "Outro"));
}

void testRecordAndFeedback(TestRun& run, const World& world) {
    TempDir dir;
    RecordManager(dir.path()).startSession("Rival", 2);
    seedProfile(dir.path(), 0);
    const std::vector<glm::vec3> spots = {kFarAway, kFarAway + glm::vec3(1, 0, 0), kFarAway + glm::vec3(2, 0, 0)};
    Rig rig(world, dir.path(), spots);
    run.check("record: mostra o record de outro perfil", rig.session.view().record == 2 && rig.session.view().recordHolder == "Rival"
                                                             && !rig.session.view().leadingRecord);
    rig.bird.flight().place(kFarAway, 0.0f, 0.0f);
    rig.ticks(3);
    rig.landOnNest();
    const std::vector<GameplayFeedback> first = rig.session.drainFeedback();
    run.check("record: ultrapassar avisa uma unica vez",
              std::count(first.begin(), first.end(), GameplayFeedback::RecordBeaten) == 1);
    run.check("feedback: cada aviso sai uma unica vez", rig.session.drainFeedback().empty());
    run.check("record: passa a liderar", rig.session.view().record == 3 && rig.session.view().leadingRecord
                                             && rig.session.view().recordBeatenNow);
    rig.ticks(7 * 60);
    run.check("record: o aviso na tela termina sozinho", !rig.session.view().recordBeatenNow && rig.session.view().leadingRecord);

    rig.flap();
    rig.field.advance(100.0f);
    rig.bird.flight().place(kFarAway, 0.0f, 0.0f);
    rig.ticks(5);
    rig.landOnNest();
    run.check("record: novas entregas nao repetem o aviso", rig.countFeedback(GameplayFeedback::RecordBeaten) == 0
                                                                && rig.session.view().totalDelivered > 3);

    TempDir freshDir;
    Rig fresh(world, freshDir.path(), {kFarAway});
    fresh.bird.flight().place(kFarAway, 0.0f, 0.0f);
    fresh.ticks(3);
    fresh.landOnNest();
    run.check("record: a primeira entrega do computador nao e aviso de record",
              fresh.countFeedback(GameplayFeedback::RecordBeaten) == 0 && fresh.session.view().record == 1);
}

void testNestDirection(TestRun& run, const World& world) {
    TempDir dir;
    Rig rig(world, dir.path());
    const glm::vec3 perch = rig.session.nest().perch;
    rig.bird.flight().place(perch + glm::vec3(-100, 0, 0), 0.0f, 0.0f);
    rig.session.update(0.0f);
    run.check("direcao: distancia ate o ninho", std::abs(rig.session.view().nestDistance - 100.0f) < 0.01f);
    run.check("direcao: ninho a direita e positivo", std::abs(rig.session.view().nestBearing - 1.5707963f) < 0.001f);
    rig.bird.flight().place(perch + glm::vec3(100, 0, 0), 0.0f, 0.0f);
    rig.session.update(0.0f);
    run.check("direcao: ninho a esquerda e negativo", std::abs(rig.session.view().nestBearing + 1.5707963f) < 0.001f);
    rig.bird.flight().place(perch + glm::vec3(0, 0, 100), 0.0f, 0.0f);
    rig.session.update(0.0f);
    run.check("direcao: ninho a frente e zero", std::abs(rig.session.view().nestBearing) < 0.001f);
}

// Pilota usando so o que a interface mostra: direcao do ninho e altura do poleiro.
FlightControls pilotToNest(const Rig& rig, int frame) {
    FlightControls controls;
    controls.bank = std::clamp(rig.session.view().nestBearing * 2.0f, -1.0f, 1.0f);
    if (rig.bird.flight().position().y < rig.session.nest().perch.y && frame % 20 == 0) {
        controls.flaps = 1;
        controls.flapStrength = 0.6f;
        controls.mode = FlightMode::Flap;
    }
    return controls;
}

// Ciclo completo: coleta em voo, entrega no ninho, compra e fica mensuravelmente mais rapido.
void testFullLoop(TestRun& run, const World& world) {
    TempDir dir;
    const NestSite site = chooseNestSite(world, kSeed, kMe);
    const CashewTree& tree = world.trees()[site.tree];
    const glm::vec3 heading = headingOf(site.yaw);
    const float startDistance = tree.reach() + 15.0f;
    const glm::vec3 start = site.perch + heading * startDistance;
    std::vector<glm::vec3> spots;
    for (float offset : {12.0f, 24.0f, 36.0f}) spots.push_back(start - heading * offset);

    Rig rig(world, dir.path(), spots);
    const float cruiseBefore = cruiseDistance(rig);
    rig.bird.spawn(start, site.yaw + 3.1415927f);
    for (int frame = 0; frame < 1800 && !rig.session.perched(); frame++) rig.tick(pilotToNest(rig, frame));
    run.check("ciclo: voa ate o ninho e pousa sozinho", rig.session.perched());
    run.check("ciclo: coleta os tres cajus no caminho", rig.session.collector().carried() == 3 || rig.session.view().coins == 3);

    rig.ticks(45);
    run.check("ciclo: entrega vira moedas e pontuacao", rig.session.view().coins == 3 && rig.session.view().totalDelivered == 3);
    rig.press(ArmSide::Left);
    rig.press(ArmSide::Right);
    run.check("ciclo: as moedas compram Velocidade", rig.session.profile()->level(BirdUpgrade::Speed) == 1 && rig.session.view().coins == 0);
    rig.flap();
    run.check("ciclo: decola depois da compra", !rig.session.perched());

    const float cruiseAfter = cruiseDistance(rig);
    run.check("ciclo: o passaro fica mensuravelmente mais rapido", cruiseAfter > cruiseBefore * 1.05f);
}
}

void testBodyControls(TestRun& run, const World& world) {
    GestureControls mapper;
    GestureState gesture;
    gesture.mode = FlightMode::Flap;
    gesture.stroke = 0.8f;
    gesture.flapCount = 2;
    gesture.flapStrength = 0.7f;
    gesture.menuCount = 1;
    gesture.menuChoice = ArmSide::Right;
    FlightControls read = mapper.read(gesture, true, 10.0);
    run.check("corpo: usa o voo continuo", read.bodyControlled && read.stroke == 0.8f);
    run.check("corpo: conta as batidas e o menu", read.flaps == 2 && read.menuPressed == ArmSide::Right);
    read = mapper.read(gesture, false, 10.1);
    run.check("corpo: cada batida e cada escolha valem uma unica vez", read.flaps == 0 && read.menuPressed == ArmSide::None);
    run.check("corpo: imagem recente mantem a batida", read.stroke == 0.8f);
    read = mapper.read(gesture, false, 10.5);
    run.check("corpo: sem imagem nova a batida para de empurrar", read.stroke == 0.0f);

    TempDir dir;
    Rig rig(world, dir.path());
    rig.landOnNest();
    FlightControls body;
    body.bodyControlled = true;
    body.flaps = 1;
    body.flapStrength = 0.7f;
    rig.tick(body);
    run.check("corpo: bater os bracos decola do ninho", !rig.session.perched() && rig.bird.flight().verticalSpeed() > 10.0f);
}

void testHunterDrop(TestRun& run, const World& world) {
    TempDir dir;
    auto owned = std::make_unique<LocalAuthority>();
    LocalAuthority& authority = *owned;
    const glm::vec3 cashew = kFarAway + glm::vec3(0, 0, -2);
    Rig rig(world, dir.path(), {cashew, cashew + glm::vec3(1, 0, 0)}, std::move(owned));
    rig.bird.flight().place(kFarAway, 0.0f, 0.0f);
    rig.ticks(3);
    run.check("cacador: o jogador esta com carga", rig.session.view().carried == 2);

    rig.session.publishLocalPlayer();
    run.check("cacador: em voo a carga fica visivel", authority.players().size() == 1 && authority.players()[0].carried == 2);
    rig.session.drainFeedback();
    GameEvent drop;
    drop.type = GameEventType::CashewDropped;
    drop.target = kMe;
    drop.value = 2;
    authority.request(drop);
    rig.tick();
    run.check("cacador: derruba a carga e mostra o aviso", rig.session.view().carried == 0 && rig.session.view().cargoLostNow);
    run.check("cacador: avisa uma unica vez", rig.countFeedback(GameplayFeedback::CargoLost) == 1);
    rig.ticks(200);
    run.check("cacador: o aviso some sozinho", !rig.session.view().cargoLostNow);

    authority.request(drop);
    rig.tick();
    run.check("cacador: sem carga nao ha aviso", rig.countFeedback(GameplayFeedback::CargoLost) == 0);

    rig.landOnNest();
    rig.session.publishLocalPlayer();
    run.check("cacador: quem esta no ninho nao vira alvo", authority.players()[0].carried == 0);
}

// Jogo local com um rival conectado, cujo estado o teste controla.
class RivalAuthority final : public LocalAuthority {
public:
    explicit RivalAuthority(PlayerId id) { rival.id = id; rival.position = glm::vec3(0, 300, 0); }
    std::vector<PlayerPublicState> players() const override {
        std::vector<PlayerPublicState> list = LocalAuthority::players();
        if (present) list.push_back(rival);
        return list;
    }
    PlayerPublicState rival;
    bool present = true;
};

constexpr PlayerId kRivalId = 1;

struct RobRig {
    RobRig(const World& world, const fs::path& dir, int stock, int defense = 0, const std::vector<glm::vec3>& cashews = {}) {
        auto owned = std::make_unique<RivalAuthority>(kRivalId);
        authority = owned.get();
        authority->rival.nestStock = static_cast<uint8_t>(stock);
        authority->rival.nestDefense = static_cast<uint8_t>(defense);
        rig = std::make_unique<Rig>(world, dir, cashews, std::move(owned));
        rivalSite = chooseNestSite(world, kSeed, kRivalId);
        rig->ticks(1);   // A lista de jogadores chega e o ninho rival aparece.
    }
    void landOnRival() {
        rig->bird.spawn(rivalSite.perch + glm::vec3(0, 6, 0), 0.0f);
        rig->ticks(45);
        rig->session.drainFeedback();
    }
    int seconds(float s) { return static_cast<int>(std::lround(s / kFrame)); }

    RivalAuthority* authority = nullptr;
    std::unique_ptr<Rig> rig;
    NestSite rivalSite;
};

void testRobbery(TestRun& run, const World& world) {
    TempDir dir;
    RobRig rob(world, dir.path(), 5);
    Rig& rig = *rob.rig;
    run.check("roubo: o ninho do rival existe", rig.session.rivalNests().find(kRivalId) != nullptr);
    rob.landOnRival();
    const GameplayView& view = rig.session.view();
    run.check("roubo: pousa no ninho rival sem abrir o menu",
              rig.session.robbing() && !view.perchedOnNest && view.menu.page == NestMenuPage::Closed && view.robbery.active);
    run.check("roubo: fica no poleiro do rival", glm::length(rig.bird.flight().position() - rob.rivalSite.perch) < 0.001f);

    rig.ticks(rob.seconds(robSeconds(0)) - 60);
    run.check("roubo: a barra enche aos poucos", rig.session.collector().carried() == 0 && view.robbery.progress > 0.6f &&
                                                 view.robbery.progress < 1.0f);
    rig.ticks(70);
    run.check("roubo: depois de 4 s leva um caju marcado como roubado",
              rig.session.collector().carried() == 1 && rig.session.collector().stolen() == 1 && view.robbery.stolenNow == 1);
    run.check("roubo: avisa com som proprio", rig.countFeedback(GameplayFeedback::CashewStolen) == 1);
    rig.ticks(rob.seconds(robSeconds(0)) * 2 + 20);
    run.check("roubo: continua enquanto fica pousado e cabe na carga",
              rig.session.collector().carried() == 3 && view.robbery.blocked == RobBlock::CargoFull);

    rig.flap();
    run.check("roubo: bater as asas foge do ninho rival", !rig.session.perched() && !rig.session.robbing());
    rig.bird.spawn(rig.session.nest().perch + glm::vec3(0, 6, 0), 0.0f);
    rig.ticks(45);
    rig.ticks(2);
    const Profile* profile = rig.session.profile();
    run.check("roubo: roubados viram moeda e nao somam no record",
              profile->coins() == 3 && profile->totalDelivered() == 0 && rig.session.view().record == 0);
}

void testRobberyLimits(TestRun& run, const World& world) {
    {
        TempDir dir;
        RobRig rob(world, dir.path(), 5, 2);
        rob.landOnRival();
        rob.rig->ticks(rob.seconds(robSeconds(0)) + 30);
        const bool slower = rob.rig->session.collector().carried() == 0;
        rob.rig->ticks(rob.seconds(robSeconds(2) - robSeconds(0)));
        run.check("defesa: cada nivel deixa o roubo mais lento", slower && rob.rig->session.collector().carried() == 1 &&
                                                                    robSeconds(2) == 7.0f && robSeconds(9) == robSeconds(5));
    }
    {
        TempDir dir;
        RobRig rob(world, dir.path(), 0);
        rob.landOnRival();
        rob.rig->ticks(rob.seconds(10.0f));
        run.check("cofre: ninho sem moedas fora do Cofre nao e roubado",
                  rob.rig->session.collector().carried() == 0 && rob.rig->session.view().robbery.blocked == RobBlock::EmptyNest);
    }
    {
        TempDir dir;
        RobRig rob(world, dir.path(), 5);
        rob.landOnRival();
        rob.authority->present = false;
        rob.rig->ticks(2);
        run.check("roubo: se o dono sai da sala, o ladrao decola", !rob.rig->session.perched());
    }
}

GameEvent robbedBy(PlayerId thief) {
    GameEvent event;
    event.type = GameEventType::NestRobbed;
    event.actor = thief;
    event.target = kMe;
    event.value = 1;
    return event;
}

void testVictim(TestRun& run, const World& world) {
    {
        TempDir dir;
        seedProfile(dir.path(), 12, {}, {3, 0, 2, 0});
        RobRig rob(world, dir.path(), 0);
        Rig& rig = *rob.rig;
        rig.session.publishLocalPlayer();
        const PlayerPublicState& me = rob.authority->players().front();
        run.check("cofre: publica as moedas que podem ser roubadas e a Defesa", me.nestStock == 2 && me.nestDefense == 3);
        rob.authority->request(robbedBy(kRivalId));
        rig.tick();
        run.check("vitima: perde uma moeda por caju roubado e salva",
                  rig.session.profile()->coins() == 11 && ProfileStore(dir.path()).load(kName)->coins() == 11);
        run.check("alarme: sem Alarme a vitima nao e avisada",
                  !rig.session.view().alarmNow && rig.countFeedback(GameplayFeedback::NestAlarm) == 0);
        for (int i = 0; i < 3; i++) rob.authority->request(robbedBy(kRivalId));
        rig.tick();
        run.check("cofre: as moedas guardadas nao saem", rig.session.profile()->coins() == vaultProtection(2));
    }
    {
        TempDir dir;
        seedProfile(dir.path(), 5, {}, {0, 1, 0, 0});
        RobRig rob(world, dir.path(), 0);
        Rig& rig = *rob.rig;
        rig.bird.flight().place(kFarAway, 0.0f, 0.0f);
        rob.authority->rival.position = kFarAway + glm::vec3(100, 0, 0);   // A direita de quem olha para -z.
        rob.authority->request(robbedBy(kRivalId));
        rig.tick();
        const GameplayView& view = rig.session.view();
        run.check("alarme: avisa e aponta o ladrao", view.alarmNow && rig.countFeedback(GameplayFeedback::NestAlarm) == 1 &&
                                                       view.thiefBearing > 1.4f && view.thiefBearing < 1.75f &&
                                                       std::abs(view.thiefDistance - 100.0f) < 1.0f);
        rig.ticks(static_cast<int>(alarmSeconds(1) / kFrame) + 5);
        run.check("alarme: dura 5 s mais 3 s por nivel", !rig.session.view().alarmNow && alarmSeconds(1) == 8.0f && alarmSeconds(0) == 0.0f);
    }
}

void testTreeHit(TestRun& run, const World& world) {
    TempDir dir;
    auto owned = std::make_unique<LocalAuthority>();
    const glm::vec3 cashew = kFarAway + glm::vec3(0, 0, -2);
    Rig rig(world, dir.path(), {cashew, cashew + glm::vec3(1, 0, 0)}, std::move(owned));
    rig.bird.flight().place(kFarAway, 0.0f, 0.0f);
    rig.ticks(3);
    rig.session.drainFeedback();

    const CashewTree& tree = world.trees().front();
    rig.bird.flight().place(tree.base() + glm::vec3(0, 6, 30), 0.0f, 40.0f);   // Em linha reta contra o tronco.
    bool hit = false;
    for (int i = 0; i < 120 && !hit; i++) {
        rig.tick();
        hit = rig.session.view().cargoLostNow;
    }
    const GameplayView& view = rig.session.view();
    run.check("arvore: bater com forca derruba um caju", hit && view.carried == 1 && view.cargoLossCause == CargoLossCause::Tree);
    run.check("arvore: a batida reduz a velocidade", rig.bird.flight().speed() < 30.0f);
    run.check("arvore: o caju cai no mundo", rig.field.activeLoose().size() == 1);
    rig.ticks(20);
    run.check("arvore: raspar na mesma batida nao derruba outro", rig.session.view().carried == 1);
}

int runNestSelfTest() {
    TestRun run;
    const World world(kSeed);
    testNestSite(run, world);
    testRivalSites(run, world);
    testRivalRoster(run, world);
    testVisualGrowth(run);
    testLandingAndDelivery(run, world);
    testMenu(run, world);
    testPurchases(run, world);
    testUpgradeEffects(run, world);
    testTakeOff(run, world);
    testPersistence(run, world);
    testRecordAndFeedback(run, world);
    testNestDirection(run, world);
    testFullLoop(run, world);
    testBodyControls(run, world);
    testHunterDrop(run, world);
    testRobbery(run, world);
    testRobberyLimits(run, world);
    testVictim(run, world);
    testTreeHit(run, world);
    return run.failures;
}

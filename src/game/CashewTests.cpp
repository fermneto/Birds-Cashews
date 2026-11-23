#include "game/CashewTests.h"
#include "core/TestRun.h"
#include "entity/PlayerBird.h"
#include "game/CashewCollector.h"
#include "game/CashewModel.h"
#include "gfx/Camera3D.h"
#include "shared/CashewSpill.h"
#include "shared/LocalAuthority.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <set>
#include <tuple>
#include <vector>

namespace {
constexpr uint32_t kSeed = 20261207;
constexpr float kFrame = 1.0f / 60.0f;
constexpr PlayerId kMe = 0, kRival = 1;

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

// Cajus em fila, a cada 100 m, para isolar a regra testada.
CashewField rowOfCashews(int count) {
    std::vector<CashewSpot> spots;
    for (int i = 0; i < count; i++) {
        CashewSpot spot;
        spot.position = glm::vec3(100.0f * i, 50, 0);
        spot.respawnDelay = 30.0f + i;
        spots.push_back(spot);
    }
    return CashewField(spots);
}

// Cajus todos juntos, para caberem no mesmo raio de coleta.
CashewField clusterOfCashews(int count) {
    std::vector<CashewSpot> spots;
    for (int i = 0; i < count; i++) {
        CashewSpot spot;
        spot.position = glm::vec3(static_cast<float>(i), 50, 0);
        spots.push_back(spot);
    }
    return CashewField(spots);
}

void settle(HeldAuthority& authority, CashewCollector& collector) {
    authority.confirmAll();
    for (const GameEvent& event : authority.pollEvents()) collector.handle(event);
}

float canopyDistance(glm::vec3 point, const SphereCollider& sphere) {
    const glm::vec3 delta = point - sphere.center;
    return glm::length(glm::vec3(delta.x, delta.y / CashewTree::kCanopySquash, delta.z));
}

bool isFree(const World& world, glm::vec3 point) {
    return !world.resolve(point, PlayerBird::kRadius).hit();
}

// Busca um caminho ate o ceu aberto usando somente a colisao real do mundo.
bool reachesOpenSky(const World& world, glm::vec3 start, float skyHeight) {
    constexpr float kGrid = 4.0f;
    constexpr int kMaxVisits = 20000;
    using Cell = std::tuple<int, int, int>;
    const auto lower = [](const Cell& a, const Cell& b) { return std::get<1>(a) < std::get<1>(b); };
    std::priority_queue<Cell, std::vector<Cell>, decltype(lower)> open(lower);
    std::set<Cell> seen = {Cell{0, 0, 0}};
    open.push(Cell{0, 0, 0});
    for (int visits = 0; !open.empty() && visits < kMaxVisits; visits++) {
        const auto [x, y, z] = open.top();
        open.pop();
        if (start.y + y * kGrid > skyHeight) return true;
        const std::array<Cell, 6> around = {Cell{x + 1, y, z}, Cell{x - 1, y, z}, Cell{x, y + 1, z},
                                            Cell{x, y - 1, z}, Cell{x, y, z + 1}, Cell{x, y, z - 1}};
        for (const Cell& next : around) {
            const glm::vec3 offset(std::get<0>(next), std::get<1>(next), std::get<2>(next));
            if (seen.insert(next).second && isFree(world, start + offset * kGrid)) open.push(next);
        }
    }
    return false;
}

void testGeneration(TestRun& run, const World& world) {
    const CashewField first(world, kSeed), second(world, kSeed), other(world, kSeed + 1);
    bool identical = first.size() == second.size() && first.size() > 0;
    for (size_t i = 0; identical && i < first.size(); i++)
        identical = first.spots()[i].position == second.spots()[i].position
                 && first.spots()[i].respawnDelay == second.spots()[i].respawnDelay;
    run.check("a mesma semente gera os mesmos cajus", identical);

    bool different = other.size() != first.size();
    for (size_t i = 0; !different && i < first.size(); i++) different = other.spots()[i].position != first.spots()[i].position;
    run.check("sementes diferentes geram cajus diferentes", different);

    bool delays = true;
    for (const CashewSpot& spot : first.spots()) delays = delays && spot.respawnDelay >= 30.0f && spot.respawnDelay <= 60.0f;
    run.check("cada caju volta entre 30 e 60 s", delays);
}

void testCountPerTree(TestRun& run) {
    bool inRange = true;
    for (uint32_t seed : {kSeed, kSeed + 1, kSeed + 2, 7u, 123456u}) {
        const World world(seed);
        const CashewField field(world, seed);
        std::vector<int> counts(world.trees().size(), 0);
        for (const CashewSpot& spot : field.spots()) counts[spot.tree]++;
        for (int count : counts) inRange = inRange && count >= 6 && count <= 10;
    }
    run.check("todo cajueiro tem de 6 a 10 cajus em varias sementes", inRange);
}

void testReachable(TestRun& run) {
    bool free = true, connected = true, outside = true, anchored = true;
    for (uint32_t seed : {kSeed, 7u, 123456u}) {
        const World world(seed);
        const CashewField field(world, seed);
        float skyHeight = 0;
        for (const CashewTree& tree : world.trees()) skyHeight = std::max(skyHeight, tree.base().y + tree.height() * 1.1f);

        for (const CashewSpot& spot : field.spots()) {
            free = free && isFree(world, spot.position);
            connected = connected && reachesOpenSky(world, spot.position, skyHeight);
            bool inCanopy = false;
            for (const CashewTree& tree : world.trees()) {
                for (const SphereCollider& sphere : tree.canopy()) {
                    outside = outside && canopyDistance(spot.position, sphere) >= sphere.radius * 1.12f;
                    inCanopy = inCanopy || canopyDistance(spot.stemAnchor, sphere) < sphere.radius * 0.88f;
                }
            }
            anchored = anchored && inCanopy;
        }
    }
    run.check("o passaro cabe no lugar de todo caju", free);
    run.check("todo caju tem caminho livre ate o ceu aberto", connected);
    run.check("nenhum caju fica enterrado na folhagem", outside);
    run.check("todo talo termina dentro de uma copa", anchored);
}

void testCollectRadius(TestRun& run) {
    CashewField field = rowOfCashews(1);
    HeldAuthority authority;
    CashewCollector collector(field, authority, kMe);
    const glm::vec3 cashew = field.positionOf(0);

    collector.update(kFrame, cashew + glm::vec3(9.5f, 0, 0));
    run.check("fora do raio de 9 m nada e pedido", authority.requests.empty());
    collector.update(kFrame, cashew + glm::vec3(8.5f, 0, 0));
    run.check("dentro do raio de 9 m o caju e pedido", authority.count(GameEventType::CashewCollected) == 1);
    run.check("o pedido identifica caju e jogador", authority.requests.size() == 1 && authority.requests[0].cashew == 0
              && authority.requests[0].actor == kMe && authority.requests[0].position == cashew);

    CashewField wide = rowOfCashews(1);
    HeldAuthority wideAuthority;
    CashewCollector upgraded(wide, wideAuthority, kMe);
    upgraded.stats().collectRadius = 20.0f;
    upgraded.update(kFrame, cashew + glm::vec3(15.0f, 0, 0));
    run.check("o raio vem dos atributos de carga", wideAuthority.count(GameEventType::CashewCollected) == 1);
}

void testCargoLimit(TestRun& run) {
    CashewField field = clusterOfCashews(6);
    HeldAuthority authority;
    CashewCollector collector(field, authority, kMe);
    const glm::vec3 center = field.positionOf(2);

    collector.update(kFrame, center);
    collector.update(kFrame, center);
    run.check("os pedidos em andamento respeitam a carga", authority.count(GameEventType::CashewCollected) == 3);
    settle(authority, collector);
    run.check("a carga chega ao limite de 3", collector.carried() == 3);

    collector.update(kFrame, center);
    run.check("com a carga cheia nada e pedido", authority.requests.empty());
    int active = 0;
    for (CashewId id = 0; id < 6; id++) active += field.isActive(id);
    run.check("com a carga cheia os outros cajus ficam no lugar", active == 3);

    CashewField bigField = clusterOfCashews(6);
    HeldAuthority bigAuthority;
    CashewCollector big(bigField, bigAuthority, kMe);
    big.stats().capacity = 5;
    big.update(kFrame, center);
    settle(bigAuthority, big);
    run.check("a capacidade vem dos atributos de carga", big.carried() == 5);

    // Uma resposta atrasada chega depois de o pedido expirar e outro caju ter sido pedido.
    CashewField lateField = rowOfCashews(2);
    HeldAuthority lateAuthority;
    CashewCollector late(lateField, lateAuthority, kMe);
    late.stats().capacity = 1;
    late.update(kFrame, lateField.positionOf(0));
    late.update(4.0f, glm::vec3(5000, 50, 0));
    late.update(kFrame, lateField.positionOf(1));
    run.check("o pedido expirado libera a reserva", lateAuthority.count(GameEventType::CashewCollected) == 2);
    settle(lateAuthority, late);
    run.check("resposta atrasada nao passa do limite de carga", late.carried() == 1);
}

void testConfirmation(TestRun& run) {
    CashewField field = rowOfCashews(1);
    HeldAuthority authority;
    CashewCollector collector(field, authority, kMe);

    collector.update(kFrame, field.positionOf(0));
    run.check("antes da confirmacao a carga nao muda", collector.carried() == 0);
    run.check("antes da confirmacao o caju continua no mundo", field.isActive(0));
    settle(authority, collector);
    run.check("a confirmacao aumenta a carga", collector.carried() == 1);
    run.check("a confirmacao retira o caju do mundo", !field.isActive(0));
}

void testDuplicateRequests(TestRun& run) {
    CashewField field = rowOfCashews(1);
    HeldAuthority authority;
    CashewCollector collector(field, authority, kMe);
    const glm::vec3 cashew = field.positionOf(0);

    for (int i = 0; i < 60; i++) collector.update(kFrame, cashew);
    run.check("o pedido pendente nao se repete", authority.count(GameEventType::CashewCollected) == 1);
    for (int i = 0; i < 180; i++) collector.update(kFrame, cashew);
    run.check("sem resposta o pedido e refeito depois do prazo", authority.count(GameEventType::CashewCollected) == 2);

    settle(authority, collector);
    run.check("confirmacao repetida conta uma unica vez", collector.carried() == 1);
}

void testContestedCashew(TestRun& run) {
    for (bool rivalFirst : {false, true}) {
        CashewField mine = rowOfCashews(2), theirs = rowOfCashews(2);
        LocalAuthority authority;
        CashewCollector me(mine, authority, kMe), rival(theirs, authority, kRival);
        CashewCollector& loser = rivalFirst ? me : rival;
        CashewField& loserField = rivalFirst ? mine : theirs;
        loser.stats().capacity = 1;   // Uma reserva esquecida bloquearia a proxima coleta.

        const glm::vec3 cashew = mine.positionOf(0);
        if (rivalFirst) rival.update(kFrame, cashew);
        me.update(kFrame, cashew);
        if (!rivalFirst) rival.update(kFrame, cashew);
        for (const GameEvent& event : authority.pollEvents()) {
            me.handle(event);
            rival.handle(event);
        }
        const std::string order = rivalFirst ? " (rival primeiro)" : " (local primeiro)";
        run.check("so o primeiro confirmado leva o caju" + order,
                  me.carried() == (rivalFirst ? 0 : 1) && rival.carried() == (rivalFirst ? 1 : 0));
        run.check("o caju some para os dois jogadores" + order, !mine.isActive(0) && !theirs.isActive(0));

        loser.update(kFrame, loserField.positionOf(1));
        for (const GameEvent& event : authority.pollEvents()) loser.handle(event);
        run.check("quem perdeu a disputa continua coletando" + order, loser.carried() == 1);
    }

    CashewField field = rowOfCashews(1);
    HeldAuthority authority;
    CashewCollector collector(field, authority, kMe);
    GameEvent stolen;
    stolen.type = GameEventType::CashewCollected;
    stolen.actor = kRival;
    stolen.cashew = 0;
    collector.handle(stolen);
    run.check("coleta de outro jogador nao soma na carga local", collector.carried() == 0 && !field.isActive(0));

    GameEvent invalid = stolen;
    invalid.cashew = 999;
    invalid.actor = kMe;
    collector.handle(invalid);
    run.check("evento de caju inexistente e ignorado", collector.carried() == 0);
}

void testRespawn(TestRun& run) {
    CashewField field = rowOfCashews(1);
    HeldAuthority authority;
    CashewCollector collector(field, authority, kMe);
    const glm::vec3 away(5000, 50, 0), cashew = field.positionOf(0);
    const float delay = field.spots()[0].respawnDelay;

    collector.update(kFrame, cashew);
    settle(authority, collector);

    GameEvent early;
    early.type = GameEventType::CashewRespawned;
    early.cashew = 0;
    collector.handle(early);
    run.check("aviso antes da hora nao traz o caju de volta", !field.isActive(0));

    for (float t = 0; t < delay - 1.0f; t += 0.1f) collector.update(0.1f, away);
    run.check("antes do tempo o caju nao pede para voltar", authority.count(GameEventType::CashewRespawned) == 0);
    for (float t = 0; t < 1.5f; t += 0.1f) collector.update(0.1f, away);
    run.check("vencido o tempo o reaparecimento e pedido uma vez", authority.count(GameEventType::CashewRespawned) == 1);
    run.check("o caju so volta com a confirmacao", !field.isActive(0));
    settle(authority, collector);
    run.check("o caju confirmado volta ao mundo", field.isActive(0));

    collector.update(kFrame, cashew);
    settle(authority, collector);
    run.check("o caju que voltou pode ser coletado de novo", collector.carried() == 2 && !field.isActive(0));
}

void testQueries(TestRun& run) {
    CashewField field = rowOfCashews(3);
    const CashewQuery& query = field;
    run.check("positionOf devolve a posicao do caju", query.positionOf(2) == glm::vec3(200, 50, 0));
    run.check("positionOf de id invalido devolve a origem", query.positionOf(77) == glm::vec3(0));
    run.check("nearest escolhe o caju mais proximo", query.nearest({90, 50, 0}, 50.0f) == CashewId(1));
    run.check("nearest respeita o raio", !query.nearest({50, 500, 0}, 50.0f).has_value());
    field.markCollected(1);
    run.check("nearest ignora caju coletado", query.nearest({90, 50, 0}, 500.0f) == CashewId(0));
}

void testBeak(TestRun& run) {
    CashewField field = rowOfCashews(1);
    HeldAuthority authority;
    CashewCollector collector(field, authority, kMe);
    const glm::vec3 cashew = field.positionOf(0);
    run.check("o bico quer abrir com caju a 15 m", collector.beakTarget(cashew + glm::vec3(15, 0, 0)) == 1.0f);
    run.check("o bico fica fechado com caju a 25 m", collector.beakTarget(cashew + glm::vec3(25, 0, 0)) == 0.0f);

    CashewField fullField = clusterOfCashews(5);
    HeldAuthority fullAuthority;
    CashewCollector full(fullField, fullAuthority, kMe);
    full.update(kFrame, fullField.positionOf(1));
    settle(fullAuthority, full);
    run.check("com a carga cheia o bico nao abre", full.carried() == 3 && full.beakTarget(fullField.positionOf(4)) == 0.0f);

    PlayerBird bird;
    bird.setBeakTarget(BeakSource::Cashew, 1.0f);
    bird.animate(kFrame);
    const float firstFrame = bird.animation().beakOpen;
    for (int i = 0; i < 60; i++) bird.animate(kFrame);
    run.check("o bico abre suavemente", firstFrame > 0.0f && firstFrame < 0.5f && bird.animation().beakOpen > 0.95f);

    bird.setBeakTarget(BeakSource::Voice, 0.4f);
    bird.setBeakTarget(BeakSource::Cashew, 0.0f);
    bird.animate(kFrame);
    const float closing = bird.animation().beakOpen;
    for (int i = 0; i < 180; i++) bird.animate(kFrame);
    run.check("o bico fecha suavemente", closing > 0.8f);
    run.check("vale a maior abertura entre as fontes", std::abs(bird.animation().beakOpen - 0.4f) < 0.02f);
}

void testVisibility(TestRun& run) {
    std::vector<CashewSpot> spots(4);
    spots[0].position = glm::vec3(0, 50, -100);    // A frente da camera.
    spots[1].position = glm::vec3(0, 50, 100);     // Atras da camera.
    spots[2].position = glm::vec3(0, 50, -1500);   // A frente, mas longe demais.
    spots[3].position = glm::vec3(5, 50, -120);    // A frente, porem ja coletado.
    CashewField field(spots);
    field.markCollected(3);
    Camera3D camera;
    camera.position = glm::vec3(0, 50, 0);
    const glm::mat4 viewProjection = camera.viewProjection();
    const std::vector<glm::mat4> visible = visibleCashews(field, viewProjection, camera.position, 0.0f, kCashewDrawDistance);
    run.check("so os cajus ativos, proximos e a frente sao desenhados",
              visible.size() == 1 && glm::vec3(visible[0][3]) == spots[0].position);
    run.check("cada caju cai em uma unica faixa de detalhe",
              visibleCashews(field, viewProjection, camera.position, 0.0f, 100.5f).size() == 1
              && visibleCashews(field, viewProjection, camera.position, 100.5f, kCashewDrawDistance).empty());
    run.check("o modelo distante e bem mais leve",
              buildCashewFarMesh().vertices().size() * 3 < buildCashewMesh().vertices().size());
}

// Procura um caju com aproximacao reta e livre, vinda de fora da arvore.
bool findApproach(const World& world, const CashewField& field, CashewId& id, glm::vec3& start) {
    for (size_t i = 0; i < field.size(); i++) {
        const CashewSpot& spot = field.spots()[i];
        const glm::vec3 base = world.trees()[spot.tree].base();
        const glm::vec3 outward = glm::normalize(glm::vec3(spot.position.x - base.x, 0, spot.position.z - base.z));
        bool clear = true;
        for (float d = 0; clear && d <= 70.0f; d += 2.0f)
            for (float rise : {-3.0f, 0.0f, 4.0f}) clear = clear && isFree(world, spot.position + outward * d + glm::vec3(0, rise, 0));
        if (!clear) continue;
        id = static_cast<CashewId>(i);
        start = spot.position + outward * 70.0f + glm::vec3(0, 4, 0);
        return true;
    }
    return false;
}

void testFlightCollects(TestRun& run, const World& world) {
    CashewField field(world, kSeed);
    LocalAuthority authority;
    CashewCollector collector(field, authority, kMe);
    CashewId target = 0;
    glm::vec3 start(0);
    const bool found = findApproach(world, field, target, start);
    run.check("existe uma aproximacao livre ate um caju", found);
    if (!found) return;

    const glm::vec3 toCashew = field.positionOf(target) - start;
    PlayerBird bird;
    bird.spawn(start, std::atan2(toCashew.x, -toCashew.z));
    float closest = 1e9f, beakPeak = 0;
    for (int frame = 0; frame < 300 && collector.carried() == 0; frame++) {
        for (int i = 0; i < 2; i++) bird.step(world, 1.0f / 120.0f);
        for (const GameEvent& event : authority.pollEvents()) collector.handle(event);
        const glm::vec3 position = bird.flight().position();
        collector.update(kFrame, position);
        bird.setBeakTarget(BeakSource::Cashew, collector.beakTarget(position));
        bird.animate(kFrame);
        closest = std::min(closest, glm::length(position - field.positionOf(target)));
        beakPeak = std::max(beakPeak, bird.animation().beakOpen);
    }
    run.check("o voo real coleta o caju pelo fluxo da autoridade", collector.carried() == 1 && !field.isActive(target));
    run.check("o voo passa dentro do raio de coleta", closest <= collector.stats().collectRadius);
    run.check("o bico abre durante a aproximacao", beakPeak > 0.5f);
}

GameEvent dropOf(PlayerId target, int value, glm::vec3 position, PlayerId actor = kNoPlayer) {
    GameEvent event;
    event.type = GameEventType::CashewDropped;
    event.actor = actor;
    event.target = target;
    event.value = static_cast<uint8_t>(value);
    event.position = position;
    return event;
}

void testSpillPool(TestRun& run) {
    CashewSpill pool;
    const std::vector<GameEvent> three = pool.spill(dropOf(kMe, 3, glm::vec3(10, 50, 10)));
    std::set<CashewId> ids;
    bool loose = true, around = true;
    for (const GameEvent& e : three) {
        ids.insert(e.cashew);
        loose = loose && e.type == GameEventType::CashewSpilled && isLooseCashew(e.cashew) && e.target == kMe;
        around = around && glm::length(glm::vec2(e.position.x - 10, e.position.z - 10)) < 6.0f && e.position.y < 50;
    }
    run.check("caju solto: um evento por caju derrubado, com ids proprios", three.size() == 3 && ids.size() == 3 && loose);
    run.check("caju solto: os cajus saem em volta e abaixo do passaro", around);
    run.check("caju solto: so o primeiro pedido leva", pool.take(three[0].cashew) && !pool.take(three[0].cashew));
    run.check("caju solto: id fixo nunca e solto", !pool.take(5) && pool.activeCount() == 2);
    run.check("caju solto: quem entra recebe os que estao no chao", pool.activeSpills().size() == 2);
    pool.advance(kLooseCashewSeconds + 3.0f);
    run.check("caju solto: a vaga e liberada depois que o caju some", pool.activeCount() == 0 && !pool.take(three[1].cashew));

    CashewSpill full;
    for (int i = 0; i < kLooseCashewSlots; i++) full.spill(dropOf(kMe, 1, glm::vec3(0)));
    const std::vector<GameEvent> extra = full.spill(dropOf(kMe, 1, glm::vec3(0)));
    run.check("caju solto: sem vaga, reaproveita a do mais antigo",
              extra.size() == 1 && extra[0].cashew == kLooseCashewBase && full.activeCount() == kLooseCashewSlots);
}

void testLooseCashewField(TestRun& run, const World& world) {
    CashewField field(world, kSeed);
    const CashewTree& tree = world.trees().front();
    const glm::vec3 from = tree.base() + glm::vec3(tree.reach() + 40.0f, 80.0f, 0);   // Ao lado da copa, sobre o chao.
    const CashewId id = kLooseCashewBase + 3;
    run.check("caju solto: comeca inativo", !field.isActive(id) && field.contains(id) && field.activeLoose().empty());
    field.spill(id, from);
    run.check("caju solto: aparece onde saiu", field.isActive(id) && glm::length(field.positionOf(id) - from) < 1e-3f);
    field.advance(0.5f);
    const glm::vec3 falling = field.positionOf(id);
    field.advance(6.0f);
    const glm::vec3 rest = field.positionOf(id);
    glm::vec3 probe = rest - glm::vec3(0, 3.0f, 0);
    run.check("caju solto: cai ate o chao", falling.y < from.y && rest.y < falling.y && world.resolve(probe, 3.0f).hit());
    glm::vec3 clear = rest;
    run.check("caju solto: fica apoiado sem afundar", !world.resolve(clear, 2.0f).hit());
    Camera3D camera;
    camera.position = rest + glm::vec3(0, 0, 60);   // Olhando para -z, com o caju a frente.
    const std::vector<glm::mat4> drawn = visibleCashews(field, camera.viewProjection(), camera.position, 0.0f, kCashewDrawDistance);
    const bool shown = std::any_of(drawn.begin(), drawn.end(), [&](const glm::mat4& m) { return glm::length(glm::vec3(m[3]) - rest) < 1e-3f; });
    run.check("caju solto: aparece na busca e no desenho", field.nearest(rest, 5.0f) == std::optional<CashewId>(id) && shown);
    run.check("caju solto: nao volta depois de coletado",
              field.markCollected(id) && !field.isActive(id) && !field.forceRespawn(id) && field.dueForRespawn().size() == 0);

    field.spill(id, from);
    field.advance(kLooseCashewSeconds + 0.1f);
    run.check("caju solto: some sozinho depois do tempo", !field.isActive(id));
    field.spill(id, from);
    const size_t fixedCount = field.size();
    run.check("caju solto: nao conta entre os cajus fixos", fixedCount == CashewField(world, kSeed).size());
}

void testStolenCargo(TestRun& run) {
    CashewField field({CashewSpot{glm::vec3(0)}, CashewSpot{glm::vec3(1, 0, 0)}});
    HeldAuthority authority;
    CashewCollector cargo(field, authority, kMe);
    cargo.handle({GameEventType::CashewCollected, kMe, kNoPlayer, 0, 1});
    GameEvent robbed{GameEventType::NestRobbed, kMe, kRival, kNoCashew, 1};
    cargo.handle(robbed);
    cargo.handle(robbed);
    run.check("roubo: cada caju roubado entra na carga marcado", cargo.carried() == 3 && cargo.stolen() == 2);
    cargo.handle(robbed);
    run.check("roubo: carga cheia nao recebe mais", cargo.carried() == 3 && cargo.stolen() == 2);
    GameEvent other = robbed;
    other.actor = kRival;
    other.target = kMe;
    cargo.handle(other);
    run.check("roubo: o roubo de outro jogador nao mexe na carga", cargo.carried() == 3);

    cargo.handle(dropOf(kMe, 1, glm::vec3(0)));
    run.check("roubo: na queda o roubado sai primeiro", cargo.carried() == 2 && cargo.stolen() == 1);
    int stolen = -1;
    run.check("roubo: na entrega o roubado sai por ultimo", cargo.unload(1, &stolen) == 1 && stolen == 0 && cargo.stolen() == 1);
    run.check("roubo: a entrega informa os roubados", cargo.unload(5, &stolen) == 1 && stolen == 1 && cargo.stolen() == 0);

    GameEvent own{GameEventType::CashewSpilled, kMe, kMe, kLooseCashewBase + 1, 1, glm::vec3(-100, 0, 0)};
    cargo.handle(own);
    cargo.update(kFrame, glm::vec3(-100, 0, 0));
    const bool waits = authority.requests.empty();
    for (int i = 0; i < 130; i++) cargo.update(kFrame, glm::vec3(-100, 0, 0));
    run.check("caju solto: quem derrubou espera 2 s para pegar de volta",
              waits && authority.count(GameEventType::CashewCollected) == 1);
    authority.requests.clear();

    GameEvent spilled{GameEventType::CashewSpilled, kNoPlayer, kRival, kLooseCashewBase, 1, glm::vec3(100, 0, 0)};
    cargo.handle(spilled);
    run.check("caju solto: o evento poe o caju no campo", field.isActive(kLooseCashewBase));
    cargo.update(kFrame, glm::vec3(100, 0, 0));
    authority.confirmAll();
    for (const GameEvent& event : authority.pollEvents()) cargo.handle(event);
    run.check("caju solto: qualquer um coleta como caju comum",
              cargo.carried() == 1 && cargo.stolen() == 0 && !field.isActive(kLooseCashewBase));
}

void testWorldRestore(TestRun& run) {
    std::vector<CashewSpot> spots(4);
    for (auto& spot : spots) spot.respawnDelay = 40.0f;
    CashewField field(spots);
    field.markCollected(1);
    field.advance(15.0f);
    field.markCollected(3);
    field.spill(kLooseCashewBase, glm::vec3(0));
    const std::vector<UnavailableCashew> saved = field.unavailable();
    run.check("mundo salvo: guarda os cajus fixos que faltam e o tempo de cada um",
              saved.size() == 2 && saved[0].id == 1 && std::abs(saved[0].secondsLeft - 25.0f) < 1e-3f && saved[1].id == 3 &&
                  std::abs(saved[1].secondsLeft - 40.0f) < 1e-3f);

    CashewField reopened(spots);
    reopened.restoreUnavailable(saved);
    reopened.restoreUnavailable({{kLooseCashewBase, 5.0f}, {200, 5.0f}});
    run.check("mundo salvo: ao reabrir os mesmos cajus continuam colhidos",
              !reopened.isActive(1) && !reopened.isActive(3) && reopened.isActive(0) && reopened.activeLoose().empty());
    reopened.advance(26.0f);
    run.check("mundo salvo: cada caju volta no tempo que faltava",
              reopened.dueForRespawn() == std::vector<CashewId>{1} && reopened.markRespawned(1) && !reopened.markRespawned(3));
}

void testLocalAuthoritySpill(TestRun& run) {
    LocalAuthority authority;
    authority.request(dropOf(kMe, 2, glm::vec3(0, 50, 0)));
    run.check("autoridade local: sem carga nada cai", authority.pollEvents().empty());
    authority.request({GameEventType::CashewCollected, kMe, kNoPlayer, 0, 1});
    authority.request({GameEventType::CashewCollected, kMe, kNoPlayer, 1, 1});
    authority.pollEvents();
    authority.request(dropOf(kMe, 5, glm::vec3(0, 50, 0)));
    const std::vector<GameEvent> events = authority.pollEvents();
    const long spilled = std::count_if(events.begin(), events.end(), [](const GameEvent& e) { return e.type == GameEventType::CashewSpilled; });
    run.check("autoridade local: derruba so o que o jogador carrega",
              !events.empty() && events[0].type == GameEventType::CashewDropped && events[0].value == 2 && spilled == 2);
    const CashewId loose = events[1].cashew;
    authority.request({GameEventType::CashewCollected, kNoPlayer, kNoPlayer, loose, 1});
    authority.request({GameEventType::CashewCollected, kMe, kNoPlayer, loose, 1});
    const std::vector<GameEvent> taken = authority.pollEvents();
    run.check("autoridade local: o caju solto vai para um so", taken.size() == 1 && taken[0].actor == kNoPlayer);
    authority.request({GameEventType::CashewSpilled, kMe, kMe, kLooseCashewBase + 9, 1});
    run.check("autoridade local: ninguem cria caju solto por pedido", authority.pollEvents().empty());
}
}

int runCashewSelfTest() {
    TestRun run;
    const World world(kSeed);
    testGeneration(run, world);
    testCountPerTree(run);
    testReachable(run);
    testCollectRadius(run);
    testCargoLimit(run);
    testConfirmation(run);
    testDuplicateRequests(run);
    testContestedCashew(run);
    testRespawn(run);
    testQueries(run);
    testBeak(run);
    testVisibility(run);
    testFlightCollects(run, world);
    testSpillPool(run);
    testLooseCashewField(run, world);
    testStolenCargo(run);
    testLocalAuthoritySpill(run);
    testWorldRestore(run);
    return run.failures;
}

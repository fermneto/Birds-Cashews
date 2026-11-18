#include "shared/ContractTests.h"
#include "core/TestRun.h"
#include "shared/BirdFactory.h"
#include "shared/CashewQuery.h"
#include "shared/LocalAuthority.h"
#include "shared/NetSession.h"
#include <type_traits>

namespace {
static_assert(std::is_abstract_v<GameAuthority> && std::has_virtual_destructor_v<GameAuthority>);
static_assert(std::is_abstract_v<CashewQuery> && std::has_virtual_destructor_v<CashewQuery>);
static_assert(std::is_abstract_v<NetSession> && std::has_virtual_destructor_v<NetSession>);
static_assert(std::is_base_of_v<GameAuthority, LocalAuthority> && !std::is_abstract_v<LocalAuthority>);
static_assert(std::is_same_v<decltype(&buildBirdMesh), MeshBuilder (*)(uint32_t)>);

GameEvent eventOf(GameEventType type, PlayerId actor, CashewId cashew = kNoCashew) {
    GameEvent event;
    event.type = type;
    event.actor = actor;
    event.cashew = cashew;
    return event;
}

PlayerPublicState playerOf(PlayerId id, uint8_t carried = 0) {
    PlayerPublicState state;
    state.id = id;
    state.carried = carried;
    return state;
}

void testEvents(TestRun& run) {
    LocalAuthority authority;
    GameAuthority& rules = authority;
    run.check("sem pedidos nao ha eventos", rules.pollEvents().empty());

    GameEvent delivered = eventOf(GameEventType::CashewDelivered, 1, 7);
    delivered.target = 2;
    delivered.value = 3;
    delivered.position = glm::vec3(1, 2, 3);
    rules.request(eventOf(GameEventType::CashewCollected, 1, 7));
    rules.request(delivered);

    const std::vector<GameEvent> events = rules.pollEvents();
    run.check("os eventos chegam na ordem dos pedidos", events.size() == 2
              && events[0].type == GameEventType::CashewCollected && events[1].type == GameEventType::CashewDelivered);
    run.check("o evento preserva todos os campos", events.size() == 2 && events[1].actor == 1 && events[1].target == 2
              && events[1].cashew == 7 && events[1].value == 3 && events[1].position == glm::vec3(1, 2, 3));
    run.check("cada evento e entregue uma unica vez", rules.pollEvents().empty());
}

void testPlayers(TestRun& run) {
    LocalAuthority rules;
    rules.publishLocalPlayer(playerOf(0, 1));
    rules.publishLocalPlayer(playerOf(0, 3));
    run.check("publicar de novo atualiza o mesmo jogador", rules.players().size() == 1 && rules.players()[0].carried == 3);

    rules.publishLocalPlayer(playerOf(1));
    run.check("um segundo jogador simulado entra na lista", rules.players().size() == 2);

    rules.request(eventOf(GameEventType::PlayerLeft, 0));
    run.check("quem sai deixa a lista de jogadores", rules.players().size() == 1 && rules.players()[0].id == 1);
    run.check("a saida tambem vira evento", rules.pollEvents().size() == 1);
}
}

int runContractSelfTest() {
    TestRun run;
    testEvents(run);
    testPlayers(run);
    return run.failures;
}

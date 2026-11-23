#include "game/GameplaySession.h"
#include "game/NestEconomy.h"
#include "shared/LocalAuthority.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

namespace {
constexpr float kLandingRadius = 16.0f;     // Distancia do poleiro que inicia o pouso.
constexpr float kRearmRadius = 45.0f;       // Depois de decolar, precisa se afastar ate aqui para pousar de novo.
constexpr float kLandingSeconds = 0.6f;     // Duracao do pouso; gestos nesse intervalo sao ignorados.
constexpr float kTakeoffSpeedFactor = 1.2f; // Velocidade de saida em relacao ao cruzeiro.
constexpr float kTakeoffFlap = 1.4f;        // Forca da batida que tira o passaro do ninho.
constexpr float kPerchPitch = -0.42f;       // Pousado, o passaro olha para dentro do ninho.
constexpr float kRecordNoticeSeconds = 6.0f;
constexpr float kCargoLostNoticeSeconds = 3.0f;
constexpr float kTreeHitImpact = 6.0f;      // m/s contra a copa ou o tronco para soltar um caju.
constexpr float kTreeHitCooldown = 1.5f;    // Uma batida longa raspando na copa so derruba um caju.
constexpr float kRobAnswerSeconds = 3.0f;   // Sem resposta da autoridade, tenta de novo.
constexpr float kFullTurn = 6.2831853f;

std::unique_ptr<GameAuthority> orLocal(std::unique_ptr<GameAuthority> authority) {
    if (authority) return authority;
    return std::make_unique<LocalAuthority>();
}

// O mesmo nome sempre gera o mesmo passaro (FNV-1a).
uint32_t birdSeedFor(const std::string& name) {
    uint32_t hash = 2166136261u;
    for (const char c : name) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    return hash;
}
}

GameplaySession::GameplaySession(const World& world, uint32_t seed, CashewField& cashews, PlayerBird& bird,
                                 const std::filesystem::path& saveDir, PlayerId localPlayer,
                                 std::unique_ptr<GameAuthority> authority)
    : world_(world), bird_(bird), localPlayer_(localPlayer), authority_(orLocal(std::move(authority))),
      collector_(cashews, *authority_, localPlayer), nest_(chooseNestSite(world, seed, localPlayer)),
      rivals_(world, seed),
      store_(saveDir), records_(saveDir), perchSite_(nest_) {
    refreshView();
}

void GameplaySession::startProfile(const std::string& name) {
    if (!isValidProfileName(name)) {
        std::cerr << "Nome de perfil invalido\n";
        return;
    }
    saveNow();
    releasePerch();
    collector_.unload(collector_.carried());
    deliveryPending_ = false;
    recordNotice_ = 0;

    std::optional<Profile> loaded = store_.load(name);
    if (!loaded) loaded = store_.create(name, birdSeedFor(name));
    if (!loaded) {
        std::cerr << "Nao foi possivel gravar o perfil; o progresso desta partida nao sera salvo\n";
        loaded = Profile(name, birdSeedFor(name));
    }
    profile_ = std::move(loaded);
    records_.startSession(profile_->name(), profile_->totalDelivered());
    applyUpgrades();
    refreshView();
}

void GameplaySession::saveNow() {
    if (profile_) store_.save(*profile_);
}

std::vector<GameplayFeedback> GameplaySession::drainFeedback() {
    std::vector<GameplayFeedback> drained;
    drained.swap(feedback_);
    return drained;
}

void GameplaySession::applyControls(const FlightControls& controls) {
    flightMode_ = perched_ || controls.mode == FlightMode::Lost ? FlightMode::Glide : controls.mode;
    if (!perched_) {
        bird_.applyControls(controls);
        return;
    }
    menuArm_ = controls.raisedArm;
    menuHold_ = controls.menuProgress;
    bird_.applyControls(FlightControls{});
    if (settle_ >= kLandingSeconds) {
        if (controls.flaps > 0) takeOff();
        else if (perchRival_ == kNoPlayer) handleMenuPress(controls.menuPressed);
    }
    refreshView();
}

void GameplaySession::stepBird(float dt) {
    if (perched_) return;
    bird_.step(world_, dt);
    checkTreeHit();
}

// Bater com forca numa copa ou tronco solta um caju, que cai no mundo. A fisica ja reduziu a velocidade.
void GameplaySession::checkTreeHit() {
    const float impact = bird_.takeSolidImpact();
    if (impact < kTreeHitImpact || treeCooldown_ > 0 || collector_.carried() <= 0) return;
    treeCooldown_ = kTreeHitCooldown;
    GameEvent event;
    event.type = GameEventType::CashewDropped;
    event.actor = localPlayer_;
    event.target = localPlayer_;
    event.value = 1;
    event.position = bird_.flight().position();
    authority_->request(event);
}

// Todo evento confirmado passa por aqui, porque pollEvents entrega cada um uma unica vez.
void GameplaySession::update(float frameSeconds) {
    players_ = authority_->players();
    for (const GameEvent& event : authority_->pollEvents()) handleEvent(event);
    rivals_.update(players_, localPlayer_);
    recordNotice_ = std::max(0.0f, recordNotice_ - frameSeconds);
    cargoLostNotice_ = std::max(0.0f, cargoLostNotice_ - frameSeconds);
    knockedNotice_ = std::max(0.0f, knockedNotice_ - frameSeconds);
    treeCooldown_ = std::max(0.0f, treeCooldown_ - frameSeconds);
    alarm_ = std::max(0.0f, alarm_ - frameSeconds);
    if (perched_) holdPerch(frameSeconds);
    else watchForLanding();

    const glm::vec3 position = bird_.flight().position();
    collector_.setCollecting(!perched_);
    collector_.update(frameSeconds, position);
    bird_.setBeakTarget(BeakSource::Cashew, perched_ ? 0.0f : collector_.beakTarget(position));
    refreshView();
}

void GameplaySession::handleEvent(const GameEvent& event) {
    const int carriedBefore = collector_.carried();
    collector_.handle(event);
    if (event.type == GameEventType::CashewCollected && collector_.carried() > carriedBefore)
        feedback_.push_back(GameplayFeedback::CashewCollected);
    if (event.type == GameEventType::CashewDropped) handleCargoDrop(event, carriedBefore);
    if (event.type == GameEventType::NestRobbed) {
        if (event.actor == localPlayer_ && collector_.carried() > carriedBefore) {
            stolenThisVisit_++;
            feedback_.push_back(GameplayFeedback::CashewStolen);
        }
        handleRobbery(event);
    }
    if (event.type == GameEventType::CashewDelivered && event.actor == localPlayer_) confirmDelivery(event);
}

// A carga cai por tres motivos: cacador (NPC), mergulho de outro jogador ou batida em arvore.
void GameplaySession::handleCargoDrop(const GameEvent& event, int carriedBefore) {
    if (event.target == localPlayer_ && collector_.carried() < carriedBefore) {
        lossCause_ = event.actor == kNoPlayer      ? CargoLossCause::Hunter
                     : event.actor == localPlayer_ ? CargoLossCause::Tree
                                                   : CargoLossCause::Player;
        feedback_.push_back(GameplayFeedback::CargoLost);
        cargoLostNotice_ = kCargoLostNoticeSeconds;
    }
    if (event.actor == localPlayer_ && event.target != localPlayer_ && event.target < kMaxPlayers) {
        feedback_.push_back(GameplayFeedback::RivalKnocked);
        knockedNotice_ = kCargoLostNoticeSeconds;
    }
}

void GameplaySession::handleRobbery(const GameEvent& event) {
    if (event.actor == localPlayer_) robPending_ = false;
    if (event.target == localPlayer_ && event.actor != localPlayer_) sufferRobbery(event);
}

// O perfil e local: cada computador tira a moeda do proprio dono. O Cofre guarda parte do saldo.
void GameplaySession::sufferRobbery(const GameEvent& event) {
    if (!profile_) return;
    if (robbableCoins(profile_->coins(), profile_->level(NestUpgrade::Vault)) > 0 && profile_->spendCoins(1)) saveNow();
    const float seconds = alarmSeconds(profile_->level(NestUpgrade::Alarm));
    if (seconds <= 0) return;
    alarm_ = seconds;
    thief_ = event.actor;
    feedback_.push_back(GameplayFeedback::NestAlarm);
}

const PlayerPublicState* GameplaySession::findPlayer(PlayerId id) const {
    for (const PlayerPublicState& player : players_)
        if (player.id == id) return &player;
    return nullptr;
}

// No ninho o jogador aparece sem carga, porque os cacadores nao atacam quem esta pousado.
void GameplaySession::publishLocalPlayer() {
    const FlightModel& flight = bird_.flight();
    PlayerPublicState state;
    state.id = localPlayer_;
    state.position = flight.position();
    state.velocity = perched_ ? glm::vec3(0) : flight.velocity();
    state.yaw = flight.yaw();
    state.pitch = flight.pitch();
    state.roll = flight.roll();
    state.mode = perched_ ? FlightMode::Glide : flightMode_;
    state.carried = perched_ ? 0 : static_cast<uint8_t>(std::min(collector_.carried(), 255));
    state.nestLevel = static_cast<uint8_t>(nestSizeLevel());
    if (profile_) {
        state.birdSeed = profile_->birdSeed();
        state.nestDefense = static_cast<uint8_t>(profile_->level(NestUpgrade::Defense));
        const int stock = robbableCoins(profile_->coins(), profile_->level(NestUpgrade::Vault));
        state.nestStock = static_cast<uint8_t>(std::min(stock, 255));
    }
    authority_->publishLocalPlayer(state);
}

// Pedidos em andamento impedem entregar a mesma carga duas vezes.
void GameplaySession::requestDelivery() {
    if (deliveryPending_ || collector_.carried() <= 0) return;
    deliveryPending_ = true;
    GameEvent event;
    event.type = GameEventType::CashewDelivered;
    event.actor = localPlayer_;
    event.value = static_cast<uint8_t>(std::min(collector_.carried(), 255));
    event.position = nest_.perch;
    authority_->request(event);
}

// O bonus entra como "roubado" em deliverCashews: vira moeda, mas nao soma na pontuacao.
void GameplaySession::confirmDelivery(const GameEvent& event) {
    if (!deliveryPending_) return;
    deliveryPending_ = false;
    int stolen = 0;
    const int delivered = collector_.unload(event.value, &stolen);
    if (delivered <= 0 || !profile_) return;

    // Roubados e bonus viram moeda, mas so os cajus colhidos somam na pontuacao.
    const int bonus = sizeCoinBonus(delivered, profile_->level(NestUpgrade::Size));
    profile_->deliverCashews(delivered + bonus, bonus + stolen);
    feedback_.push_back(GameplayFeedback::CashewDelivered);

    // O primeiro total de um computador sem record nao ultrapassa ninguem.
    const bool hadRecord = records_.record() > 0;
    if (records_.update(profile_->name(), profile_->totalDelivered()) && hadRecord) {
        feedback_.push_back(GameplayFeedback::RecordBeaten);
        recordNotice_ = kRecordNoticeSeconds;
    }
    saveNow();
}

// Depois de decolar, o pouso so volta a valer quando o passaro se afasta do ninho de onde saiu.
// Pousa no proprio ninho e tambem nos ninhos dos rivais, onde rouba.
void GameplaySession::watchForLanding() {
    const glm::vec3 position = bird_.flight().position();
    if (!landingArmed_) {
        landingArmed_ = glm::length(perchSite_.perch - position) > kRearmRadius;
        return;
    }
    if (!profile_) return;
    if (glm::length(nest_.perch - position) <= kLandingRadius) return land(nest_, kNoPlayer);
    for (const RivalNest& rival : rivals_.nests())
        if (glm::length(rival.site.perch - position) <= kLandingRadius) return land(rival.site, rival.owner);
}

void GameplaySession::land(const NestSite& site, PlayerId owner) {
    FlightModel& flight = bird_.flight();
    perched_ = true;
    perchSite_ = site;
    perchRival_ = owner;
    robTimer_ = robWait_ = 0;
    robPending_ = false;
    stolenThisVisit_ = 0;
    settle_ = 0;
    landingFrom_ = flight.position();
    landingYaw_ = flight.yaw();
    landingPitch_ = flight.pitch();
    landingRoll_ = flight.roll();
    bird_.applyControls(FlightControls{});
    if (owner == kNoPlayer) menu_.open();
    feedback_.push_back(GameplayFeedback::NestLanded);
}

// Desliza ate o poleiro e fica parado. Cajus confirmados depois do pouso tambem sao entregues.
void GameplaySession::holdPerch(float dt) {
    settle_ = std::min(settle_ + dt, kLandingSeconds);
    const float t = glm::smoothstep(0.0f, kLandingSeconds, settle_);
    FlightModel& flight = bird_.flight();
    flight.place(glm::mix(landingFrom_, perchSite_.perch, t),
                 landingYaw_ + std::remainder(perchSite_.yaw - landingYaw_, kFullTurn) * t, 0.0f);
    flight.setAttitude(glm::mix(landingPitch_, kPerchPitch, t), landingRoll_ * (1.0f - t));
    if (perchRival_ == kNoPlayer) requestDelivery();
    else robTick(dt);
}

// Cada volta completa da barra pede 1 caju a autoridade. A Defesa do dono deixa a volta mais longa.
void GameplaySession::robTick(float dt) {
    if (!rivals_.find(perchRival_)) {
        takeOff();   // O dono saiu da sala e o ninho sumiu.
        return;
    }
    if (settle_ < kLandingSeconds) return;
    if (robPending_) {
        robWait_ += dt;
        if (robWait_ >= kRobAnswerSeconds) robPending_ = false;
        return;
    }
    const PlayerPublicState* owner = findPlayer(perchRival_);
    if (!owner || owner->nestStock == 0 || collector_.freeSpace() <= 0) {
        robTimer_ = 0;
        return;
    }
    robTimer_ += dt;
    if (robTimer_ < robSeconds(owner->nestDefense)) return;
    robTimer_ = 0;
    robWait_ = 0;
    robPending_ = true;
    GameEvent event;
    event.type = GameEventType::NestRobbed;
    event.actor = localPlayer_;
    event.target = perchRival_;
    event.value = 1;
    event.position = perchSite_.perch;
    authority_->request(event);
}

// A batida de saida usa o mesmo caminho das batidas normais, com a forca da decolagem.
void GameplaySession::takeOff() {
    releasePerch();
    landingArmed_ = false;
    FlightModel& flight = bird_.flight();
    flight.place(perchSite_.perch, perchSite_.yaw, FlightModel::kCruiseSpeed * flight.stats().speed * kTakeoffSpeedFactor);
    FlightControls launch;
    launch.mode = FlightMode::Flap;
    launch.flaps = 1;
    launch.flapStrength = kTakeoffFlap;
    bird_.applyControls(launch);
    feedback_.push_back(GameplayFeedback::TakeOff);
}

void GameplaySession::releasePerch() {
    perched_ = false;
    perchRival_ = kNoPlayer;
    robPending_ = false;
    menu_.close();
    menuArm_ = ArmSide::None;
    menuHold_ = 0;
}

void GameplaySession::handleMenuPress(ArmSide side) {
    const NestMenuAction action = menu_.press(side);
    if (action == NestMenuAction::Moved) feedback_.push_back(GameplayFeedback::MenuMove);
    if (action == NestMenuAction::Buy) buySelected();
}

void GameplaySession::buySelected() {
    if (!profile_) return;
    const int option = menu_.selected();
    const bool eating = menu_.page() == NestMenuPage::Eat;
    const bool bought = eating ? purchase(static_cast<BirdUpgrade>(option)) : purchase(static_cast<NestUpgrade>(option));
    if (!bought) {
        feedback_.push_back(GameplayFeedback::UpgradeDenied);
        return;
    }
    announcePurchase(eating ? upgradeCode(static_cast<BirdUpgrade>(option)) : upgradeCode(static_cast<NestUpgrade>(option)));
    applyUpgrades();
    saveNow();
    feedback_.push_back(GameplayFeedback::UpgradeBought);
}

// Confere o nivel antes de cobrar, para nunca gastar moedas sem entregar o upgrade.
template <typename Upgrade>
bool GameplaySession::purchase(Upgrade upgrade) {
    if (profile_->isMaxed(upgrade)) return false;
    if (!profile_->spendCoins(upgradePrice(upgrade, profile_->level(upgrade)))) return false;
    return profile_->upgrade(upgrade);
}

// O perfil e local: a compra ja valeu, e o evento so conta aos outros jogadores o que mudou.
void GameplaySession::announcePurchase(int code) {
    GameEvent event;
    event.type = GameEventType::UpgradeBought;
    event.actor = localPlayer_;
    event.value = static_cast<uint8_t>(code);
    event.position = nest_.perch;
    authority_->request(event);
}

// Defesa, Alarme e Cofre valem no roubo: publishLocalPlayer e sufferRobbery leem os niveis direto do perfil.
void GameplaySession::applyUpgrades() {
    FlightStats& flight = bird_.flight().stats();
    flight.speed = speedMultiplier(profile_->level(BirdUpgrade::Speed));
    flight.agility = agilityMultiplier(profile_->level(BirdUpgrade::Agility));
    CargoStats& cargo = collector_.stats();
    cargo.capacity = carryCapacity(profile_->level(BirdUpgrade::CarryCapacity));
    cargo.collectRadius = collectRadius(profile_->level(BirdUpgrade::CollectRange));
    if (nestSizeLevel() != appliedSizeLevel_) {
        appliedSizeLevel_ = nestSizeLevel();
        nestRevision_++;
    }
}

int GameplaySession::nestSizeLevel() const { return profile_ ? profile_->level(NestUpgrade::Size) : 0; }

void GameplaySession::refreshView() {
    const FlightModel& flight = bird_.flight();
    const glm::vec3 toNest = nest_.perch - flight.position();
    view_.carried = collector_.carried();
    view_.capacity = collector_.stats().capacity;
    view_.record = records_.record();
    view_.recordHolder = records_.holder();
    view_.recordBeatenNow = recordNotice_ > 0;
    view_.cargoLostNow = cargoLostNotice_ > 0;
    view_.cargoLossCause = lossCause_;
    view_.knockedRivalNow = knockedNotice_ > 0;
    view_.nearBoundary = bird_.nearBoundary();
    view_.perchedOnNest = perched_ && perchRival_ == kNoPlayer;
    refreshRobbery();
    refreshAlarm();
    view_.nestDistance = glm::length(toNest);
    view_.nestBearing = std::remainder(std::atan2(toNest.x, -toNest.z) - flight.yaw(), kFullTurn);
    if (!profile_) return;
    view_.playerName = profile_->name();
    view_.coins = profile_->coins();
    view_.totalDelivered = profile_->totalDelivered();
    view_.leadingRecord = records_.isLeading(profile_->name(), profile_->totalDelivered());
    view_.menu = describeNestMenu(menu_, *profile_);
    view_.menu.raisedArm = menuArm_;
    view_.menu.holdProgress = menuHold_;
}

void GameplaySession::refreshRobbery() {
    RobView& rob = view_.robbery;
    rob = RobView{};
    if (!robbing()) return;
    rob.active = true;
    rob.stolenNow = stolenThisVisit_;
    const PlayerPublicState* owner = findPlayer(perchRival_);
    if (robPending_) rob.blocked = RobBlock::Waiting;
    else if (collector_.freeSpace() <= 0) rob.blocked = RobBlock::CargoFull;
    else if (!owner || owner->nestStock == 0) rob.blocked = RobBlock::EmptyNest;
    if (owner && rob.blocked == RobBlock::None) rob.progress = std::min(1.0f, robTimer_ / robSeconds(owner->nestDefense));
    if (rob.blocked == RobBlock::Waiting) rob.progress = 1.0f;
}

// A bussola aponta o ladrao enquanto o Alarme durar e ele estiver na sala.
void GameplaySession::refreshAlarm() {
    const PlayerPublicState* thief = alarm_ > 0 ? findPlayer(thief_) : nullptr;
    view_.alarmNow = thief != nullptr;
    if (!thief) return;
    const FlightModel& flight = bird_.flight();
    const glm::vec3 toThief = thief->position - flight.position();
    view_.thiefDistance = glm::length(toThief);
    view_.thiefBearing = std::remainder(std::atan2(toThief.x, -toThief.z) - flight.yaw(), kFullTurn);
}

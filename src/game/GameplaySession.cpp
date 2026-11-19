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
      store_(saveDir), records_(saveDir) {
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
    if (!perched_) {
        bird_.applyControls(controls);
        return;
    }
    menuArm_ = controls.raisedArm;
    menuHold_ = controls.menuProgress;
    bird_.applyControls(FlightControls{});
    if (settle_ >= kLandingSeconds) {
        if (controls.flaps > 0) takeOff();
        else handleMenuPress(controls.menuPressed);
    }
    refreshView();
}

void GameplaySession::stepBird(float dt) {
    if (!perched_) bird_.step(world_, dt);
}

// Todo evento confirmado passa por aqui, porque pollEvents entrega cada um uma unica vez.
void GameplaySession::update(float frameSeconds) {
    for (const GameEvent& event : authority_->pollEvents()) handleEvent(event);
    recordNotice_ = std::max(0.0f, recordNotice_ - frameSeconds);
    cargoLostNotice_ = std::max(0.0f, cargoLostNotice_ - frameSeconds);
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
    if (collector_.carried() > carriedBefore) feedback_.push_back(GameplayFeedback::CashewCollected);
    if (event.type == GameEventType::CashewDropped && collector_.carried() < carriedBefore) {
        feedback_.push_back(GameplayFeedback::CargoLost);
        cargoLostNotice_ = kCargoLostNoticeSeconds;
    }
    if (event.type == GameEventType::CashewDelivered && event.actor == localPlayer_) confirmDelivery(event);
}

// No ninho o jogador aparece sem carga, porque os cacadores nao atacam quem esta pousado.
void GameplaySession::publishLocalPlayer() {
    const FlightModel& flight = bird_.flight();
    PlayerPublicState state;
    state.id = localPlayer_;
    state.position = flight.position();
    state.yaw = flight.yaw();
    state.pitch = flight.pitch();
    state.roll = flight.roll();
    state.carried = perched_ ? 0 : static_cast<uint8_t>(std::min(collector_.carried(), 255));
    state.nestLevel = static_cast<uint8_t>(nestSizeLevel());
    if (profile_) state.birdSeed = profile_->birdSeed();
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
    const int delivered = collector_.unload(event.value);
    if (delivered <= 0 || !profile_) return;

    const int bonus = sizeCoinBonus(delivered, profile_->level(NestUpgrade::Size));
    profile_->deliverCashews(delivered + bonus, bonus);
    feedback_.push_back(GameplayFeedback::CashewDelivered);

    // O primeiro total de um computador sem record nao ultrapassa ninguem.
    const bool hadRecord = records_.record() > 0;
    if (records_.update(profile_->name(), profile_->totalDelivered()) && hadRecord) {
        feedback_.push_back(GameplayFeedback::RecordBeaten);
        recordNotice_ = kRecordNoticeSeconds;
    }
    saveNow();
}

// Depois de decolar, o pouso so volta a valer quando o passaro se afasta do ninho.
void GameplaySession::watchForLanding() {
    const float distance = glm::length(nest_.perch - bird_.flight().position());
    if (!landingArmed_) {
        landingArmed_ = distance > kRearmRadius;
        return;
    }
    if (profile_ && distance <= kLandingRadius) land();
}

void GameplaySession::land() {
    FlightModel& flight = bird_.flight();
    perched_ = true;
    settle_ = 0;
    landingFrom_ = flight.position();
    landingYaw_ = flight.yaw();
    landingPitch_ = flight.pitch();
    landingRoll_ = flight.roll();
    bird_.applyControls(FlightControls{});
    menu_.open();
    feedback_.push_back(GameplayFeedback::NestLanded);
}

// Desliza ate o poleiro e fica parado. Cajus confirmados depois do pouso tambem sao entregues.
void GameplaySession::holdPerch(float dt) {
    settle_ = std::min(settle_ + dt, kLandingSeconds);
    const float t = glm::smoothstep(0.0f, kLandingSeconds, settle_);
    FlightModel& flight = bird_.flight();
    flight.place(glm::mix(landingFrom_, nest_.perch, t),
                 landingYaw_ + std::remainder(nest_.yaw - landingYaw_, kFullTurn) * t, 0.0f);
    flight.setAttitude(glm::mix(landingPitch_, kPerchPitch, t), landingRoll_ * (1.0f - t));
    requestDelivery();
}

// A batida de saida usa o mesmo caminho das batidas normais, com a forca da decolagem.
void GameplaySession::takeOff() {
    releasePerch();
    landingArmed_ = false;
    FlightModel& flight = bird_.flight();
    flight.place(nest_.perch, nest_.yaw, FlightModel::kCruiseSpeed * flight.stats().speed * kTakeoffSpeedFactor);
    FlightControls launch;
    launch.mode = FlightMode::Flap;
    launch.flaps = 1;
    launch.flapStrength = kTakeoffFlap;
    bird_.applyControls(launch);
    feedback_.push_back(GameplayFeedback::TakeOff);
}

void GameplaySession::releasePerch() {
    perched_ = false;
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

// Defesa, Alarme e Cofre so guardam o nivel: quem os usa e o roubo de ninho.
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
    view_.perchedOnNest = perched_;
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

#pragma once
#include "entity/PlayerBird.h"
#include "events/BoundaryWhale.h"
#include "game/CashewCollector.h"
#include "game/NestMenu.h"
#include "game/NestSite.h"
#include "game/RivalNests.h"
#include "save/ProfileStore.h"
#include "save/RecordManager.h"
#include "shared/GameAuthority.h"
#include "shared/GameplayView.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Regras da partida: coleta, ninho, entrega, economia, perfil e record do jogador local.
class GameplaySession : public GameplaySource {
public:
    // Sem autoridade informada, joga sozinho com uma LocalAuthority.
    GameplaySession(const World& world, uint32_t seed, CashewField& cashews, PlayerBird& bird,
                    const std::filesystem::path& saveDir, PlayerId localPlayer = 0,
                    std::unique_ptr<GameAuthority> authority = nullptr);

    void startProfile(const std::string& name) override;
    void saveNow() override;
    const GameplayView& view() const override { return view_; }
    std::vector<GameplayFeedback> drainFeedback() override;

    void applyControls(const FlightControls& controls);   // Uma vez por quadro, antes da fisica.
    void stepBird(float dt);                              // A cada passo fixo da fisica.
    void update(float frameSeconds);                      // Unico ponto que consome GameAuthority::pollEvents.

    void publishLocalPlayer();                            // Informa a autoridade onde o jogador esta.
    GameAuthority& authority() { return *authority_; }
    PlayerId localPlayer() const { return localPlayer_; }

    bool perched() const { return perched_; }
    bool robbing() const { return perched_ && perchRival_ != kNoPlayer; }   // Pousado no ninho de um rival.
    const NestSite& nest() const { return nest_; }
    int nestSizeLevel() const;
    int nestRevision() const { return nestRevision_; }    // Muda quando o visual do ninho precisa ser refeito.
    const RivalNests& rivalNests() const { return rivals_; }   // Ninhos dos outros jogadores conectados.
    const CashewCollector& collector() const { return collector_; }
    const Profile* profile() const { return profile_ ? &*profile_ : nullptr; }
    BoundaryWhale& whale() { return whale_; }               // Baleia do limite do mapa: desenho e sons.
    const BoundaryWhale& whale() const { return whale_; }

private:
    void handleEvent(const GameEvent& event);
    void handleRobbery(const GameEvent& event);
    void handleCargoDrop(const GameEvent& event, int carriedBefore);
    void handleWhaleBite(const GameEvent& event, int carriedBefore);
    void sufferRobbery(const GameEvent& event);
    void checkTreeHit();
    void robTick(float dt);
    const PlayerPublicState* findPlayer(PlayerId id) const;
    void refreshRobbery();
    void refreshAlarm();
    void confirmDelivery(const GameEvent& event);
    void requestDelivery();
    void watchForLanding();
    void land(const NestSite& site, PlayerId owner);
    void holdPerch(float dt);
    void takeOff();
    void handleMenuPress(ArmSide side);
    void buySelected();
    template <typename Upgrade> bool purchase(Upgrade upgrade);
    void announcePurchase(int code);
    void applyUpgrades();
    void releasePerch();
    void refreshView();

    const World& world_;
    PlayerBird& bird_;
    PlayerId localPlayer_;
    std::unique_ptr<GameAuthority> authority_;
    CashewCollector collector_;
    NestSite nest_;
    RivalNests rivals_;
    ProfileStore store_;
    RecordManager records_;
    std::optional<Profile> profile_;
    NestMenu menu_;
    GameplayView view_;
    std::vector<GameplayFeedback> feedback_;
    std::vector<PlayerPublicState> players_;   // Lista replicada do quadro atual.

    bool perched_ = false, landingArmed_ = true, deliveryPending_ = false;
    float settle_ = 0, recordNotice_ = 0, cargoLostNotice_ = 0, knockedNotice_ = 0, menuHold_ = 0;
    CargoLossCause lossCause_ = CargoLossCause::Hunter;
    float treeCooldown_ = 0;
    NestSite perchSite_;                  // Ninho onde o passaro esta ou esteve pousado por ultimo.
    PlayerId perchRival_ = kNoPlayer;     // Dono do ninho rival; kNoPlayer no proprio ninho.
    float robTimer_ = 0, robWait_ = 0;
    bool robPending_ = false;
    int stolenThisVisit_ = 0;
    float alarm_ = 0;
    PlayerId thief_ = kNoPlayer;
    ArmSide menuArm_ = ArmSide::None;
    FlightMode flightMode_ = FlightMode::Glide;   // Replicado para os outros desenharem as asas.
    glm::vec3 landingFrom_{0};
    float landingYaw_ = 0, landingPitch_ = 0, landingRoll_ = 0;
    int nestRevision_ = 0, appliedSizeLevel_ = 0;
    BoundaryWhale whale_;
};

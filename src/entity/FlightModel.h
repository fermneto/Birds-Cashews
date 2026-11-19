#pragma once
#include "entity/FlightControls.h"
#include "world/World.h"
#include <glm/glm.hpp>

// Multiplicadores que os upgrades do passaro alteram.
struct FlightStats {
    float speed = 1.0f;
    float agility = 1.0f;
};

// Fisica arcade do voo: velocidade a frente e velocidade vertical tratadas separadamente.
class FlightModel {
public:
    static constexpr float kCruiseSpeed = 24.0f;
    static constexpr float kCeiling = 420.0f;
    static constexpr float kHardCeiling = 470.0f;

    void place(glm::vec3 position, float yaw, float speed);
    void setAttitude(float pitch, float roll) { pitch_ = pitch; roll_ = roll; }
    void flap(float strength);
    void step(const FlightControls& controls, float dt);
    float applyContact(const Contact& contact);
    void leaveGround() { grounded_ = false; }
    void steerToward(float targetYaw, float rate, float dt);

    glm::vec3& position() { return position_; }
    glm::vec3 position() const { return position_; }
    glm::vec3 heading() const;
    glm::vec3 velocity() const { return heading() * speed_ + glm::vec3(0, verticalSpeed_, 0); }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    float roll() const { return roll_; }
    float speed() const { return speed_; }
    float fullDiveSpeed() const;
    float verticalSpeed() const { return verticalSpeed_; }
    bool grounded() const { return grounded_; }
    FlightStats& stats() { return stats_; }

private:
    void updateSpeeds(FlightMode mode, float dt);
    void updateAttitude(const FlightControls& controls, float dt);
    float targetPitch(FlightMode mode) const;
    void applyCeiling(float dt);

    glm::vec3 position_{0};
    float yaw_ = 0, pitch_ = 0, roll_ = 0;
    float speed_ = kCruiseSpeed, verticalSpeed_ = 0;
    bool grounded_ = false;
    FlightStats stats_;
};

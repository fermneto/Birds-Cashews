#pragma once
#include "shared/GameplayView.h"

// Partida de exemplo usada apenas nos testes e nas fotos do --render-test.
class DemoGameplaySource : public GameplaySource {
public:
    DemoGameplaySource() : view_(flightSample()) {}

    void startProfile(const std::string& name) override { view_.playerName = name; profilesStarted_++; }
    void saveNow() override { saves_++; }
    const GameplayView& view() const override { return view_; }
    std::vector<GameplayFeedback> drainFeedback() override;

    void setView(const GameplayView& view) { view_ = view; }
    void push(GameplayFeedback feedback) { pending_.push_back(feedback); }
    int saves() const { return saves_; }
    int profilesStarted() const { return profilesStarted_; }

    static GameplayView flightSample();
    static GameplayView recordSample();
    static GameplayView nestRootSample();
    static GameplayView nestEatSample();
    static GameplayView nestInvestSample();

private:
    GameplayView view_;
    std::vector<GameplayFeedback> pending_;
    int saves_ = 0, profilesStarted_ = 0;
};

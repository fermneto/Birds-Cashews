#pragma once
#include "entity/FlightControls.h"
#include "vision/Pose.h"
#include <array>

enum class CalRegion { Left, Right, Top, Bottom };
constexpr int kCalRegionCount = 4;

// Faixa de cada regiao, como fracao do quadro espelhado. Com o corpo inteiro no quadro, os bracos
// abertos chegam perto de 20% das laterais, entao a faixa precisa ser um pouco maior que isso.
constexpr float kCalRegionBand = 0.25f;

// Confere se o jogador e detectado em todo o video: corpo inteiro e maos nas quatro bordas.
// As marcas sao independentes: valem em qualquer ordem e as duas maos podem marcar ao mesmo tempo.
class CalibrationTracker {
public:
    void reset();
    // newSample indica uma pose nova do detector; sem ela, a pose anterior continua valendo.
    void update(const Pose& mirroredPose, cv::Size frame, float seconds, bool newSample = true);

    static bool inRegion(CalRegion region, cv::Point2f normalized);
    static bool isFullBody(const Pose& pose, cv::Size frame);

    bool bodyVisibleNow() const { return bodyNow_; }
    bool bodyConfirmed() const { return body_.done; }
    bool reached(CalRegion region) const { return regions_[static_cast<size_t>(region)].done; }
    int marksDone() const;
    bool complete() const;

    // Levar a mao ao alto imita o gesto do menu, entao os gestos so valem depois de concluir
    // e de o jogador baixar os bracos.
    bool acceptsGesture(const FlightControls& controls);

private:
    // Tempo e amostras seguidas de uma condicao. Uma falha curta do detector nao zera a contagem.
    struct Hold {
        float held = 0, missing = 0;
        int samples = 0;
        bool done = false;
        void step(bool inside, float seconds, bool newSample, float needed, int minSamples);
    };

    std::array<Hold, kCalRegionCount> regions_{};
    Hold body_;
    bool bodyNow_ = false, armsLowered_ = false;
};

#pragma once
#include "ui/SoundManager.h"
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

// Porte do passaro, que escolhe o tipo de canto.
enum class BirdVoice { Small, Medium, Large };

// Um NPC que pode cantar.
struct SingingBird {
    uint32_t key = 0;          // Identidade estavel entre quadros.
    glm::vec3 position{0};
    BirdVoice voice = BirdVoice::Small;
};

struct ChirpRequest {
    UiSound sound = UiSound::ChirpTweet;
    float volume = 0;
};

// Decide quando cada passaro por perto canta. Sem passaros perto, silencio: os cantos nunca viram
// um fundo constante. Cada passaro espera alguns segundos entre um canto e outro, e ha um intervalo
// minimo entre dois cantos quaisquer, para nao poluir.
class BirdChorus {
public:
    static constexpr float kHearing = 80.0f;     // Metros: mais longe nao se ouve.
    static constexpr float kMinGap = 0.7f;       // Segundos entre dois cantos quaisquer.

    explicit BirdChorus(uint32_t seed = 0xB12Du) : random_(seed) {}
    std::vector<ChirpRequest> update(float dt, glm::vec3 listener, const std::vector<SingingBird>& birds);

private:
    float nextWait(BirdVoice voice);

    std::mt19937 random_;
    std::unordered_map<uint32_t, float> waits_;   // Segundos ate o proximo canto de cada passaro.
    float gap_ = 0;
};

UiSound chirpFor(BirdVoice voice, uint32_t key);   // Pequenos alternam entre piado e trinado.

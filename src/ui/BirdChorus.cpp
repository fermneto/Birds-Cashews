#include "ui/BirdChorus.h"
#include <algorithm>

UiSound chirpFor(BirdVoice voice, uint32_t key) {
    switch (voice) {
        case BirdVoice::Small: return key % 2 ? UiSound::ChirpTrill : UiSound::ChirpTweet;
        case BirdVoice::Medium: return UiSound::ChirpWhistle;
        case BirdVoice::Large: return UiSound::ChirpCaw;
    }
    return UiSound::ChirpTweet;
}

// Grandes cantam bem menos que os pequenos.
float BirdChorus::nextWait(BirdVoice voice) {
    const float low = voice == BirdVoice::Large ? 7.0f : 3.0f, high = voice == BirdVoice::Large ? 15.0f : 9.0f;
    return std::uniform_real_distribution<float>(low, high)(random_);
}

std::vector<ChirpRequest> BirdChorus::update(float dt, glm::vec3 listener, const std::vector<SingingBird>& birds) {
    gap_ = std::max(0.0f, gap_ - dt);
    std::vector<ChirpRequest> chirps;
    std::unordered_map<uint32_t, float> kept;
    for (const SingingBird& bird : birds) {
        const float distance = glm::length(bird.position - listener);
        if (distance >= kHearing) continue;   // Longe: esquece o relogio, e ao chegar perto ele recomeca.
        const auto found = waits_.find(bird.key);
        // Quem acabou de chegar perto espera um pouco, para os cantos nao comecarem todos juntos.
        float wait = found != waits_.end() ? found->second - dt : nextWait(bird.voice) * 0.5f;
        if (wait <= 0 && gap_ <= 0) {
            const float closeness = 1.0f - distance / kHearing;
            chirps.push_back({chirpFor(bird.voice, bird.key), 0.15f + 0.85f * closeness * closeness});
            gap_ = kMinGap;
            wait = nextWait(bird.voice);
        }
        kept[bird.key] = std::max(wait, 0.0f);
    }
    waits_ = std::move(kept);
    return chirps;
}

#pragma once
#include <vector>

// Sons da baleia do limite do mapa, sintetizados ao abrir o jogo como os outros efeitos.
enum class WhaleSound { Spout, Splash, Bite, Chew, Spit, Rumble, Song };

// Amostras de -1 a 1, mono, na taxa pedida. Sao longos e graves de proposito: duram ate uns 3,5 s.
std::vector<float> synthesizeWhaleSound(WhaleSound sound, int sampleRate);

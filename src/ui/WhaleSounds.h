#pragma once
#include <vector>

// Sons da baleia do limite do mapa, sintetizados ao abrir o jogo como os outros efeitos.
enum class WhaleSound { Spout, Splash, Bite, Chew, Spit, Rumble };

// Amostras de -1 a 1, mono, na taxa pedida. Todos duram menos de 1 s.
std::vector<float> synthesizeWhaleSound(WhaleSound sound, int sampleRate);

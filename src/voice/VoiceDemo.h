#pragma once
#include "shared/GameTypes.h"
#include <string>
#include <vector>

// Falante virtual da demonstracao: parado a 10 m na frente ate parkSeconds, depois voa num circulo
// de 25 m de raio a 40 m/s, com centro 30 m a frente de quem ouve (passa de 5 a 55 m, pela direita e pela esquerda).
PlayerPublicState virtualSpeaker(float seconds, float parkSeconds, PlayerId id);

// Grava audio estereo intercalado em WAV de 16 bits.
bool writeStereoWav(const std::string& path, const std::vector<float>& stereo, int sampleRate);

// ./birds --voz-teste: microfone -> voz de passaro -> Opus -> volta como falante virtual circulando.
int runVoiceDemo();
// ./birds --voz-teste arquivo saida.wav: o mesmo caminho com um sinal sintetico, sem dispositivos.
int runVoiceFileDemo(const std::string& outputPath);

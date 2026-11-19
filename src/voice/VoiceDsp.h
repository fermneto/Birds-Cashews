#pragma once
#include <cstdint>
#include <vector>

// Formato unico da voz: 48 kHz mono, em quadros de 20 ms.
constexpr int kVoiceRate = 48000;
constexpr int kVoiceFrame = 960;

// Ajustes da deteccao de fala.
struct SpeechSettings {
    float marginDb = 10.0f;            // Quanto acima do ruido de fundo conta como fala.
    float minimumDb = -55.0f;          // Limiar absoluto: abaixo disso nunca e fala.
    float hangoverSeconds = 0.3f;      // Espera depois da ultima fala, para nao cortar o fim das palavras.
    float floorRiseDbPerSecond = 2.0f; // Velocidade com que o ruido de fundo estimado sobe.
};

// Deteccao de fala por energia. O ruido de fundo e estimado por um seguidor assimetrico:
// desce na hora quando o quadro fica mais baixo e sobe devagar quando fica mais alto.
// Assim as pausas entre palavras ancoram o ruido, e um ruido novo e constante e absorvido em alguns segundos.
class SpeechDetector {
public:
    explicit SpeechDetector(SpeechSettings settings = SpeechSettings{});
    bool process(const float* frame, int count);   // Devolve se ha fala neste quadro.
    void reset();

    bool speaking() const { return speaking_; }
    float levelDb() const { return levelDb_; }
    float noiseFloorDb() const { return floorDb_; }
    float thresholdDb() const;
    float level() const;   // 0 a 1: quanto o quadro passou do limiar, para abrir o bico.

private:
    SpeechSettings settings_;
    float levelDb_ = -100.0f, floorDb_ = -100.0f;
    int hangover_ = 0;
    bool started_ = false, speaking_ = false;
};

// Mudanca de tom granular no dominio do tempo, sem mudar a duracao.
// Graos de 30 ms com janela de Hann e 50% de sobreposicao avancam no mesmo passo na entrada e na saida
// (por isso a duracao nao muda); dentro de cada grao a entrada e lida "ratio" vezes mais rapido
// (por isso o tom muda). Dois graos sempre se somam e as janelas somam 1, entao o volume fica constante.
// Sincronizacao (como no SOLA): cada grao novo comeca ate 12 ms antes do ponto nominal, no lugar em que
// a forma de onda mais se parece com a do grao que esta saindo. Sem isso os graos se somam fora de fase
// e o som fica "metalico", com linhas a cada 67 Hz em vez do tom certo.
class PitchShifter {
public:
    explicit PitchShifter(float semitones = 5.0f, int grainSamples = 1440);
    void setSemitones(float semitones);
    float ratio() const { return ratio_; }
    int latency() const { return delay_; }   // Amostras de atraso, para ler o futuro de cada grao.
    void process(const float* in, float* out, int count);
    void reset();

private:
    int64_t alignedStart(int64_t grain) const;

    std::vector<float> history_, window_;
    int64_t time_ = 0;
    int64_t starts_[2] = {0, 0};   // Inicio de leitura dos dois graos ativos (par e impar).
    float ratio_ = 1.0f;
    int grain_, hop_, delay_ = 0;
};

// Reamostragem continua para o Doppler, com buffer limitado.
// Le a entrada a "factor" amostras por amostra de saida, com interpolacao cubica e fator suavizado.
// Como o fator muda quanto audio e consumido, quando o atraso sai da faixa permitida a leitura salta
// para tras ou para frente cerca de 5 a 15 ms, no ponto mais parecido (correlacao) e com um cruzamento curto.
class DopplerResampler {
public:
    static constexpr int kFade = 192;                    // Cruzamento de 4 ms em cada salto.
    static constexpr int kMinLag = 320;                  // Menor atraso antes de saltar para tras.
    static constexpr int kMaxLag = kMinLag + 960 + 720 + 240;   // Maior atraso antes de saltar para frente.

    DopplerResampler();
    void push(const float* in, int count);
    void render(float* out, int count, float factor);
    void reset();   // Volta ao silencio com o atraso inicial.

    int buffered() const { return static_cast<int>(static_cast<double>(write_) - read_); }
    float currentFactor() const { return factor_; }
    int splices() const { return splices_; }

private:
    float at(double position) const;
    int bestSplice(bool backward) const;

    std::vector<float> ring_;
    int64_t write_ = 0;
    double read_ = 0, fadeRead_ = 0;
    float factor_ = 1.0f;
    int fadeLeft_ = 0, splices_ = 0;
};

// Limitador estereo de pico com ataque instantaneo: a saida nunca passa do limiar.
class Limiter {
public:
    explicit Limiter(float threshold = 0.89f, float releaseSeconds = 0.15f);
    void process(float* stereo, int frames);
    float gain() const { return gain_; }

private:
    float threshold_, release_, envelope_ = 0, gain_ = 1;
};

// Interpolacao cubica de Hermite entre y1 e y2, com t de 0 a 1.
inline float hermite(float y0, float y1, float y2, float y3, float t) {
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

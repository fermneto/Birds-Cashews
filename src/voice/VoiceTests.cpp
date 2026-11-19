#include "voice/VoiceTests.h"
#include "core/TestRun.h"
#include "voice/JitterBuffer.h"
#include "voice/LoopbackVoiceLink.h"
#include "voice/OpusCodec.h"
#include "voice/Spatializer.h"
#include "voice/VoiceChat.h"
#include "voice/VoiceDsp.h"
#include "voice/VoiceMixer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>

namespace {
constexpr double kTwoPi = 6.283185307179586;
const float kBirdRatio = std::pow(2.0f, 5.0f / 12.0f);

std::vector<float> tone(double hz, size_t count, float amplitude = 0.5f, size_t offset = 0) {
    std::vector<float> samples(count);
    for (size_t i = 0; i < count; i++)
        samples[i] = amplitude * static_cast<float>(std::sin(kTwoPi * hz * static_cast<double>(i + offset) / kVoiceRate));
    return samples;
}

std::vector<float> whiteNoise(size_t count, float sigma, unsigned seed = 1) {
    std::mt19937 random(seed);
    std::normal_distribution<float> normal(0.0f, sigma);
    std::vector<float> samples(count);
    for (float& sample : samples) sample = normal(random);
    return samples;
}

double magnitudeAt(const std::vector<float>& x, size_t start, size_t count, double hz) {
    const std::complex<double> step = std::polar(1.0, -kTwoPi * hz / kVoiceRate);
    std::complex<double> phase = 1.0, sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        const double window = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / static_cast<double>(count));
        sum += phase * (window * x[start + i]);
        phase *= step;
    }
    return std::abs(sum);
}

// Frequencia do pico do espectro entre lo e hi: busca a cada 1 Hz e refina a cada 0,05 Hz.
double peakFrequency(const std::vector<float>& x, size_t start, size_t count, double lo, double hi) {
    double best = lo, bestMagnitude = -1;
    for (double hz = lo; hz <= hi; hz += 1.0) {
        const double magnitude = magnitudeAt(x, start, count, hz);
        if (magnitude > bestMagnitude) { bestMagnitude = magnitude; best = hz; }
    }
    const double center = best;
    for (double hz = center - 1.0; hz <= center + 1.0; hz += 0.05) {
        const double magnitude = magnitudeAt(x, start, count, hz);
        if (magnitude > bestMagnitude) { bestMagnitude = magnitude; best = hz; }
    }
    return best;
}

float rms(const float* x, size_t count, size_t stride = 1) {
    double sum = 0;
    for (size_t i = 0; i < count; i++) sum += static_cast<double>(x[i * stride]) * x[i * stride];
    return static_cast<float>(std::sqrt(sum / std::max<size_t>(count, 1)));
}

bool near(double value, double expected, double tolerance) { return std::abs(value - expected) <= tolerance; }

std::string describe(const std::string& name, double value) {
    std::ostringstream out;
    out << name << " (" << std::fixed << std::setprecision(3) << value << ")";
    return out.str();
}

void testSpeechDetector(TestRun& run) {
    SpeechDetector silence;
    const std::vector<float> zeros(kVoiceFrame, 0.0f);
    int speakingFrames = 0;
    for (int i = 0; i < 100; i++) speakingFrames += silence.process(zeros.data(), kVoiceFrame);
    run.check("Voz: silencio nao e fala", speakingFrames == 0);

    SpeechDetector noisy;
    const std::vector<float> noise = whiteNoise(200 * kVoiceFrame, 0.02f);
    speakingFrames = 0;
    for (int i = 0; i < 200; i++) speakingFrames += noisy.process(noise.data() + i * kVoiceFrame, kVoiceFrame);
    run.check("Voz: ruido de fundo constante nao e fala", speakingFrames == 0);

    // Ruido, depois um tom de 1 s, depois ruido de novo: fala no tom, segura o fim e depois fecha.
    const std::vector<float> speech = tone(300, 50 * kVoiceFrame, 0.3f);
    bool onsetNow = false, heldAfterEnd = false, closedLater = false;
    int tonesSpeaking = 0;
    for (int i = 0; i < 50; i++) {
        std::vector<float> frame(speech.begin() + i * kVoiceFrame, speech.begin() + (i + 1) * kVoiceFrame);
        for (int s = 0; s < kVoiceFrame; s++) frame[static_cast<size_t>(s)] += noise[static_cast<size_t>(s)];
        tonesSpeaking += noisy.process(frame.data(), kVoiceFrame);
        if (i == 0) onsetNow = noisy.speaking();
    }
    for (int i = 0; i < 40; i++) {
        noisy.process(noise.data() + i * kVoiceFrame, kVoiceFrame);
        if (i == 10) heldAfterEnd = noisy.speaking();
        if (i == 25) closedLater = !noisy.speaking();
    }
    run.check("Voz: tom sobre o ruido e fala desde o primeiro quadro", onsetNow && tonesSpeaking == 50);
    run.check("Voz: fala segura 300 ms depois do fim e depois fecha", heldAfterEnd && closedLater);
    run.check("Voz: limiar acompanha o ruido de fundo", noisy.noiseFloorDb() > -40.0f && noisy.noiseFloorDb() < -30.0f);
}

std::vector<float> shiftAll(PitchShifter& shifter, const std::vector<float>& in) {
    std::vector<float> out(in.size());
    for (size_t i = 0; i + kVoiceFrame <= in.size(); i += kVoiceFrame) shifter.process(in.data() + i, out.data() + i, kVoiceFrame);
    return out;
}

// Inicio e fim do trecho com som, por janelas de 5 ms acima de metade do maior volume.
std::pair<double, double> soundSpan(const std::vector<float>& x) {
    constexpr size_t kWindow = 240;
    std::vector<float> levels;
    for (size_t i = 0; i + kWindow <= x.size(); i += kWindow) levels.push_back(rms(x.data() + i, kWindow));
    const float limit = *std::max_element(levels.begin(), levels.end()) * 0.5f;
    size_t first = levels.size(), last = 0;
    for (size_t i = 0; i < levels.size(); i++)
        if (levels[i] > limit) { first = std::min(first, i); last = i; }
    return {static_cast<double>(first * kWindow) / kVoiceRate, static_cast<double>((last + 1) * kWindow) / kVoiceRate};
}

void testPitchShifter(TestRun& run) {
    PitchShifter shifter(5.0f);
    const std::vector<float> in = tone(300, kVoiceRate);
    const std::vector<float> out = shiftAll(shifter, in);
    const double measured = peakFrequency(out, 9600, 28800, 250, 500);
    const double expected = 300.0 * kBirdRatio;
    run.check(describe("Voz: +5 semitons leva 300 Hz a 400,4 Hz", measured), near(measured, expected, expected * 0.005));
    const float level = rms(out.data() + 9600, 28800) / rms(in.data() + 9600, 28800);
    run.check(describe("Voz: volume da voz de passaro fica perto do original", level), level > 0.75f && level < 1.25f);

    // Duracao: um toque de 0,5 s continua com 0,5 s, so atrasado pela latencia do deslocador.
    std::vector<float> burst(kVoiceRate, 0.0f);
    const std::vector<float> beep = tone(250, kVoiceRate / 2);
    std::copy(beep.begin(), beep.end(), burst.begin() + 14400);
    PitchShifter burstShifter(5.0f);
    const auto [start, end] = soundSpan(shiftAll(burstShifter, burst));
    run.check(describe("Voz: mudanca de tom nao muda a duracao (s)", end - start), near(end - start, 0.5, 0.03));
    run.check("Voz: latencia do deslocador e de cerca de 10 ms", burstShifter.latency() < 600 && start - 0.3 < 0.05);

    PitchShifter lower(-5.0f);
    const double down = peakFrequency(shiftAll(lower, in), 9600, 28800, 150, 350);
    run.check(describe("Voz: -5 semitons tambem funciona", down), near(down, 300.0 / kBirdRatio, 1.5));
    PitchShifter off(0.0f);
    const std::vector<float> same = shiftAll(off, in);
    run.check("Voz: zero semitons deixa o som intacto", same == in);
}

void testOpus(TestRun& run) {
    VoiceEncoder encoder;
    VoiceDecoder decoder;
    run.check("Voz: Opus abre codificador e decodificador", encoder.ok() && decoder.ok());
    const std::vector<float> in = tone(440, kVoiceRate, 0.4f);
    std::vector<float> out(in.size());
    size_t bytes = 0, largest = 0;
    bool decoded = true;
    for (size_t i = 0; i < in.size(); i += kVoiceFrame) {
        const std::vector<uint8_t> packet = encoder.encode(in.data() + i);
        bytes += packet.size();
        largest = std::max(largest, packet.size());
        decoded = decoded && !packet.empty() && decoder.decode(packet, out.data() + i);
    }
    run.check("Voz: cada quadro Opus cabe em kMaxVoicePayload", decoded && largest > 0 && largest <= kMaxVoicePayload);
    const double kbps = static_cast<double>(bytes) * 8.0 / 1000.0;
    run.check(describe("Voz: Opus perto de 24 kbps", kbps), kbps > 12.0 && kbps < 32.0);
    const double measured = peakFrequency(out, 9600, 28800, 380, 500);
    const float level = rms(out.data() + 9600, 28800) / rms(in.data() + 9600, 28800);
    run.check(describe("Voz: ida e volta pelo Opus preserva o tom", measured), near(measured, 440.0, 1.0));
    run.check(describe("Voz: ida e volta pelo Opus preserva o volume", level), level > 0.7f && level < 1.3f);

    std::vector<float> concealed(kVoiceFrame, 99.0f);
    decoder.conceal(nullptr, concealed.data());
    const bool finite = std::all_of(concealed.begin(), concealed.end(), [](float v) { return std::isfinite(v) && std::abs(v) <= 1.5f; });
    const std::vector<uint8_t> next = encoder.encode(in.data());
    std::vector<float> recovered(kVoiceFrame, 99.0f);
    decoder.conceal(&next, recovered.data());
    run.check("Voz: ocultacao de perda e FEC devolvem um quadro valido",
              finite && rms(recovered.data(), kVoiceFrame) < 1.0f && recovered[0] != 99.0f);
}

std::vector<uint8_t> tag(int value) { return {static_cast<uint8_t>(value)}; }

void testJitterBuffer(TestRun& run) {
    using Kind = JitterBuffer::Kind;
    JitterBuffer jitter(3);
    std::vector<std::string> played;
    const auto pop = [&]() {
        JitterBuffer::Item item = jitter.pop();
        if (item.kind == Kind::Packet) played.push_back(std::to_string(item.payload[0]));
        else if (item.kind == Kind::Lost) played.push_back(item.next.empty() ? "P" : "F" + std::to_string(item.next[0]));
        else played.push_back("-");
    };
    pop();   // Vazio: repouso.
    jitter.push(2, tag(2));
    pop();   // Ainda esperando juntar 60 ms.
    jitter.push(0, tag(0));
    jitter.push(1, tag(1));
    pop();   // Tres quadros: comeca pelo 0.
    jitter.push(1, tag(1));   // Duplicado.
    jitter.push(4, tag(4));
    jitter.push(3, tag(3));
    pop(); pop();
    jitter.push(6, tag(6));   // O 5 se perdeu.
    pop(); pop(); pop();
    jitter.push(7, tag(7));
    jitter.push(5, tag(5));   // Chegou tarde demais.
    pop(); pop();
    pop(); pop(); pop();   // A pessoa parou: duas ocultacoes e repouso.
    jitter.push(8, tag(8));   // Tambem tarde: ja foi ocultado.
    pop();
    std::string sequence;
    for (const std::string& step : played) sequence += step + " ";
    run.check("Voz: jitter reordena, oculta a perda com FEC e para (" + sequence + ")",
              sequence == "- - 0 1 2 3 4 F6 6 7 P P - - ");
    run.check("Voz: jitter descarta duplicados e atrasados",
              jitter.stats().duplicates == 1 && jitter.stats().late == 2 && jitter.stats().lost == 1);

    // Recupera quando a pessoa volta a falar.
    jitter.push(30, tag(30));
    jitter.push(31, tag(31));
    jitter.push(32, tag(32));
    played.clear();
    pop(); pop(); pop();
    run.check("Voz: jitter volta a tocar numa fala nova",
              played == std::vector<std::string>{"30", "31", "32"} && jitter.stats().talkspurts == 2);

    // A sequencia tem 16 bits: atravessar a volta nao confunde a ordem.
    JitterBuffer wrap(3);
    wrap.push(0, tag(0));
    wrap.push(65535, tag(35));
    wrap.push(65534, tag(34));
    std::vector<int> wrapped;
    for (int i = 0; i < 3; i++) {
        const JitterBuffer::Item item = wrap.pop();
        wrapped.push_back(item.kind == Kind::Packet ? item.payload[0] : -1);
    }
    run.check("Voz: jitter atravessa a volta da sequencia", wrapped == std::vector<int>{34, 35, 0});

    // Fala curta, com menos de 60 ms, tambem toca depois da espera.
    JitterBuffer shortSpeech(3);
    shortSpeech.push(10, tag(10));
    const bool waits = shortSpeech.pop().kind == Kind::Idle && shortSpeech.pop().kind == Kind::Idle;
    run.check("Voz: jitter toca fala curta depois de esperar 60 ms", waits && shortSpeech.pop().kind == Kind::Packet);

    // Rajada: o atraso nunca passa de alvo + 4 quadros.
    JitterBuffer burst(3);
    for (int i = 0; i < 20; i++) burst.push(static_cast<uint16_t>(i), tag(i));
    burst.pop();
    run.check("Voz: jitter limita o atraso numa rajada", burst.buffered() <= 7 && burst.stats().overflow > 0);
}

void testSpatial(TestRun& run) {
    const SpatialSettings settings;
    bool monotonic = true;
    for (float d = 15; d < 80; d += 1) monotonic = monotonic && distanceGain(d + 1, settings) <= distanceGain(d, settings);
    run.check("Voz: volume cheio ate 15 m e silencio a 80 m",
              distanceGain(0, settings) == 1 && distanceGain(15, settings) == 1 && distanceGain(80, settings) == 0 &&
              distanceGain(200, settings) == 0);
    run.check("Voz: volume cai suave e sempre entre 15 e 80 m",
              monotonic && near(distanceGain(47.5f, settings), 0.5, 0.01) && distanceGain(20, settings) > 0.95f);

    VoiceListener me;
    PlayerPublicState other;
    other.position = {10, 0, 0};
    const SpatialParams right = spatialize(me, other, settings);
    other.position = {-10, 0, 0};
    const SpatialParams left = spatialize(me, other, settings);
    other.position = {0, 0, -10};
    const SpatialParams ahead = spatialize(me, other, settings);
    run.check("Voz: falante a direita sai na direita", right.right > 0.99f && right.left < 0.01f);
    run.check("Voz: falante a esquerda sai na esquerda", left.left > 0.99f && left.right < 0.01f);
    run.check("Voz: falante a frente fica no centro com potencia constante",
              near(ahead.left, ahead.right, 1e-4) && near(ahead.left * ahead.left + ahead.right * ahead.right, 1.0, 1e-4));
    me.yaw = 1.5707963f;   // Virado para +x: o +z fica a direita.
    other.position = {0, 0, 10};
    run.check("Voz: pan acompanha a direcao de quem ouve", spatialize(me, other, settings).pan > 0.99f);
}

void testDoppler(TestRun& run) {
    const SpatialSettings settings;
    VoiceListener me;
    const glm::vec3 ahead(0, 0, -40);
    const float still = dopplerFactor(me, ahead, glm::vec3(0), settings);
    const float approach = dopplerFactor(me, ahead, glm::vec3(0, 0, 40), settings);
    const float recede = dopplerFactor(me, ahead, glm::vec3(0, 0, -40), settings);
    const float passing = dopplerFactor(me, ahead, glm::vec3(40, 0, 0), settings);
    run.check("Voz: Doppler e 1 com os dois parados e numa passagem de lado", still == 1.0f && near(passing, 1.0, 1e-5));
    run.check(describe("Voz: Doppler aproximando = 343 / (343 - 3*40)", approach), near(approach, 343.0 / 223.0, 1e-4));
    run.check(describe("Voz: Doppler afastando = 343 / (343 + 3*40)", recede), near(recede, 343.0 / 463.0, 1e-4));
    me.velocity = {0, 0, -20};
    const float listenerMoving = dopplerFactor(me, ahead, glm::vec3(0), settings);
    run.check(describe("Voz: Doppler com quem ouve indo ao encontro", listenerMoving), near(listenerMoving, 403.0 / 343.0, 1e-4));
    run.check("Voz: Doppler limitado entre 0,6 e 1,6",
              dopplerFactor(VoiceListener{}, ahead, glm::vec3(0, 0, 100), settings) == 1.6f &&
              dopplerFactor(VoiceListener{}, ahead, glm::vec3(0, 0, -100), settings) == 0.6f);

    // A reamostragem aplica a razao na frequencia.
    for (float factor : {approach, recede}) {
        DopplerResampler resampler;
        const std::vector<float> in = tone(500, 2 * kVoiceRate);
        std::vector<float> out(in.size());
        for (size_t i = 0; i < in.size(); i += kVoiceFrame) {
            resampler.push(in.data() + i, kVoiceFrame);
            resampler.render(out.data() + i, kVoiceFrame, factor);
        }
        const double measured = peakFrequency(out, kVoiceRate / 2, kVoiceRate, 200, 1000);
        run.check(describe(factor > 1 ? "Voz: aproximando o tom de 500 Hz sobe" : "Voz: afastando o tom de 500 Hz desce", measured),
                  near(measured, 500.0 * factor, 500.0 * factor * 0.005));
    }
}

void testBoundedDoppler(TestRun& run) {
    DopplerResampler resampler;
    const std::vector<float> in = tone(700, 60 * kVoiceRate, 0.5f);
    std::vector<float> out(kVoiceFrame);
    int minLag = 1 << 30, maxLag = 0;
    float quietest = 1, maxStep = 0, previous = 0;
    for (size_t b = 0; b < in.size() / kVoiceFrame; b++) {
        const double t = static_cast<double>(b) * kVoiceFrame / kVoiceRate;
        // Fator varrendo toda a faixa a cada 5 s, com saltos bruscos a cada 7 s.
        float factor = static_cast<float>(1.1 + 0.5 * std::sin(kTwoPi * t / 5.0));
        if (std::fmod(t, 7.0) < 0.5) factor = std::fmod(t, 14.0) < 7.0 ? 1.6f : 0.6f;
        resampler.push(in.data() + b * kVoiceFrame, kVoiceFrame);
        minLag = std::min(minLag, resampler.buffered());
        maxLag = std::max(maxLag, resampler.buffered());
        resampler.render(out.data(), kVoiceFrame, factor);
        minLag = std::min(minLag, resampler.buffered());
        if (b > 5) quietest = std::min(quietest, rms(out.data(), kVoiceFrame));
        for (float sample : out) {
            if (b > 5) maxStep = std::max(maxStep, std::abs(sample - previous));
            previous = sample;
        }
    }
    std::ostringstream detail;
    detail << "atraso de " << minLag << " a " << maxLag << " amostras, " << resampler.splices() << " saltos";
    run.check("Voz: buffer do Doppler fica limitado em 60 s variando (" + detail.str() + ")",
              minLag >= 3 && maxLag <= DopplerResampler::kMaxLag + kVoiceFrame);
    run.check(describe("Voz: Doppler variando nunca esvazia (menor RMS por bloco)", quietest), quietest > 0.3f);
    // Um seno de 0,5 a 1,6 x 700 Hz muda no maximo 0,5 * 2pi * 1120 / 48000 = 0,073 por amostra.
    run.check(describe("Voz: Doppler mudando nao estala (maior passo)", maxStep), maxStep < 0.12f);
}

struct FakeSpeaker {
    std::unique_ptr<VoiceEncoder> encoder = std::make_unique<VoiceEncoder>();
    std::vector<float> signal;
    uint16_t sequence = 0;
};

void testMixing(TestRun& run) {
    // Sala cheia: 6 jogadores, entao quem ouve recebe 5 falantes de uma vez, todos gritando bem perto.
    constexpr int kSpeakers = 5, kTicks = 150;
    VoiceMixer mixer;
    std::vector<FakeSpeaker> speakers(kSpeakers);
    VoiceListener me;
    for (int s = 0; s < kSpeakers; s++) {
        speakers[static_cast<size_t>(s)].signal = tone(180.0 + 70.0 * s, static_cast<size_t>(kTicks) * kVoiceFrame, 0.95f);
        PlayerPublicState state;
        state.id = static_cast<PlayerId>(s);
        state.position = {std::sin(1.2f * s) * 5.0f, 0, -std::cos(1.2f * s) * 5.0f};
        state.velocity = {0, 0, 20.0f * (s - 2)};   // Cada um com um Doppler diferente.
        mixer.setSpatial(state.id, spatialize(me, state, SpatialSettings{}));
    }
    std::vector<float> stereo(2 * kVoiceFrame);
    float peak = 0, loudness = 0;
    int allSpeaking = 0;
    double decodeMs = 0, encodeMs = 0;
    SpeechDetector detector;
    PitchShifter shifter;
    VoiceEncoder localEncoder;
    std::vector<float> shifted(kVoiceFrame);
    for (int tick = 0; tick < kTicks; tick++) {
        for (int s = 0; s < kSpeakers; s++) {
            FakeSpeaker& speaker = speakers[static_cast<size_t>(s)];
            VoiceFrame frame{static_cast<PlayerId>(s), speaker.sequence++, speaker.encoder->encode(speaker.signal.data() + tick * kVoiceFrame)};
            mixer.receive(frame);
        }
        // Custo do lado de quem ouve: jitter, Opus, Doppler, pan e mixagem dos 5 falantes.
        const auto begin = std::chrono::steady_clock::now();
        mixer.render(stereo.data());
        const auto middle = std::chrono::steady_clock::now();
        // Custo do lado de quem fala: deteccao, voz de passaro e Opus do proprio microfone.
        const float* mic = speakers[0].signal.data() + tick * kVoiceFrame;
        detector.process(mic, kVoiceFrame);
        shifter.process(mic, shifted.data(), kVoiceFrame);
        localEncoder.encode(shifted.data());
        const auto end = std::chrono::steady_clock::now();
        decodeMs += std::chrono::duration<double, std::milli>(middle - begin).count();
        encodeMs += std::chrono::duration<double, std::milli>(end - middle).count();
        if (tick > 10) {
            for (float sample : stereo) peak = std::max(peak, std::abs(sample));
            loudness = std::max(loudness, rms(stereo.data(), 2 * kVoiceFrame));
        }
        int now = 0;
        for (int s = 0; s < kSpeakers; s++) now += mixer.speaking(static_cast<PlayerId>(s));
        allSpeaking = std::max(allSpeaking, now);
    }
    run.check("Voz: mixagem recebe 5 falantes ao mesmo tempo", allSpeaking == kSpeakers && mixer.voices() == kSpeakers);
    run.check(describe("Voz: 5 falantes somados nao saturam (pico)", peak), peak <= 0.8901f && loudness > 0.2f);
    const double perFrame = (decodeMs + encodeMs) / kTicks;
    std::ostringstream cost;
    cost << std::fixed << std::setprecision(3) << "Voz: custo por quadro de 20 ms com 5 falantes: " << perFrame << " ms (receber "
         << decodeMs / kTicks << " ms + enviar " << encodeMs / kTicks << " ms)";
    run.check(cost.str(), perFrame < 5.0);
}

void testLoopbackChat(TestRun& run) {
    auto hub = std::make_shared<LoopbackVoiceHub>();
    LoopbackVoiceLink linkA(hub), linkB(hub);
    VoiceSettings settings;
    settings.useDevices = false;
    VoiceChat alice(settings), bruno(settings);
    run.check("Voz: VoiceChat sem dispositivos inicia sem SDL", alice.start() && bruno.start() && !alice.hasMicrophone());
    alice.attach(&linkA, 0);
    bruno.attach(&linkB, 1);

    PlayerPublicState aliceState, brunoState;
    aliceState.id = 0;
    aliceState.position = {20, 0, 0};   // A direita de Bruno, que olha para -z.
    brunoState.id = 1;
    const VoiceListener aliceEar = VoiceListener::from(aliceState), brunoEar = VoiceListener::from(brunoState);

    constexpr int kBlocks = 200;
    const std::vector<float> noise = whiteNoise(static_cast<size_t>(kBlocks) * kVoiceFrame, 0.002f, 3);
    const std::vector<float> speech = tone(300, static_cast<size_t>(kBlocks) * kVoiceFrame, 0.3f);
    std::vector<float> heard, ignored(2 * kVoiceFrame);
    bool aliceSpoke = false, brunoHeard = false, brunoSpoke = false;
    for (int b = 0; b < kBlocks; b++) {
        std::vector<float> mic(noise.begin() + b * kVoiceFrame, noise.begin() + (b + 1) * kVoiceFrame);
        if (b >= 25 && b < 125)   // Fala de 2 s depois de 0,5 s so de ruido.
            for (int i = 0; i < kVoiceFrame; i++) mic[static_cast<size_t>(i)] += speech[static_cast<size_t>(b * kVoiceFrame + i)];
        alice.feedMicrophone(mic.data(), mic.size());
        alice.update(0.02f, aliceEar, {brunoState});
        bruno.update(0.02f, brunoEar, {aliceState});
        std::vector<float> block(2 * kVoiceFrame);
        bruno.renderOutput(block.data());
        alice.renderOutput(ignored.data());
        heard.insert(heard.end(), block.begin(), block.end());
        aliceSpoke = aliceSpoke || alice.localSpeaking();
        brunoHeard = brunoHeard || bruno.speaking(0);
        brunoSpoke = brunoSpoke || bruno.localSpeaking();
    }
    run.check("Voz: quem fala ve localSpeaking e quem ouve ve speaking", aliceSpoke && brunoHeard && !brunoSpoke);
    run.check("Voz: ao fim da fala os dois indicadores apagam", !alice.localSpeaking() && !bruno.speaking(0));
    // Canal direito, durante a fala.
    std::vector<float> left, right;
    for (size_t i = 0; i < heard.size(); i += 2) { left.push_back(heard[i]); right.push_back(heard[i + 1]); }
    const size_t from = 50 * kVoiceFrame, count = 48000;
    const double measured = peakFrequency(right, from, count, 250, 500);
    run.check(describe("Voz: Bruno ouve a voz de passaro de Alice (Hz)", measured), near(measured, 300.0 * kBirdRatio, 3.0));
    const float rightLevel = rms(right.data() + from, count), leftLevel = rms(left.data() + from, count);
    run.check(describe("Voz: Alice a direita soa na direita (direita/esquerda)", rightLevel / std::max(leftLevel, 1e-6f)),
              rightLevel > 0.05f && rightLevel > 5 * leftLevel);
    run.check("Voz: sem eco da propria voz", alice.mixer().voices() == 0 && alice.stats().framesReceived == 0);
}
}

int runVoiceSelfTest() {
    TestRun run;
    testSpeechDetector(run);
    testPitchShifter(run);
    testOpus(run);
    testJitterBuffer(run);
    testSpatial(run);
    testDoppler(run);
    testBoundedDoppler(run);
    testMixing(run);
    testLoopbackChat(run);
    return run.failures;
}

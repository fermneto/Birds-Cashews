#include "vision/GestureLab.h"
#include "vision/Clock.h"
#include "vision/VisionOverlay.h"
#include "vision/VisionPipeline.h"
#include "core/TestRun.h"
#include "entity/FlightModel.h"
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

int runInferenceBenchmark(const std::string& modelPath) {
    PoseDetector detector(modelPath);
    cv::Mat frame(480, 640, CV_8UC3);
    cv::randu(frame, cv::Scalar::all(0), cv::Scalar::all(255));
    std::vector<double> samples;
    for (int i = 0; i < 120; i++) {
        const double before = nowSeconds();
        detector.detect(frame);
        if (i >= 20) samples.push_back((nowSeconds() - before) * 1000.0);
    }
    std::sort(samples.begin(), samples.end());
    double total = 0;
    for (double sample : samples) total += sample;
    const double mean = total / samples.size();
    std::cout << std::fixed << std::setprecision(1) << "Inferencia: media " << mean << " ms, p95 "
              << samples[samples.size() * 95 / 100] << " ms, " << 1000.0 / mean << " FPS\n";
    return 0;
}

int runCameraTest(int cameraIndex, const std::string& modelPath, int seconds) {
    VisionPipeline vision(cameraIndex, modelPath);
    if (!vision.start()) {
        std::cerr << "Nao foi possivel abrir a camera " << cameraIndex << "\n";
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    const VisionMetrics m = vision.metrics();
    vision.stop();
    if (m.processed == 0) {
        std::cerr << "A camera abriu, mas nenhum quadro foi processado\n";
        return 1;
    }
    const double n = static_cast<double>(m.processed);
    std::cout << std::fixed << std::setprecision(1)
              << "Captura: " << m.captured / m.seconds << " FPS\n"
              << "Processamento: " << n / m.seconds << " FPS\n"
              << "Inferencia: media " << m.inferenceMs / n << " ms, pior " << m.worstInferenceMs << " ms\n"
              << "Latencia captura-resultado: " << m.latencyMs / n << " ms\n"
              << "Quadros com pessoa: " << 100.0 * m.withPerson / n << "%\n"
              << "Quadros com ombros e punhos: " << 100.0 * m.withArms / n << "%\n";
    return 0;
}

int runGestureLab(int cameraIndex, const std::string& modelPath) {
    VisionPipeline vision(cameraIndex, modelPath);
    if (!vision.start()) {
        std::cerr << "Nao foi possivel abrir a camera " << cameraIndex << "\n";
        return 1;
    }
    VisionSnapshot view;
    uint64_t sequence = 0;
    while (true) {
        if (vision.snapshot(view, sequence)) {
            const VisionMetrics m = vision.metrics();
            drawVisionOverlay(view.frame, view.pose, view.gesture, m.processed / std::max(m.seconds, 0.001));
            cv::imshow("Birds&Cashews - laboratorio de gestos (Q para sair)", view.frame);
        }
        const int key = cv::waitKey(5) & 0xFF;
        if (key == 'q' || key == 27) break;
    }
    cv::destroyAllWindows();
    return 0;
}

// --- Testes com esqueletos sinteticos ---
namespace {
constexpr float kUnit = 96.0f;   // Escala do corpo sintetico: 0,6 x tronco de 160 px.
constexpr double kStep = 1.0 / 15.0;

Pose syntheticPose(float leftOut, float leftDrop, float rightOut, float rightDrop) {
    Pose pose;
    pose.valid = true;
    const auto set = [&](int joint, float x, float y) { pose.joints[joint] = {cv::Point2f(x, y), 0.9f}; };
    set(LShoulder, 280, 200);
    set(RShoulder, 360, 200);
    set(LHip, 295, 360);
    set(RHip, 345, 360);
    set(LWrist, 280 - leftOut * kUnit, 200 + leftDrop * kUnit);
    set(RWrist, 360 + rightOut * kUnit, 200 + rightDrop * kUnit);
    return pose;
}

using PoseAt = std::function<Pose(double)>;

GestureState simulate(GestureRecognizer& rec, const PoseAt& poseAt, double seconds, double step = kStep, double start = 0) {
    GestureState state = rec.state();
    for (double t = 0; t < seconds; t += step) state = rec.update(poseAt(t), start + t);
    return state;
}

Pose openArms(double) { return syntheticPose(1.5f, 0, 1.5f, 0); }

Pose tilted(float degrees) {
    const float reach = 1.9f, rad = degrees * static_cast<float>(CV_PI) / 180.0f;
    const float drop = reach * std::sin(rad), out = reach * std::cos(rad) - 0.42f;
    return syntheticPose(out, -drop, out, drop);
}

Pose flapPose(double t, double period) {
    const float drop = 0.4f - 0.9f * static_cast<float>(std::cos(2.0 * CV_PI * t / period));
    const float out = 1.0f - 0.3f * (drop + 0.5f);
    return syntheticPose(out, drop, out, drop);
}

void testGlideAndBank(TestRun& run) {
    GestureRecognizer rec;
    GestureState s = simulate(rec, openArms, 1.0);
    run.check("bracos abertos planam nivelados", s.mode == FlightMode::Glide && s.bank == 0);

    GestureRecognizer right;
    s = simulate(right, [](double) { return tilted(30); }, 1.0);
    run.check("braco direito baixo curva para a direita", s.mode == FlightMode::Glide && s.bank > 0.4f);

    GestureRecognizer left;
    s = simulate(left, [](double) { return tilted(-30); }, 1.0);
    run.check("braco esquerdo baixo curva para a esquerda", s.bank < -0.4f);

    GestureRecognizer small;
    s = simulate(small, [](double) { return tilted(5); }, 1.0);
    run.check("inclinacao de 5 graus cai na zona morta", s.bank == 0);
}

void testDive(TestRun& run) {
    GestureRecognizer rec;
    simulate(rec, openArms, 0.5);
    GestureState s = simulate(rec, [](double) { return syntheticPose(0.1f, 1.5f, 0.1f, 1.5f); }, 0.15, kStep, 0.5);
    run.check("mergulho espera a postura ser mantida", s.mode != FlightMode::Dive);
    s = simulate(rec, [](double) { return syntheticPose(0.1f, 1.5f, 0.1f, 1.5f); }, 0.5, kStep, 0.65);
    run.check("bracos colados mergulham", s.mode == FlightMode::Dive);
}

void testFlap(TestRun& run) {
    for (double fps : {30.0, 15.0, 10.0}) {
        GestureRecognizer rec;
        GestureState s = simulate(rec, [](double t) { return flapPose(t, 0.8); }, 4.0, 1.0 / fps);
        run.check("5 batidas contadas a " + std::to_string(static_cast<int>(fps)) + " FPS",
                  s.flapCount == 5 && s.mode == FlightMode::Flap);
    }
    GestureRecognizer slow;
    GestureState s = simulate(slow, [](double t) {
        const float drop = static_cast<float>(-0.5 + 1.8 * t / 4.0);
        return syntheticPose(1.0f, drop, 1.0f, drop);
    }, 4.0);
    run.check("baixar os bracos devagar nao conta batida", s.flapCount == 0);

    for (double duration : {1.0, 0.30}) {
        for (double fps : {10., 15., 30.}) {
            GestureRecognizer transition;
            const auto end = simulate(transition, [duration](double t) {
                const float progress = static_cast<float>(std::clamp((t - 0.5) / duration, 0.0, 1.0));
                return syntheticPose(1.5f - 1.4f * progress, 1.5f * progress,
                                     1.5f - 1.4f * progress, 1.5f * progress);
            }, 2.5, 1.0 / fps);
            const bool fast = duration < 0.5;
            run.check(std::string(fast ? "descida rapida conta uma batida" : "transicao lenta mergulha sem batida")
                      + " a " + std::to_string(int(fps)) + " FPS",
                      end.flapCount == unsigned(fast) && end.mode == FlightMode::Dive);
        }
    }
    // Mesmo arco de movimento por quatro segundos, em dois ritmos.
    for (int fps : {10, 15, 30}) {
        const auto flyCycle = [fps](double period, float amplitude = 0.9f) {
            FlightModel bird;
            bird.place({0, 150, 0}, 0, FlightModel::kCruiseSpeed);
            GestureRecognizer rec;
            FlightControls c;
            c.bodyControlled = true;
            for (int tick = 0; tick < 480; ++tick) {
                if (tick % (120 / fps) == 0) {
                    const double t = tick / 120.0;
                    const float drop = 0.4f - amplitude * std::cos(2 * CV_PI * t / period);
                    const auto g = rec.update(syntheticPose(1, drop, 1, drop), t);
                    c.stroke = g.stroke;
                    c.dive = g.dive;
                    c.pullUp = g.pullUp;
                }
                bird.step(c, 1.0f / 120);
            }
            return glm::vec2(bird.position().y, -bird.position().z / 4.0f);
        };
        const auto fast = flyCycle(0.8), slow = flyCycle(2.0), shortStroke = flyCycle(0.8, 0.45f);
        const float fastHeight = fast.x, slowHeight = slow.x;
        run.check("ritmo rapido aumenta velocidade horizontal media", fast.y > slow.y + 1);
        run.check("maior amplitude aumenta velocidade e sustentacao",
                  fast.y > shortStroke.y + 1 && fast.x > shortStroke.x);
        std::cout << "Voo em 4 s (" << fps << " FPS): lento " << slowHeight - 150
                  << ", rapido " << fastHeight - 150 << "\n";
        run.check("batidas confortaveis sustentam o voo", slowHeight >= 150);
        run.check("ciclos rapidos ganham mais altitude a " + std::to_string(fps) + " FPS",
                  fastHeight > 150 && fastHeight > slowHeight + 2);
    }
    GestureRecognizer asymmetric;
    auto wings = asymmetric.update(syntheticPose(1, -0.8f, 1, 0), 0.0);
    run.check("erguer so o punho esquerdo move so a asa esquerda",
              wings.leftWingHeight > 0.5f && wings.rightWingHeight == 0);
    wings = asymmetric.update(syntheticPose(1, 0, 1, -0.8f), 0.1);
    run.check("erguer so o punho direito move so a asa direita",
              wings.rightWingHeight > 0.5f && wings.leftWingHeight == 0);

    const auto extreme = simulate(asymmetric, [](double) {
        return syntheticPose(0.05f, -1.4f, 0.05f, 1.4f);
    }, 1.0, kStep, 0.2);
    run.check("bracos verticais opostos sustentam a curva", extreme.bank > 0.8f);
    const auto reversed = asymmetric.update(syntheticPose(.05f, 1.4f, .05f, -1.4f), 1.24);
    run.check("trocar os bracos suaviza a inversao", reversed.bank > -0.5f && reversed.bank < extreme.bank);
    GestureRecognizer oneArm;
    s = simulate(oneArm, [](double t) {
        return syntheticPose(1.0f, 0.4f - 0.9f * static_cast<float>(std::cos(2.0 * CV_PI * t / 0.8)), 1.0f, 0.0f);
    }, 4.0);
    run.check("balancar um braco so nao conta batida", s.flapCount == 0);
}

void testTracking(TestRun& run) {
    GestureRecognizer rec;
    simulate(rec, openArms, 0.5);
    GestureState s = simulate(rec, [](double) { return Pose(); }, 0.13, kStep, 0.5);
    run.check("falha curta mantem o comando", s.mode == FlightMode::Glide);
    s = simulate(rec, [](double) { return Pose(); }, 1.0, kStep, 0.65);
    run.check("sem jogador o passaro nivela", s.mode == FlightMode::Lost && s.bank == 0);

    Pose pose = syntheticPose(1, 0, 1, 0).mirrored(640);
    run.check("espelhar preserva os rotulos do corpo",
              pose.at(LShoulder).x == 640 - 280.0f && pose.at(RShoulder).x == 640 - 360.0f);
}

void testMenu(TestRun& run) {
    GestureRecognizer rec;
    GestureState s = simulate(rec, [](double) { return syntheticPose(0.3f, -1.2f, 0.2f, 1.2f); }, 1.5);
    run.check("braco esquerdo erguido escolhe uma vez",
              s.menuCount == 1 && s.menuChoice == ArmSide::Left);

    GestureRecognizer other;
    s = simulate(other, [](double) { return syntheticPose(0.2f, 1.2f, 0.3f, -1.2f); }, 1.5);
    run.check("braco direito erguido escolhe a outra opcao", s.menuChoice == ArmSide::Right);

    GestureRecognizer both;
    s = simulate(both, [](double) { return syntheticPose(0.3f, -1.2f, 0.3f, -1.2f); }, 1.5);
    run.check("dois bracos erguidos nao escolhem", s.menuCount == 0);

    GestureRecognizer quick;
    s = simulate(quick, [](double) { return syntheticPose(0.3f, -1.2f, 0.2f, 1.2f); }, 0.4);
    run.check("gesto rapido nao confirma o menu", s.menuCount == 0);
}
}

// Centro do alvo claro da imagem, em coordenadas continuas (o pixel i cobre [i, i+1)).
cv::Point2f brightCenter(const cv::Mat& image) {
    cv::Mat gray, mask;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, mask, 127, 255, cv::THRESH_BINARY);
    const cv::Moments m = cv::moments(mask, true);
    if (m.m00 <= 0) return {-1000, -1000};
    return {static_cast<float>(m.m10 / m.m00) + 0.5f, static_cast<float>(m.m01 / m.m00) + 0.5f};
}

std::string pixels(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

// Faz o caminho de um ponto conhecido pela cadeia da visao: imagem da camera, letterbox,
// saida da rede numa celula, volta a imagem e espelho. O ponto final precisa cair sobre
// o mesmo alvo na imagem espelhada que o jogador ve.
void testCoordinateChain(TestRun& run) {
    const cv::Point2f cell = PoseDetector::keypointFromCell(0, 0, 5, 3, 32);
    run.check("ponto da rede parte do canto da celula, como na Ultralytics", cell == cv::Point2f(160, 96));
    run.check("deslocamento bruto vale duas celulas por unidade",
              PoseDetector::keypointFromCell(0.25f, -0.5f, 10, 7, 8) == cv::Point2f(84, 48));

    for (const cv::Size& size : {cv::Size(640, 480), cv::Size(1280, 720), cv::Size(480, 640)}) {
        const std::string name = std::to_string(size.width) + "x" + std::to_string(size.height);
        cv::Mat frame(size, CV_8UC3, cv::Scalar::all(0));
        const cv::Rect target(size.width * 3 / 4, size.height / 5, 8, 8);
        cv::rectangle(frame, target, cv::Scalar::all(255), cv::FILLED);
        const cv::Point2f truth = brightCenter(frame);

        cv::Point2f scale, pad;
        const cv::Mat square = PoseDetector::letterbox(frame, scale, pad);
        const cv::Point2f seen = brightCenter(square);
        run.check("letterbox " + name + " vira quadrado do modelo", square.size() == cv::Size(320, 320));

        // Rede ideal: devolve o alvo pela formula da Ultralytics na celula de passo 32 que o contem.
        const int stride = 32, cx = static_cast<int>(seen.x / stride), cy = static_cast<int>(seen.y / stride);
        Pose pose;
        pose.valid = true;
        pose.joints[RWrist] = {PoseDetector::keypointFromCell((seen.x / stride - cx) / 2, (seen.y / stride - cy) / 2, cx, cy, stride), 1};
        PoseDetector::restoreCoordinates(pose, scale, pad);
        const float error = static_cast<float>(cv::norm(pose.at(RWrist) - truth));
        run.check("ida e volta do letterbox " + name + " acha o alvo (erro " + pixels(error) + " px)", error < 1.5f);

        cv::Mat mirrored;
        cv::flip(frame, mirrored, 1);
        const Pose shown = pose.mirrored(size.width);
        const float mirrorError = static_cast<float>(cv::norm(shown.at(RWrist) - brightCenter(mirrored)));
        run.check("espelho " + name + " leva o ponto ao alvo espelhado (erro " + pixels(mirrorError) + " px)", mirrorError < 1.5f);

        // O esqueleto desenhado sobre a imagem espelhada marca o pixel do alvo.
        cv::Mat drawn(size, CV_8UC3, cv::Scalar::all(0));
        drawVisionSkeleton(drawn, shown);
        const cv::Point center(static_cast<int>(brightCenter(mirrored).x), static_cast<int>(brightCenter(mirrored).y));
        run.check("ponto " + name + " desenhado sobre o alvo", drawn.at<cv::Vec3b>(center) == cv::Vec3b(40, 200, 255));
    }
}

int runGestureSelfTest() {
    TestRun run;
    testGlideAndBank(run);
    testDive(run);
    GestureRecognizer analog;
    auto partial = simulate(analog, [](double) { return syntheticPose(.5f,.72f,.5f,.72f); }, 1.5);
    run.check("bracos a meia altura produzem mergulho parcial", partial.dive > .4f && partial.dive < .6f);
    auto turning = simulate(analog, [](double) {
        auto pose = syntheticPose(.1f,1.5f,.1f,1.5f);
        pose.joints[RShoulder].pos.y += 36;
        pose.joints[RWrist].pos.y += 36;
        return pose;
    }, 1.5, kStep, 1.5);
    run.check("inclinacao do tronco curva durante mergulho", turning.dive > .85f && turning.bank > .25f);
    testFlap(run);
    testTracking(run);
    testMenu(run);
    testCoordinateChain(run);
    return run.failures;
}

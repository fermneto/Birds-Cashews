#include "ai/NpcTests.h"
#include "core/FlightTests.h"
#include "core/Game.h"
#include "game/CashewTests.h"
#include "game/NestTests.h"
#include "net/NetTests.h"
#include "procedural/BirdFactoryTests.h"
#include "save/SaveTests.h"
#include "shared/ContractTests.h"
#include "ui/UiTests.h"
#include "vision/GestureLab.h"
#include "voice/VoiceDemo.h"
#include "voice/VoiceTests.h"
#include <SDL.h>
#include <unistd.h>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {
void printUsage() {
    std::cout << "Uso:\n"
              << "  ./birds                    voa com o corpo, pela webcam\n"
              << "  ./birds --teclado          voa com o teclado\n"
              << "  ./birds --camera N         escolhe a webcam\n"
              << "  ./birds --gestos [camera]  laboratorio de gestos\n"
              << "  ./birds --selftest         testes automaticos\n"
              << "  ./birds --benchmark        mede a inferencia da pose\n"
              << "  ./birds --camera-test [segundos] [camera]\n"
              << "  ./birds --render-test PASTA\n"
              << "  ./birds --soak [segundos]\n"
              << "  ./birds --voz-teste        voz de passaro com um falante virtual (Ctrl+C sai)\n"
              << "  ./birds --voz-teste arquivo SAIDA.wav\n";
}

// O modelo fica ao lado do executavel, entao o jogo abre de qualquer pasta.
std::string modelPath() {
    char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "./") + "models/pose_trimmed.onnx";
    SDL_free(base);
    return path;
}

// Os modos de teste do jogo gravam aqui, longe da pasta saves/ real.
class ScratchSaves {
public:
    ScratchSaves() : path_(std::filesystem::temp_directory_path() / ("birds-saves-" + std::to_string(::getpid()))) {}
    ~ScratchSaves() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

int runSelfTests() {
    const int failures = runGestureSelfTest() + runFlightSelfTest() + runContractSelfTest() + runCashewSelfTest()
                       + runSaveSelfTest() + runNestSelfTest() + runUiSelfTest() + runNpcSelfTest()
                       + runNetworkSelfTest() + runBirdFactorySelfTest() + runVoiceSelfTest();
    std::cout << (failures == 0 ? "Todos os testes passaram\n" : "Ha testes falhando\n");
    return failures == 0 ? 0 : 1;
}
}

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const auto number = [&](size_t i, int fallback) { return args.size() > i ? std::stoi(args[i]) : fallback; };
    try {
        GameOptions options;
        options.modelPath = modelPath();
        if (args.empty()) return Game(options).run();
        const std::string& command = args[0];
        if (command == "--help") { printUsage(); return 0; }
        if (command == "--selftest") return runSelfTests();
        if (command == "--benchmark") return runInferenceBenchmark(options.modelPath);
        if (command == "--camera-test") return runCameraTest(number(2, 0), options.modelPath, number(1, 10));
        if (command == "--gestos") return runGestureLab(number(1, 0), options.modelPath);
        if (command == "--voz-teste") return args.size() == 3 && args[1] == "arquivo" ? runVoiceFileDemo(args[2]) : runVoiceDemo();
        if (command == "--teclado") { options.keyboard = true; return Game(options).run(); }
        if (command == "--camera") { options.cameraIndex = number(1, 0); return Game(options).run(); }
        ScratchSaves scratch;
        options.saveDir = scratch.path();
        if (command == "--render-test" && args.size() == 2) { options.hidden = true; return Game(options).renderTest(args[1]); }
        if (command == "--soak") { options.hidden = true; return Game(options).soakTest(number(1, 15)); }
        printUsage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Erro: " << error.what() << "\n";
        return 1;
    }
}

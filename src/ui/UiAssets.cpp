#include "ui/UiAssets.h"
#include "ui/Canvas.h"
#include <SDL.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {
const char* const kIconFiles[] = {
    "caju.png", "moeda.png", "ninho.png", "seta_bussola.png", "selo_record.png",
    "upgrade_velocidade.png", "upgrade_agilidade.png", "upgrade_carga.png", "upgrade_alcance.png",
    "upgrade_defesa.png", "upgrade_alarme.png", "upgrade_cofre.png", "upgrade_tamanho.png"};

struct OptionKeyword { const char* word; UiIcon icon; };
const OptionKeyword kKeywords[] = {
    {"velocidade", UiIcon::UpgradeSpeed}, {"agilidade", UiIcon::UpgradeAgility},
    {"carga", UiIcon::UpgradeCargo}, {"capacidade", UiIcon::UpgradeCargo},
    {"alcance", UiIcon::UpgradeRange}, {"coleta", UiIcon::UpgradeRange},
    {"defesa", UiIcon::UpgradeDefense}, {"alarme", UiIcon::UpgradeAlarm},
    {"cofre", UiIcon::UpgradeVault}, {"tamanho", UiIcon::UpgradeSize},
    {"comer", UiIcon::Cashew}, {"investir", UiIcon::Nest}};
}

std::filesystem::path executableDirectory() {
    char* base = SDL_GetBasePath();
    const std::filesystem::path path = base ? base : "./";
    SDL_free(base);
    return path;
}

std::filesystem::path assetsDirectory() {
    return executableDirectory() / "assets";
}

const char* iconFileName(UiIcon icon) {
    return kIconFiles[static_cast<size_t>(icon)];
}

std::optional<UiIcon> iconForOption(const std::string& optionName) {
    std::string lower = optionName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const OptionKeyword& keyword : kKeywords)
        if (lower.find(keyword.word) != std::string::npos) return keyword.icon;
    return std::nullopt;
}

UiAssets::UiAssets(const std::filesystem::path& directory) : directory_(directory) {
    for (int i = 0; i < static_cast<int>(UiIcon::Count); i++) {
        const std::string path = (directory_ / kIconFiles[i]).string();
        const cv::Mat image = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (image.empty()) throw std::runtime_error("Imagem nao encontrada: " + path);
        if (image.type() != CV_8UC4) throw std::runtime_error("Imagem sem canal alfa: " + path);
        icons_.push_back(premultiplied(image));
    }
    if (!std::filesystem::exists(fontPath())) throw std::runtime_error("Fonte nao encontrada: " + fontPath());
}

// Reduzir com a cor pre-multiplicada evita bordas escuras ao redor das imagens.
const cv::Mat& UiAssets::icon(UiIcon which, int size) {
    const std::pair<int, int> key(static_cast<int>(which), size);
    const auto found = scaled_.find(key);
    if (found != scaled_.end()) return found->second;
    cv::Mat resized;
    cv::resize(original(which), resized, cv::Size(size, size), 0, 0, cv::INTER_AREA);
    return scaled_.emplace(key, resized).first->second;
}

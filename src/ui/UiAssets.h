#pragma once
#include <opencv2/core.hpp>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class UiIcon { Cashew, Coin, Nest, CompassArrow, RecordBadge,
                    UpgradeSpeed, UpgradeAgility, UpgradeCargo, UpgradeRange,
                    UpgradeDefense, UpgradeAlarm, UpgradeVault, UpgradeSize, Count };

// Pasta assets/ ao lado do executavel.
std::filesystem::path assetsDirectory();
std::filesystem::path executableDirectory();

const char* iconFileName(UiIcon icon);

// Escolhe o icone pelo nome mostrado no menu do ninho, sem depender de maiusculas.
std::optional<UiIcon> iconForOption(const std::string& optionName);

// PNGs com canal alfa, carregados em tempo de execucao com cv::IMREAD_UNCHANGED.
class UiAssets {
public:
    explicit UiAssets(const std::filesystem::path& directory);

    const cv::Mat& original(UiIcon icon) const { return icons_[static_cast<size_t>(icon)]; }
    const cv::Mat& icon(UiIcon icon, int size);
    std::string fontPath() const { return (directory_ / "font.ttf").string(); }

private:
    std::filesystem::path directory_;
    std::vector<cv::Mat> icons_;
    std::map<std::pair<int, int>, cv::Mat> scaled_;
};

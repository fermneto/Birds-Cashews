#include "save/RecordManager.h"
#include "save/KeyValueFile.h"
#include "save/Profile.h"
#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

namespace {
const char* const kFormat = "record";
constexpr int kVersion = 1;

struct RecordData {
    int value = 0;
    std::string holder;
};

std::string encodeRecord(const RecordData& data) {
    KeyValueWriter writer(kFormat, kVersion);
    writer.put("record", int64_t{data.value});
    writer.put("detentor", data.holder);
    return writer.finish();
}

RecordData decodeRecord(const std::string& text, bool& valid) {
    KeyValueReader reader(text, kFormat, kVersion);
    RecordData data;
    // Limitar um valor absurdo criaria um record impossivel de bater: ele e descartado.
    data.value = static_cast<int>(reader.bounded("record", 0, 0, kMaxProfileCounter));
    data.holder = reader.text("detentor", "");

    const bool holderMatches = data.value > 0 ? isValidProfileName(data.holder) : data.holder.empty();
    if (!holderMatches) {
        reader.markInvalid();
        data.holder.clear();
    }
    valid = reader.valid();
    return data;
}
}

RecordManager::RecordManager(const fs::path& baseDir) : path_(baseDir / "record.txt") {
    std::error_code error;
    fs::create_directories(baseDir, error);
    refreshFromDisk();
}

// Outra instancia do jogo neste computador pode ter gravado um record maior.
void RecordManager::refreshFromDisk() {
    const SaveFileContent content = readSaveFile(path_);
    if (!content.exists) return;
    bool valid = false;
    const RecordData data = decodeRecord(content.text, valid);
    if (data.value > record_) {
        record_ = data.value;
        holder_ = data.holder;
    }
}

bool RecordManager::registerTotal(const std::string& name, int total) {
    if (!isValidProfileName(name)) return false;
    refreshFromDisk();
    const int limited = std::min(total, kMaxProfileCounter);
    if (limited <= record_) return false;

    const bool overtook = name != holder_;
    record_ = limited;
    holder_ = name;
    const auto isValid = [](const std::string& text) {
        bool valid = false;
        decodeRecord(text, valid);
        return valid;
    };
    replaceSaveFile(path_, encodeRecord({record_, holder_}), isValid);
    return overtook;
}

void RecordManager::startSession(const std::string& name, int total) {
    noticeShown_ = false;
    registerTotal(name, total);
}

bool RecordManager::update(const std::string& name, int total) {
    const bool overtook = registerTotal(name, total);
    if (!overtook || noticeShown_) return false;
    noticeShown_ = true;
    return true;
}

bool RecordManager::isLeading(const std::string& name, int total) const {
    return record_ > 0 && name == holder_ && total >= record_;
}

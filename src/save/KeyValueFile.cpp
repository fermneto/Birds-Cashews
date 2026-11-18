#include "save/KeyValueFile.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <fstream>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
// Comporta o maior mundo possivel com folga e impede ler arquivos gigantes por engano.
constexpr uintmax_t kMaxSaveFileBytes = 4 * 1024 * 1024;
constexpr int kMaxCorruptCopies = 10;

enum class NumberKind { Valid, TooLow, TooHigh, NotANumber };

NumberKind parseNumber(const std::string& text, int64_t& value) {
    const char* const end = text.data() + text.size();
    const auto [next, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || next != end) return NumberKind::NotANumber;
    if (error == std::errc::result_out_of_range) return text[0] == '-' ? NumberKind::TooLow : NumberKind::TooHigh;
    return error == std::errc() ? NumberKind::Valid : NumberKind::NotANumber;
}

// Nome exclusivo, para duas instancias do jogo nao disputarem o mesmo temporario.
fs::path temporaryPathFor(const fs::path& path) {
    static std::atomic<unsigned> counter{0};
    return path.string() + "." + std::to_string(::getpid()) + "." + std::to_string(counter++) + ".tmp";
}

bool writeAll(int file, const std::string& content) {
    size_t written = 0;
    while (written < content.size()) {
        const ssize_t count = ::write(file, content.data() + written, content.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        written += static_cast<size_t>(count);
    }
    return true;
}

// Garante que o rename chegue ao disco. Falhar aqui nao invalida a gravacao ja feita.
void syncDirectory(const fs::path& directory) {
    const int handle = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (handle < 0) return;
    ::fsync(handle);
    ::close(handle);
}

// Usa o primeiro nome livre. Esgotadas as copias, somente a ultima e substituida.
fs::path freeCorruptPath(const fs::path& path) {
    const std::string base = path.string() + ".corrompido";
    std::error_code error;
    if (!fs::exists(fs::symlink_status(base, error))) return base;
    for (int i = 1; i < kMaxCorruptCopies; i++) {
        const std::string candidate = base + "." + std::to_string(i);
        if (!fs::exists(fs::symlink_status(candidate, error))) return candidate;
    }
    return base + "." + std::to_string(kMaxCorruptCopies - 1);
}
}

KeyValueWriter::KeyValueWriter(const std::string& format, int version) {
    put("formato", format);
    put("versao", int64_t{version});
}

void KeyValueWriter::put(const std::string& key, const std::string& value) {
    text_ += key + "=" + value + "\n";
}

void KeyValueWriter::put(const std::string& key, int64_t value) {
    put(key, std::to_string(value));
}

std::string KeyValueWriter::finish() const {
    return text_ + "fim=1\n";
}

KeyValueReader::KeyValueReader(const std::string& text, const std::string& format, int version) {
    bool finished = false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const size_t separator = line.find('=');
        if (finished || separator == std::string::npos || separator == 0) {
            valid_ = false;
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "fim") {
            finished = value == "1";
            valid_ = valid_ && finished;
        } else if (!entries_.emplace(key, value).second) {
            valid_ = false;
        }
    }
    if (!finished) valid_ = false;

    // Outro formato ou versao desconhecida: nenhum valor do arquivo e confiavel.
    const auto formatEntry = entries_.find("formato");
    const auto versionEntry = entries_.find("versao");
    const bool sameFormat = formatEntry != entries_.end() && formatEntry->second == format;
    const bool sameVersion = versionEntry != entries_.end() && versionEntry->second == std::to_string(version);
    if (!sameFormat || !sameVersion) {
        valid_ = false;
        entries_.clear();
    }
}

std::string KeyValueReader::text(const std::string& key, const std::string& fallback) {
    const auto entry = entries_.find(key);
    if (entry != entries_.end()) return entry->second;
    valid_ = false;
    return fallback;
}

int64_t KeyValueReader::clamped(const std::string& key, int64_t fallback, int64_t min, int64_t max) {
    const auto entry = entries_.find(key);
    int64_t value = fallback;
    const NumberKind kind = entry == entries_.end() ? NumberKind::NotANumber : parseNumber(entry->second, value);
    if (kind == NumberKind::Valid && value >= min && value <= max) return value;
    valid_ = false;
    if (kind == NumberKind::NotANumber) return fallback;
    if (kind == NumberKind::TooLow) return min;
    if (kind == NumberKind::TooHigh) return max;
    return std::clamp(value, min, max);
}

int64_t KeyValueReader::bounded(const std::string& key, int64_t fallback, int64_t min, int64_t max) {
    const auto entry = entries_.find(key);
    int64_t value = fallback;
    if (entry != entries_.end() && parseNumber(entry->second, value) == NumberKind::Valid && value >= min &&
        value <= max) {
        return value;
    }
    valid_ = false;
    return fallback;
}

SaveFileContent readSaveFile(const fs::path& path) {
    SaveFileContent content;
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return content;
    content.exists = true;
    const uintmax_t size = fs::file_size(path, error);
    if (error || size > kMaxSaveFileBytes) return content;

    std::ifstream file(path, std::ios::binary);
    std::string text(static_cast<size_t>(size), '\0');
    if (file && file.read(text.data(), static_cast<std::streamsize>(text.size()))) content.text = std::move(text);
    return content;
}

bool writeFileAtomic(const fs::path& path, const std::string& content) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);

    const fs::path temporary = temporaryPathFor(path);
    const int file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (file < 0) return false;
    bool saved = writeAll(file, content) && ::fsync(file) == 0;
    saved = ::close(file) == 0 && saved;
    if (saved) {
        fs::rename(temporary, path, error);
        saved = !error;
    }
    if (!saved) {
        fs::remove(temporary, error);
        return false;
    }
    syncDirectory(path.parent_path());
    return true;
}

bool preserveCorruptCopy(const fs::path& path) {
    const fs::path temporary = temporaryPathFor(path);
    std::error_code error;
    fs::copy_file(path, temporary, fs::copy_options::overwrite_existing, error);
    if (!error) fs::rename(temporary, freeCorruptPath(path), error);
    if (!error) return true;
    std::error_code ignored;
    fs::remove(temporary, ignored);
    return false;
}

bool replaceSaveFile(const fs::path& path, const std::string& content,
                     const std::function<bool(const std::string&)>& isValid) {
    const SaveFileContent current = readSaveFile(path);
    if (current.exists && !isValid(current.text) && !preserveCorruptCopy(path)) return false;
    return writeFileAtomic(path, content);
}

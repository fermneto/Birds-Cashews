#include "save/SaveTests.h"
#include "core/TestRun.h"
#include "save/KeyValueFile.h"
#include "save/ProfileStore.h"
#include "save/RecordManager.h"
#include "save/WorldStore.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
// Pasta temporaria exclusiva, apagada ao final. Os testes nunca usam a pasta saves/ real.
class TempDir {
public:
    TempDir() {
        std::error_code error;
        fs::path root = fs::temp_directory_path(error);
        if (error) root = "/tmp";
        std::random_device random;
        do {
            path_ = root / ("birds-save-test-" + std::to_string(::getpid()) + "-" + std::to_string(random()));
        } while (!fs::create_directory(path_, error) && !error);
    }
    ~TempDir() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    // Cada teste recebe a propria subpasta, ainda inexistente.
    fs::path fresh(const std::string& name) const { return path_ / name; }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void writeRaw(const fs::path& path, const std::string& text) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

std::string readRaw(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

bool hasTemporaryFiles(const fs::path& directory) {
    std::error_code error;
    for (fs::recursive_directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (it->path().extension() == ".tmp") return true;
    }
    return false;
}

size_t countFiles(const fs::path& directory) {
    size_t count = 0;
    std::error_code error;
    for (fs::recursive_directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) count++;
    }
    return count;
}

fs::path profilePath(const fs::path& base, const std::string& name) {
    return base / "perfis" / ProfileStore::fileNameFor(name);
}

// Texto valido de um perfil, com uma linha trocada para simular dano.
std::string profileText(const std::string& name, const std::string& changedKey = "",
                        const std::string& changedValue = "") {
    const std::vector<std::pair<std::string, std::string>> lines = {
        {"formato", "perfil"}, {"versao", "1"}, {"nome", name}, {"semente", "77"}, {"moedas", "12"},
        {"total_entregue", "34"}, {"passaro.velocidade", "1"}, {"passaro.agilidade", "2"}, {"passaro.carga", "3"},
        {"passaro.alcance", "4"}, {"ninho.defesa", "5"}, {"ninho.alarme", "0"}, {"ninho.cofre", "1"},
        {"ninho.tamanho", "2"}, {"fim", "1"}};
    std::string text;
    for (const auto& [key, value] : lines) text += key + "=" + (key == changedKey ? changedValue : value) + "\n";
    return text;
}

void testProfileRoundTrip(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("ida_e_volta");
    {
        ProfileStore store(base);
        run.check("perfil: pasta base criada", fs::is_directory(base));
        auto created = store.create("Ana Clara", 4000000000u);
        run.check("perfil: criado com semente", created && created->birdSeed() == 4000000000u);
        run.check("perfil: nao recria um existente", !store.create("Ana Clara", 1));
        created->deliverCashews(9, 2);
        created->spendCoins(4);
        created->upgrade(BirdUpgrade::Speed);
        created->upgrade(BirdUpgrade::Speed);
        created->upgrade(BirdUpgrade::CollectRange);
        created->upgrade(NestUpgrade::Vault);
        created->upgrade(NestUpgrade::Size);
        run.check("perfil: salvo", store.save(*created));
    }

    // Um novo objeto so conhece o que esta no disco, como depois de reiniciar o jogo.
    ProfileStore reopened(base);
    LoadStatus status = LoadStatus::Missing;
    const auto loaded = reopened.load("Ana Clara", &status);
    run.check("perfil: reaberto do disco", loaded.has_value() && status == LoadStatus::Ok);
    if (!loaded) return;
    run.check("perfil: nome preservado", loaded->name() == "Ana Clara");
    run.check("perfil: semente preservada", loaded->birdSeed() == 4000000000u);
    run.check("perfil: moedas preservadas", loaded->coins() == 5);
    run.check("perfil: total preservado", loaded->totalDelivered() == 7);
    run.check("perfil: upgrades do passaro preservados",
              loaded->level(BirdUpgrade::Speed) == 2 && loaded->level(BirdUpgrade::Agility) == 0 &&
                  loaded->level(BirdUpgrade::CarryCapacity) == 0 && loaded->level(BirdUpgrade::CollectRange) == 1);
    run.check("perfil: upgrades do ninho preservados",
              loaded->level(NestUpgrade::Defense) == 0 && loaded->level(NestUpgrade::Alarm) == 0 &&
                  loaded->level(NestUpgrade::Vault) == 1 && loaded->level(NestUpgrade::Size) == 1);

    LoadStatus missing = LoadStatus::Ok;
    run.check("perfil: ausente nao carrega", !reopened.load("Ninguem", &missing) && missing == LoadStatus::Missing);
}

void testDelivery(TestRun& run) {
    Profile profile("Bia", 1);
    run.check("entrega: retorna as moedas", profile.deliverCashews(5, 2) == 5);
    run.check("entrega: roubados viram moeda", profile.coins() == 5);
    run.check("entrega: roubados nao somam no total", profile.totalDelivered() == 3);

    profile.deliverCashews(4, 4);
    run.check("entrega: so roubados nao mudam o total", profile.coins() == 9 && profile.totalDelivered() == 3);
    profile.deliverCashews(2, 10);
    run.check("entrega: roubados limitados ao entregue", profile.coins() == 11 && profile.totalDelivered() == 3);
    profile.deliverCashews(2, -3);
    run.check("entrega: roubados negativos valem zero", profile.coins() == 13 && profile.totalDelivered() == 5);
    run.check("entrega: quantidade negativa e ignorada",
              profile.deliverCashews(-4, 0) == 0 && profile.coins() == 13 && profile.totalDelivered() == 5);

    run.check("moedas: gasto sem saldo e recusado", !profile.spendCoins(14) && profile.coins() == 13);
    run.check("moedas: gasto negativo e recusado", !profile.spendCoins(-1) && profile.coins() == 13);
    run.check("moedas: gasto com saldo", profile.spendCoins(13) && profile.coins() == 0);

    Profile full = Profile::restored("Cau", 1, kMaxProfileCounter, kMaxProfileCounter - 1, {}, {});
    full.deliverCashews(10, 0);
    run.check("entrega: contadores nao estouram",
              full.coins() == kMaxProfileCounter && full.totalDelivered() == kMaxProfileCounter);
}

void testUpgradeLimits(TestRun& run) {
    Profile profile("Dan", 1);
    bool allRaised = true;
    for (int i = 0; i < kMaxUpgradeLevel; i++) allRaised = profile.upgrade(BirdUpgrade::Agility) && allRaised;
    run.check("nivel: sobe ate o maximo", allRaised && profile.level(BirdUpgrade::Agility) == 5);
    run.check("nivel: indica o maximo", profile.isMaxed(BirdUpgrade::Agility));
    run.check("nivel: nao passa do maximo",
              !profile.upgrade(BirdUpgrade::Agility) && profile.level(BirdUpgrade::Agility) == 5);
    run.check("nivel: upgrades sao independentes", profile.level(BirdUpgrade::Speed) == 0);

    for (int i = 0; i < kMaxUpgradeLevel + 3; i++) profile.upgrade(NestUpgrade::Alarm);
    run.check("nivel: ninho tambem limita em 5", profile.level(NestUpgrade::Alarm) == 5);
    run.check("nivel: ninho indica o maximo", profile.isMaxed(NestUpgrade::Alarm) && !profile.isMaxed(NestUpgrade::Size));

    const Profile restored = Profile::restored("Dan", 1, -5, -5, {9, -2, 5, 0}, {-1, 6, 0, 3});
    run.check("nivel: restaurar limita os valores",
              restored.level(BirdUpgrade::Speed) == 5 && restored.level(BirdUpgrade::Agility) == 0 &&
                  restored.level(NestUpgrade::Defense) == 0 && restored.level(NestUpgrade::Alarm) == 5 &&
                  restored.coins() == 0 && restored.totalDelivered() == 0);
}

void testDangerousNames(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("nomes");
    ProfileStore store(base);

    bool allRejected = true;
    const std::vector<std::string> invalid = {"", " ", "    ", "linha\nquebrada", "tab\tulado", std::string("nu\0lo", 5),
                                              std::string(kMaxProfileNameBytes + 1, 'a')};
    for (const std::string& name : invalid) {
        allRejected = allRejected && !store.create(name, 1) && ProfileStore::fileNameFor(name).empty();
    }
    run.check("nomes: invalidos sao recusados", allRejected);
    run.check("nomes: nada gravado para invalidos", countFiles(base) == 0);
    run.check("nomes: perfil invalido nao salva", !store.save(Profile("", 1)));

    const std::vector<std::string> dangerous = {"..", ".", "../fora", "../../fora", "/etc/passwd", "a/b", "a\\b",
                                                "..\\fora", "~", "-rf", "*", "a=b", "nome.perfil", "x.tmp", "record.txt",
                                                std::string(kMaxProfileNameBytes, '/')};
    bool safeFiles = true;
    bool roundTrip = true;
    uint32_t seed = 100;
    for (const std::string& name : dangerous) {
        const std::string fileName = ProfileStore::fileNameFor(name);
        safeFiles = safeFiles && !fileName.empty() && fileName.size() <= 255 &&
                    fileName.find('/') == std::string::npos && fileName.find('\\') == std::string::npos &&
                    fileName.find("..") == std::string::npos && fileName[0] != '.' && fileName[0] != '-';
        const auto created = store.create(name, ++seed);
        const auto loaded = store.load(name);
        roundTrip = roundTrip && created && loaded && loaded->name() == name && loaded->birdSeed() == seed;
    }
    run.check("nomes: arquivos seguros", safeFiles);
    run.check("nomes: perigosos funcionam sem perder o nome", roundTrip);
    run.check("nomes: um arquivo para cada nome", countFiles(base / "perfis") == dangerous.size());
    run.check("nomes: nada escapa da pasta de perfis", countFiles(base) == countFiles(base / "perfis"));
    run.check("nomes: nada escreve fora da pasta base", !fs::exists(temp.path() / "fora"));
    const std::vector<std::string> listed = store.list();
    run.check("nomes: listagem devolve os nomes originais",
              std::set<std::string>(listed.begin(), listed.end()) ==
                  std::set<std::string>(dangerous.begin(), dangerous.end()));
}

void testNameCollisions(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("colisao");
    ProfileStore store(base);
    const std::vector<std::string> names = {"ana", "Ana", "ANA", "ana ", " ana", "a na", "a/na", "a\\na", "a_na",
                                            "a-na", "a.na", "a%2fna", "a%2Fna", "%61na", "ana.perfil", "ação", "acao"};
    std::set<std::string> files;
    uint32_t seed = 0;
    bool created = true;
    for (const std::string& name : names) {
        files.insert(ProfileStore::fileNameFor(name));
        created = store.create(name, ++seed).has_value() && created;
    }
    run.check("colisao: nomes diferentes geram arquivos diferentes", files.size() == names.size());
    run.check("colisao: todos foram criados", created && countFiles(base / "perfis") == names.size());

    bool intact = true;
    seed = 0;
    for (const std::string& name : names) {
        const auto loaded = store.load(name);
        intact = intact && loaded && loaded->name() == name && loaded->birdSeed() == ++seed;
    }
    run.check("colisao: nenhum perfil sobrescreveu outro", intact);

    std::set<std::string> folded;
    for (std::string file : files) {
        std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return std::tolower(c); });
        folded.insert(file);
    }
    run.check("colisao: arquivos distintos mesmo ignorando a caixa", folded.size() == names.size());
}

void testListing(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("listagem");
    ProfileStore store(base);
    run.check("lista: vazia sem perfis", store.list().empty());
    store.create("Zeca", 1);
    store.create("Ana", 2);
    store.create("Mia", 3);

    const fs::path directory = base / "perfis";
    writeRaw(directory / "anotacao.txt", "nao e perfil");
    writeRaw(directory / "lixo.perfil.123.4.tmp", "temporario abandonado");
    writeRaw(directory / "ana.perfil.corrompido", "copia");
    writeRaw(directory / "%zz.perfil", "codificacao invalida");
    writeRaw(directory / "Maiuscula.perfil", "nao foi gerado pelo jogo");
    writeRaw(directory / ".perfil", "sem nome");
    fs::create_directories(directory / "pasta.perfil");

    run.check("lista: ordenada e sem arquivos estranhos",
              store.list() == std::vector<std::string>({"Ana", "Mia", "Zeca"}));
    run.check("lista: consulta de existencia", store.exists("Ana") && !store.exists("Bob") && !store.exists(""));
    run.check("lista: outra instancia enxerga os perfis", ProfileStore(base).list().size() == 3);
}

void testWorld(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("mundo");
    {
        WorldStore store(base);
        LoadStatus status = LoadStatus::Ok;
        const WorldState empty = store.load(321, &status);
        run.check("mundo: ausente usa a semente de reserva",
                  status == LoadStatus::Missing && empty.seed == 321 && empty.unavailable.empty() && !store.exists());

        WorldState state;
        state.seed = 4294967295u;
        state.elapsedSeconds = 12345.678;
        state.unavailable = {{42, 3.5f}, {7, 120.25f}, {65534, 0.0f}};
        run.check("mundo: salvo", store.save(state));
    }

    WorldStore reopened(base);
    LoadStatus status = LoadStatus::Missing;
    const WorldState loaded = reopened.load(1, &status);
    run.check("mundo: reaberto do disco", status == LoadStatus::Ok && reopened.exists());
    run.check("mundo: semente preservada", loaded.seed == 4294967295u);
    run.check("mundo: tempo preservado", std::abs(loaded.elapsedSeconds - 12345.678) < 0.001);
    run.check("mundo: cajus preservados", loaded.unavailable.size() == 3);
    if (loaded.unavailable.size() == 3) {
        run.check("mundo: cajus com o tempo restante",
                  loaded.unavailable[0].id == 7 && std::abs(loaded.unavailable[0].secondsLeft - 120.25f) < 0.001f &&
                      loaded.unavailable[1].id == 42 && std::abs(loaded.unavailable[1].secondsLeft - 3.5f) < 0.001f &&
                      loaded.unavailable[2].id == 65534 && loaded.unavailable[2].secondsLeft == 0.0f);
    }

    WorldState odd;
    odd.seed = 9;
    odd.elapsedSeconds = std::numeric_limits<double>::quiet_NaN();
    odd.unavailable = {{1, -5.0f}, {2, std::numeric_limits<float>::infinity()}, {3, std::nanf("")}, {4, 8.0f},
                       {4, 2.0f}, {kNoCashew, 5.0f}};
    run.check("mundo: valores estranhos sao gravados", reopened.save(odd));
    const WorldState cleaned = reopened.load(1, &status);
    run.check("mundo: gravacao sempre gera arquivo valido", status == LoadStatus::Ok);
    run.check("mundo: tempo invalido vira zero", cleaned.elapsedSeconds == 0.0);
    run.check("mundo: id reservado e repetido sao removidos", cleaned.unavailable.size() == 4);
    if (cleaned.unavailable.size() == 4) {
        run.check("mundo: tempos de caju limitados",
                  cleaned.unavailable[0].secondsLeft == 0.0f &&
                      cleaned.unavailable[1].secondsLeft == kMaxCashewRespawnSeconds &&
                      cleaned.unavailable[2].secondsLeft == 0.0f && cleaned.unavailable[3].secondsLeft == 8.0f);
    }
}

void testRecordBetweenProfiles(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("record");
    {
        RecordManager record(base);
        run.check("record: comeca zerado", record.record() == 0 && record.holder().empty());
        run.check("record: total zero nao vira record", !record.update("Ana", 0) && record.holder().empty());
        run.check("record: ninguem lidera sem record", !record.isLeading("Ana", 0));
        run.check("record: primeiro total cria o record", record.update("Ana", 10) && record.record() == 10);
        run.check("record: detentor registrado", record.holder() == "Ana");
        run.check("record: total menor nao muda", !record.update("Bia", 7) && record.record() == 10);
        run.check("record: empate nao muda o detentor", !record.update("Bia", 10) && record.holder() == "Ana");
        run.check("record: empate nao lidera", record.isLeading("Ana", 10) && !record.isLeading("Bia", 10));
    }
    run.check("record: arquivo com o nome esperado", fs::is_regular_file(base / "record.txt"));
    {
        RecordManager reopened(base);
        run.check("record: sobrevive ao reinicio", reopened.record() == 10 && reopened.holder() == "Ana");
        reopened.update("Bia", 15);
        run.check("record: outro perfil assume", reopened.record() == 15 && reopened.holder() == "Bia");
        run.check("record: lideranca muda", reopened.isLeading("Bia", 15) && !reopened.isLeading("Ana", 10));
        run.check("record: total abaixo do record nao lidera", !reopened.isLeading("Bia", 3));
    }
    RecordManager last(base);
    run.check("record: novo detentor sobrevive ao reinicio", last.record() == 15 && last.holder() == "Bia");
    run.check("record: nome invalido e ignorado", !last.update("", 99) && !last.update("a\nb", 99) && last.record() == 15);
    run.check("record: total negativo e ignorado", !last.update("Cau", -1) && last.record() == 15);
    last.update("Cau", std::numeric_limits<int>::max());
    run.check("record: total limitado", last.record() == kMaxProfileCounter);

    // Duas instancias abertas: a segunda nao pode rebaixar o record gravado pela primeira.
    const fs::path shared = temp.fresh("record_compartilhado");
    RecordManager first(shared);
    RecordManager second(shared);
    first.update("Ana", 20);
    second.update("Bia", 12);
    run.check("record: instancia desatualizada nao rebaixa", RecordManager(shared).record() == 20);
    run.check("record: instancia desatualizada se atualiza", second.record() == 20 && second.holder() == "Ana");
}

void testNoticeOnce(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("aviso");
    writeRaw(base / "record.txt", "formato=record\nversao=1\nrecord=10\ndetentor=Ana\nfim=1\n");
    RecordManager record(base);
    record.startSession("Bia", 5);
    run.check("aviso: nao dispara abaixo do record", !record.update("Bia", 9));
    run.check("aviso: nao dispara ao empatar", !record.update("Bia", 10));
    run.check("aviso: dispara ao ultrapassar", record.update("Bia", 11));
    run.check("aviso: nao repete na entrega seguinte", !record.update("Bia", 12));
    run.check("aviso: nao repete com o mesmo total", !record.update("Bia", 12));
    run.check("aviso: record segue subindo", record.record() == 12);
    run.check("aviso: outro perfil na mesma sessao nao repete", !record.update("Cau", 13) && record.holder() == "Cau");

    record.startSession("Dan", 0);
    run.check("aviso: nova sessao libera o aviso", record.update("Dan", 14));
    run.check("aviso: nova sessao tambem avisa uma vez", !record.update("Dan", 15));

    RecordManager quiet(base);
    quiet.startSession("Eva", 40);
    run.check("aviso: abrir perfil com total maior registra sem avisar", quiet.record() == 40 && quiet.holder() == "Eva");
    run.check("aviso: abrir perfil ja deixa o HUD correto", quiet.isLeading("Eva", 40));
    run.check("aviso: detentor recem aberto nao recebe aviso", !quiet.update("Eva", 41));
    run.check("aviso: abrir perfil nao gasta o aviso da sessao", quiet.update("Fabi", 42));
}

void testHolderRaisesOwnRecord(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("detentor");
    writeRaw(base / "record.txt", "formato=record\nversao=1\nrecord=10\ndetentor=Ana\nfim=1\n");
    {
        RecordManager record(base);
        record.startSession("Ana", 10);
        run.check("detentor: lidera ao abrir", record.isLeading("Ana", 10));
        run.check("detentor: aumentar nao dispara aviso", !record.update("Ana", 11));
        run.check("detentor: aumentar atualiza o valor", record.record() == 11);
        run.check("detentor: continua liderando", record.isLeading("Ana", 11));
    }
    {
        RecordManager reopened(base);
        run.check("detentor: arquivo atualizado", reopened.record() == 11 && reopened.holder() == "Ana");
        reopened.startSession("Ana", 11);
        run.check("detentor: outra sessao tambem nao avisa", !reopened.update("Ana", 12));
        run.check("detentor: outra sessao atualiza", RecordManager(base).record() == 12);
        run.check("detentor: rival ainda pode avisar", reopened.update("Bia", 13));
    }
}

void testCorruptProfiles(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("perfil_corrompido");
    ProfileStore store(base);
    const fs::path path = profilePath(base, "Ana");
    const std::string intact = profileText("Ana");

    writeRaw(path, intact);
    LoadStatus status = LoadStatus::Missing;
    const auto good = store.load("Ana", &status);
    run.check("corrompido: arquivo integro e aceito", good && status == LoadStatus::Ok && good->coins() == 12);

    // Todo corte possivel do arquivo, byte a byte, precisa ser detectado.
    bool everyCutDetected = true;
    bool everyCutSafe = true;
    for (size_t size = 0; size + 1 < intact.size(); size++) {
        writeRaw(path, intact.substr(0, size));
        const auto cut = store.load("Ana", &status);
        everyCutDetected = everyCutDetected && status == LoadStatus::Recovered;
        everyCutSafe = everyCutSafe && cut && cut->name() == "Ana" && cut->coins() >= 0 && cut->coins() <= 12 &&
                       cut->level(BirdUpgrade::Speed) <= kMaxUpgradeLevel;
    }
    run.check("corrompido: qualquer truncamento e detectado", everyCutDetected);
    run.check("corrompido: truncado carrega valores seguros", everyCutSafe);

    writeRaw(path, intact.substr(0, intact.find("moedas=")));
    const auto partial = store.load("Ana", &status);
    run.check("corrompido: truncado aproveita o inicio", partial && partial->birdSeed() == 77 && partial->coins() == 0);

    std::string binary(2048, '\0');
    std::mt19937 generator(1234);
    for (char& c : binary) c = static_cast<char>(generator() & 255);
    writeRaw(path, binary);
    const auto garbage = store.load("Ana", &status);
    run.check("corrompido: lixo binario usa padroes",
              garbage && status == LoadStatus::Recovered && garbage->coins() == 0 && garbage->totalDelivered() == 0);
    run.check("corrompido: semente de reserva e estavel", garbage && store.load("Ana")->birdSeed() == garbage->birdSeed());

    writeRaw(path, "");
    run.check("corrompido: arquivo vazio", store.load("Ana", &status) && status == LoadStatus::Recovered);

    writeRaw(path, profileText("Ana", "moedas", "99999999999999999999999"));
    const auto hugeCoins = store.load("Ana", &status);
    run.check("absurdo: moedas gigantes", hugeCoins && status == LoadStatus::Recovered && hugeCoins->coins() == 0);
    writeRaw(path, profileText("Ana", "total_entregue", "2000000000"));
    const auto hugeTotal = store.load("Ana", &status);
    run.check("absurdo: total nao contamina o record", hugeTotal && hugeTotal->totalDelivered() == 0);
    writeRaw(path, profileText("Ana", "moedas", "-50"));
    run.check("absurdo: moedas negativas", store.load("Ana", &status)->coins() == 0 && status == LoadStatus::Recovered);
    writeRaw(path, profileText("Ana", "moedas", "doze"));
    run.check("absurdo: moedas sem numero", store.load("Ana", &status)->coins() == 0 && status == LoadStatus::Recovered);
    writeRaw(path, profileText("Ana", "passaro.velocidade", "500"));
    run.check("absurdo: nivel limitado ao maximo", store.load("Ana", &status)->level(BirdUpgrade::Speed) == 5 &&
                                                       status == LoadStatus::Recovered);
    writeRaw(path, profileText("Ana", "ninho.defesa", "-99999999999999999999999"));
    run.check("absurdo: nivel limitado ao minimo", store.load("Ana", &status)->level(NestUpgrade::Defense) == 0);
    writeRaw(path, profileText("Ana", "semente", "4294967296"));
    run.check("absurdo: semente fora de 32 bits", store.load("Ana", &status) && status == LoadStatus::Recovered);

    writeRaw(path, profileText("Ana", "versao", "2"));
    const auto future = store.load("Ana", &status);
    run.check("versao: desconhecida nao e interpretada", future && status == LoadStatus::Recovered && future->coins() == 0);
    writeRaw(path, profileText("Ana", "formato", "mundo"));
    run.check("versao: formato errado", store.load("Ana", &status)->coins() == 0 && status == LoadStatus::Recovered);
    writeRaw(path, profileText("Outra"));
    const auto foreign = store.load("Ana", &status);
    run.check("versao: nome divergente", foreign && foreign->name() == "Ana" && status == LoadStatus::Recovered);
    writeRaw(path, intact + "moedas=99\n");
    run.check("versao: conteudo depois do fim", store.load("Ana", &status) && status == LoadStatus::Recovered);
    writeRaw(path, "moedas=1\n" + intact);
    run.check("versao: chave repetida", store.load("Ana", &status) && status == LoadStatus::Recovered);

    // Copia do arquivo invalido: so nasce ao sobrescrever e nunca apaga copias anteriores.
    const fs::path copy = path.string() + ".corrompido";
    const std::string firstDamage = intact.substr(0, 40);
    writeRaw(path, firstDamage);
    auto recovered = store.load("Ana", &status);
    run.check("copia: carregar nao altera o disco", !fs::exists(copy) && readRaw(path) == firstDamage);
    recovered->deliverCashews(3, 0);
    run.check("copia: salvar sobre arquivo invalido funciona", store.save(*recovered));
    run.check("copia: original preservado em .corrompido", readRaw(copy) == firstDamage);
    run.check("copia: arquivo novo fica integro", store.load("Ana", &status) && status == LoadStatus::Ok);

    run.check("copia: salvar arquivo integro nao copia", store.save(*recovered) && !fs::exists(copy.string() + ".1"));
    writeRaw(path, "segundo dano");
    store.save(*recovered);
    run.check("copia: primeira copia mantida", readRaw(copy) == firstDamage);
    run.check("copia: segunda copia separada", readRaw(copy.string() + ".1") == "segundo dano");
    run.check("copia: nao aparece na listagem", store.list() == std::vector<std::string>({"Ana"}));

    const fs::path bigPath = profilePath(base, "Grande");
    // O conteudo seria valido: so o tamanho impede a leitura.
    const std::string padding = "comentario=" + std::string(5 * 1024 * 1024, 'x') + "\n";
    const std::string bigText = padding + profileText("Grande");
    writeRaw(bigPath, bigText);
    const auto big = store.load("Grande", &status);
    run.check("absurdo: arquivo gigante nao e lido", big && status == LoadStatus::Recovered && big->coins() == 0);
    run.check("absurdo: arquivo gigante e preservado",
              big && store.save(*big) && readRaw(bigPath.string() + ".corrompido") == bigText);
}

void testCorruptWorld(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("mundo_corrompido");
    WorldStore store(base);
    const fs::path path = base / "mundo.txt";
    const std::string intact = "formato=mundo\nversao=1\nsemente=55\ntempo_ms=9000\ncaju.3=1500\ncaju.9=2500\nfim=1\n";
    LoadStatus status = LoadStatus::Missing;

    writeRaw(path, intact);
    run.check("mundo corrompido: integro e aceito", store.load(1, &status).unavailable.size() == 2 && status == LoadStatus::Ok);

    bool everyCutDetected = true;
    for (size_t size = 0; size + 1 < intact.size(); size++) {
        writeRaw(path, intact.substr(0, size));
        store.load(1, &status);
        everyCutDetected = everyCutDetected && status == LoadStatus::Recovered;
    }
    run.check("mundo corrompido: truncamento detectado", everyCutDetected);

    writeRaw(path, "\x7f" "ELF lixo sem sentido");
    const WorldState garbage = store.load(808, &status);
    run.check("mundo corrompido: lixo usa a semente de reserva",
              garbage.seed == 808 && garbage.elapsedSeconds == 0.0 && garbage.unavailable.empty() &&
                  status == LoadStatus::Recovered);

    writeRaw(path, "formato=mundo\nversao=1\nsemente=-4\ntempo_ms=999999999999999999999\ncaju.1=-7\ncaju.2=99999999999\n"
                   "caju.65535=10\ncaju.70000=10\ncaju.abc=10\ncaju.03=10\nfim=1\n");
    const WorldState absurd = store.load(808, &status);
    run.check("mundo absurdo: detectado", status == LoadStatus::Recovered);
    run.check("mundo absurdo: semente de reserva", absurd.seed == 808);
    run.check("mundo absurdo: tempo limitado", absurd.elapsedSeconds == kMaxWorldElapsedSeconds);
    run.check("mundo absurdo: ids invalidos descartados", absurd.unavailable.size() == 2);
    if (absurd.unavailable.size() == 2) {
        run.check("mundo absurdo: esperas limitadas",
                  absurd.unavailable[0].secondsLeft == 0.0f &&
                      absurd.unavailable[1].secondsLeft == kMaxCashewRespawnSeconds);
    }

    const std::string damaged = readRaw(path);
    run.check("mundo corrompido: salvar funciona", store.save(absurd));
    run.check("mundo corrompido: copia preservada", readRaw(path.string() + ".corrompido") == damaged);
    store.load(808, &status);
    run.check("mundo corrompido: arquivo novo fica integro", status == LoadStatus::Ok);
}

void testCorruptRecord(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("record_corrompido");
    const fs::path path = base / "record.txt";
    fs::create_directories(base);
    const std::string intact = "formato=record\nversao=1\nrecord=30\ndetentor=Ana\nfim=1\n";

    writeRaw(path, intact);
    run.check("record corrompido: integro e aceito", RecordManager(base).record() == 30);

    bool everyCutSafe = true;
    for (size_t size = 0; size + 1 < intact.size(); size++) {
        writeRaw(path, intact.substr(0, size));
        const RecordManager cut(base);
        everyCutSafe = everyCutSafe && cut.record() >= 0 && cut.record() <= 30;
    }
    run.check("record corrompido: truncado nao derruba", everyCutSafe);

    const auto recordFrom = [&](const std::string& value, const std::string& holder) {
        writeRaw(path, "formato=record\nversao=1\nrecord=" + value + "\ndetentor=" + holder + "\nfim=1\n");
        return RecordManager(base);
    };
    run.check("record absurdo: negativo", recordFrom("-10", "Ana").record() == 0);
    run.check("record absurdo: gigante nao fica imbativel", recordFrom("99999999999999", "Ana").record() == 0);
    run.check("record absurdo: acima do limite", recordFrom("1000000001", "Ana").record() == 0);
    run.check("record absurdo: sem numero", recordFrom("muito", "Ana").holder().empty());
    run.check("record absurdo: sem detentor mantem o valor", recordFrom("8", "").record() == 8);
    run.check("record absurdo: detentor sem record e ignorado", recordFrom("0", "Ana").holder().empty());

    writeRaw(path, std::string("lixo\0\0\xff", 7) + " sem formato");
    const std::string damaged = readRaw(path);
    RecordManager record(base);
    run.check("record corrompido: lixo comeca zerado", record.record() == 0 && record.holder().empty());
    run.check("record corrompido: ainda aceita novo record", record.update("Bia", 3));
    run.check("record corrompido: copia preservada", readRaw(path.string() + ".corrompido") == damaged);
    const RecordManager reopened(base);
    run.check("record corrompido: arquivo novo fica integro", reopened.record() == 3 && reopened.holder() == "Bia");

    // Uma pasta no lugar do arquivo nao pode derrubar o jogo.
    const fs::path blocked = temp.fresh("record_bloqueado");
    fs::create_directories(blocked / "record.txt");
    RecordManager unusable(blocked);
    run.check("record corrompido: pasta no lugar do arquivo", unusable.update("Ana", 5) && unusable.record() == 5);
    run.check("record corrompido: bloqueado nao deixa temporarios", !hasTemporaryFiles(blocked));
}

void testAtomicWrites(TestRun& run, const TempDir& temp) {
    const fs::path base = temp.fresh("atomico");
    {
        ProfileStore profiles(base);
        WorldStore world(base);
        RecordManager record(base);
        Profile profile = *profiles.create("Ana", 1);
        bool allSaved = true;
        for (int i = 1; i <= 25; i++) {
            profile.deliverCashews(1, 0);
            allSaved = profiles.save(profile) && allSaved;
            WorldState state;
            state.seed = static_cast<uint32_t>(i);
            allSaved = world.save(state) && allSaved;
            record.update("Ana", i);
        }
        run.check("atomico: todas as gravacoes concluidas", allSaved);
    }
    run.check("atomico: sem temporarios para tras", !hasTemporaryFiles(base));
    run.check("atomico: somente os arquivos finais", countFiles(base) == 3);
    run.check("atomico: ultimo estado no disco", ProfileStore(base).load("Ana")->totalDelivered() == 25);

    // Temporario largado por uma queda do jogo nao interfere no arquivo real.
    const fs::path path = profilePath(base, "Ana");
    writeRaw(path.string() + ".999.0.tmp", "gravacao interrompida");
    LoadStatus status = LoadStatus::Missing;
    const auto loaded = ProfileStore(base).load("Ana", &status);
    run.check("atomico: queda na gravacao preserva o arquivo", loaded && status == LoadStatus::Ok && loaded->coins() == 25);
    run.check("atomico: temporario abandonado nao vira perfil", ProfileStore(base).list().size() == 1);

    const fs::path blocked = temp.fresh("atomico_bloqueado");
    ProfileStore store(blocked);
    fs::create_directories(profilePath(blocked, "Ana"));
    run.check("atomico: falha informada sem derrubar", !store.save(Profile("Ana", 1)));
    run.check("atomico: falha nao deixa temporarios", !hasTemporaryFiles(blocked));

    const fs::path notDirectory = temp.fresh("arquivo_no_lugar_da_pasta");
    writeRaw(notDirectory, "sou um arquivo");
    ProfileStore impossible(notDirectory);
    WorldStore impossibleWorld(notDirectory);
    RecordManager impossibleRecord(notDirectory);
    run.check("atomico: pasta base impossivel nao derruba",
              !impossible.create("Ana", 1) && impossible.list().empty() && !impossibleWorld.save(WorldState{}));
    run.check("atomico: record segue em memoria sem pasta", impossibleRecord.update("Ana", 2) && impossibleRecord.record() == 2);
    run.check("atomico: arquivo no lugar da pasta intacto", readRaw(notDirectory) == "sou um arquivo");
}
}

int runSaveSelfTest() {
    TestRun run;
    const TempDir temp;
    run.check("pasta temporaria dos testes criada", fs::is_directory(temp.path()));
    testProfileRoundTrip(run, temp);
    testDelivery(run);
    testUpgradeLimits(run);
    testDangerousNames(run, temp);
    testNameCollisions(run, temp);
    testListing(run, temp);
    testWorld(run, temp);
    testRecordBetweenProfiles(run, temp);
    testNoticeOnce(run, temp);
    testHolderRaisesOwnRecord(run, temp);
    testCorruptProfiles(run, temp);
    testCorruptWorld(run, temp);
    testCorruptRecord(run, temp);
    testAtomicWrites(run, temp);
    return run.failures;
}

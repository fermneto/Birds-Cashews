#pragma once
#include "net/Protocol.h"
#include "shared/CashewQuery.h"
#include "shared/GameAuthority.h"
#include <cstddef>
#include <vector>

class World;

// Papeis do ecossistema. So o cacador derruba cajus; os demais nunca mexem na carga de ninguem.
enum class NpcRole : uint8_t {
    Flock,        // Bando pequeno com Boids.
    Forager,      // Procura cajus pelo chao das copas.
    Hunter,       // Persegue quem carrega 2 cajus ou mais e derruba a carga.
    Curious,      // Segue um jogador de perto por alguns segundos e depois se afasta.
    Skittish,     // Fica pousado no alto de uma copa e foge quando alguem chega perto.
    Glider,       // Circula alto, subindo em termicas.
    Territorial,  // Expulsa quem chega perto da sua arvore, sem tocar na carga.
    Migrant,      // Bando em V que atravessa a ilha de ponta a ponta.
    Count
};

inline constexpr size_t kNpcRoleCount = static_cast<size_t>(NpcRole::Count);
static_assert(kNpcRoleCount <= net::kNpcRoleLimit, "O papel precisa caber no byte de comportamento do snapshot");
const char* npcRoleName(NpcRole role);
// Quanto do planeio vira rajada de batidas na animacao: o planador quase nunca bate asa.
float npcGlideFlaps(NpcRole role);

struct NpcBird {
    NpcRole role;
    uint32_t seed;
    glm::vec3 position, velocity, home;
    float yaw = 0, pitch = 0, roll = 0;
    float cooldown = 0;
    FlightMode mode = FlightMode::Glide;
    float radius = 1.5f;       // Raio de colisao, do tamanho do passaro gerado.
    bool perched = false;
    int stage = 0;             // Etapa da maquina de estados do papel.
    float timer = 0;           // Tempo restante na etapa atual.
    PlayerId target = kNoPlayer;
    int anchor = 0;            // Arvore, termica ou rota em uso, conforme o papel.
    int slot = 0;              // Posicao no bando em V (0 = lider).
};

// Estado publico de um NPC para o snapshot.
net::NpcPublicState npcPublicState(const NpcBird& bird);

class NpcEcosystem {
public:
    static constexpr size_t kCount = 22;
    static_assert(kCount <= net::kMaxNpcs, "Todos os NPCs precisam caber no snapshot");

    NpcEcosystem(const World& world, uint32_t seed);

    void advance(float elapsed, const std::vector<PlayerPublicState>& players,
                 const CashewQuery& cashews, GameAuthority& authority);
    const std::vector<NpcBird>& birds() const { return birds_; }
    std::vector<net::NpcPublicState> publicStates() const;

    // Pousos no alto das copas, ja afastados das folhas pelo raio do passaro.
    glm::vec3 perchOn(size_t tree, float radius) const;

private:
    void step(const std::vector<PlayerPublicState>& players,
              const CashewQuery& cashews, GameAuthority& authority);
    void stepSkittish(NpcBird& bird, const std::vector<PlayerPublicState>& players);
    void stepGlider(NpcBird& bird);
    void stepTerritorial(NpcBird& bird, const std::vector<PlayerPublicState>& players);
    void stepCurious(NpcBird& bird, const std::vector<PlayerPublicState>& players);
    void stepMigrant(NpcBird& bird, const NpcBird* leader);
    float cruiseHeight(glm::vec2 from, glm::vec2 to) const;

    const World& world_;
    std::vector<NpcBird> birds_;
    std::vector<size_t> perchTrees_;    // Arvores perto do inicio, uma por passaro assustado.
    std::vector<glm::vec3> thermals_;   // Centros das termicas, com a altura da base.
    size_t guardedTree_ = 0;
    float accumulator_ = 0;
};

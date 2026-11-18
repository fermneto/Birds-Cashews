#pragma once
#include <glm/glm.hpp>
#include <vector>

class MeshBuilder;
class Rng;

struct SphereCollider { glm::vec3 center; float radius; };
struct CapsuleCollider { glm::vec3 a, b; float radius; };

// Cajueiro gigante: tronco baixo e grosso, galhos abertos e copa larga.
class CashewTree {
public:
    static constexpr float kCanopySquash = 0.66f;   // Copas largas e achatadas.

    CashewTree(glm::vec3 base, Rng& rng);

    bool resolve(glm::vec3& position, float radius, glm::vec3& normal) const;
    void appendMesh(MeshBuilder& mesh, Rng& rng) const;

    glm::vec3 base() const { return base_; }
    float height() const { return height_; }
    float trunkRadius() const { return trunkRadius_; }
    float reach() const { return reach_; }
    const std::vector<SphereCollider>& canopy() const { return canopy_; }

private:
    void growTrunk(Rng& rng);
    void growBranch(float angle, Rng& rng);
    void addCapsules(const std::vector<glm::vec3>& points, float baseRadius, float tipRadius);

    glm::vec3 base_;
    float height_, trunkRadius_, reach_ = 0;
    glm::vec3 crown_{0};
    std::vector<glm::vec3> trunk_;
    std::vector<std::vector<glm::vec3>> branches_;
    std::vector<SphereCollider> canopy_;
    std::vector<CapsuleCollider> capsules_;
};

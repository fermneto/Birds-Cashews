#pragma once
#include <glm/glm.hpp>
#include <vector>

class Rng;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

// Monta malhas low-poly com sombreamento por face. Nao depende de OpenGL.
class MeshBuilder {
public:
    void triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 color);
    void quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color);
    void tube(const std::vector<glm::vec3>& points, const std::vector<float>& radii, int sides, glm::vec3 color);
    void blob(glm::vec3 center, float radius, glm::vec3 color, Rng& rng, int subdivisions = 1, float roughness = 0.1f,
              float squash = 1.0f);

    const std::vector<Vertex>& vertices() const { return vertices_; }

private:
    void outwardTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 center, glm::vec3 color);
    std::vector<Vertex> vertices_;
};

#include "gfx/MeshBuilder.h"
#include "world/Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

void MeshBuilder::triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 color) {
    const glm::vec3 cross = glm::cross(b - a, c - a);
    const float length = glm::length(cross);
    if (length < 1e-6f) return;
    const glm::vec3 normal = cross / length;
    vertices_.push_back({a, normal, color});
    vertices_.push_back({b, normal, color});
    vertices_.push_back({c, normal, color});
}

void MeshBuilder::quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color) {
    triangle(a, b, c, color);
    triangle(a, c, d, color);
}

void MeshBuilder::outwardTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 center, glm::vec3 color) {
    const glm::vec3 middle = (a + b + c) / 3.0f;
    if (glm::dot(glm::cross(b - a, c - a), middle - center) >= 0) triangle(a, b, c, color);
    else triangle(a, c, b, color);
}

void MeshBuilder::tube(const std::vector<glm::vec3>& points, const std::vector<float>& radii, int sides, glm::vec3 color) {
    const auto ring = [&](size_t index) {
        const size_t next = std::min(index + 1, points.size() - 1), previous = index == 0 ? 0 : index - 1;
        const glm::vec3 axis = glm::normalize(points[next] - points[previous]);
        const glm::vec3 helper = std::abs(axis.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 u = glm::normalize(glm::cross(axis, helper)), v = glm::cross(axis, u);
        std::vector<glm::vec3> out;
        for (int s = 0; s < sides; s++) {
            const float angle = 6.2831853f * s / sides;
            out.push_back(points[index] + (u * std::cos(angle) + v * std::sin(angle)) * radii[index]);
        }
        return out;
    };
    for (size_t i = 0; i + 1 < points.size(); i++) {
        const auto low = ring(i), high = ring(i + 1);
        const glm::vec3 center = (points[i] + points[i + 1]) * 0.5f;
        for (int s = 0; s < sides; s++) {
            const int n = (s + 1) % sides;
            outwardTriangle(low[s], low[n], high[n], center, color);
            outwardTriangle(low[s], high[n], high[s], center, color);
        }
    }
}

void MeshBuilder::blob(glm::vec3 center, float radius, glm::vec3 color, Rng& rng, int subdivisions, float roughness,
                       float squash) {
    const float t = 1.618034f;
    std::vector<glm::vec3> points = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                     {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::vector<std::array<int, 3>> faces = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
                                             {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                             {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
                                             {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (auto& point : points) point = glm::normalize(point);

    for (int level = 0; level < subdivisions; level++) {
        std::map<std::pair<int, int>, int> middles;
        const auto middle = [&](int a, int b) {
            const std::pair<int, int> key = std::minmax(a, b);
            const auto found = middles.find(key);
            if (found != middles.end()) return found->second;
            points.push_back(glm::normalize(points[a] + points[b]));
            return middles[key] = static_cast<int>(points.size()) - 1;
        };
        std::vector<std::array<int, 3>> finer;
        for (const auto& f : faces) {
            const int ab = middle(f[0], f[1]), bc = middle(f[1], f[2]), ca = middle(f[2], f[0]);
            finer.push_back({f[0], ab, ca});
            finer.push_back({f[1], bc, ab});
            finer.push_back({f[2], ca, bc});
            finer.push_back({ab, bc, ca});
        }
        faces = std::move(finer);
    }
    for (auto& point : points) {
        point *= radius * rng.range(1.0f - roughness, 1.0f + roughness);
        point = center + glm::vec3(point.x, point.y * squash, point.z);
    }
    for (const auto& f : faces) {
        const float shade = rng.range(0.9f, 1.08f);
        outwardTriangle(points[f[0]], points[f[1]], points[f[2]], center, color * shade);
    }
}

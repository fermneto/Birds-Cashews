#include "shared/BirdFactory.h"
#include "world/Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace {
enum class Crest { None, Tuft, Fan, Swept };
enum class Tail { Fan, Forked, Wedge, Square, Long, Streamers };
enum class Tip { Pointed, Rounded, Slotted };

// Cores ja resolvidas para cada zona do corpo.
struct Palette {
    glm::vec3 back, belly, crown, face, throat, collar, breast;
    glm::vec3 wing, wingRear, wingTip, wingBar, tail, tailTip, crest, beak, beakLow, legs, eyeRing;
    bool collarBand, breastBand, wingBand, tailBand, eyeRingOn;
    int barPanel;
};

struct Genome {
    float length, width, height;
    float head, neckReach, posture, chest, rump;
    float beakLength, beakDepth, beakCurve;
    Crest crest;
    float crestHeight, crestSweep;
    int crestPlumes;
    Tail tail;
    float tailLength, tailSpread;
    Tip tip;
    float span, chord, sweep, dihedral;
    float legs, eye;
    Palette palette;
};

float wrap(float hue) { return hue - std::floor(hue); }

glm::vec3 hsv(float hue, float saturation, float value) {
    const float sector = wrap(hue) * 6.0f;
    const glm::vec3 channels{0, 4, 2};
    glm::vec3 color;
    for (int i = 0; i < 3; ++i) {
        const float distance = std::abs(std::fmod(sector + channels[i], 6.0f) - 3.0f);
        color[i] = value * (1.0f - saturation * (1.0f - std::clamp(distance - 1.0f, 0.0f, 1.0f)));
    }
    return color;
}

float luma(glm::vec3 color) { return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b; }

// Paleta por harmonia de cores: complementar, triadica, complementar dividida, tetradica,
// analoga com destaque oposto ou base neutra (preto ou branco) com dois tons vivos.
Palette makePalette(Rng& rng) {
    // O tom principal pula a faixa do verde das copas (0,22 a 0,42), para o passaro nao sumir nas folhas.
    float hue = 0.8f * rng.unit();
    if (hue >= 0.22f) hue += 0.2f;
    const int scheme = rng.range(0, 5);
    std::array<float, 3> hues{};
    switch (scheme) {
        case 0: hues = {hue, hue + 0.5f, hue + 0.08f}; break;
        case 1: hues = {hue, hue + 1.0f / 3.0f, hue + 2.0f / 3.0f}; break;
        case 2: hues = {hue, hue + 0.42f, hue + 0.58f}; break;
        case 3: hues = {hue, hue + 0.25f, hue + 0.5f}; break;
        case 4: hues = {hue, hue + 0.09f, hue + 0.5f}; break;
        default: hues = {hue, hue + 0.5f, hue + 0.33f}; break;
    }
    const auto vivid = [&](float h) { return hsv(h, rng.range(0.78f, 1.0f), rng.range(0.80f, 1.0f)); };
    std::array<glm::vec3, 5> slot{};   // Principal, segunda, terceira, escuro e claro.
    slot[0] = vivid(hues[0]);
    slot[1] = vivid(hues[1]);
    slot[2] = vivid(hues[2]);
    slot[3] = hsv(hues[1], rng.range(0.30f, 0.60f), rng.range(0.10f, 0.18f));
    slot[4] = hsv(hues[2], rng.range(0.04f, 0.12f), rng.range(0.93f, 1.0f));
    if (scheme == 5) {
        const bool black = rng.unit() < 0.5f;
        slot[2] = slot[0];
        slot[0] = black ? slot[3] : slot[4];
    }
    // Sem contraste de brilho entre as duas cores maiores, a segunda escurece ou clareia.
    if (std::abs(luma(slot[0]) - luma(slot[1])) < 0.22f)
        slot[1] = hsv(hues[1], 0.95f, luma(slot[0]) > 0.5f ? 0.42f : 1.0f);

    // Cada escolha sorteia uma das cores, evitando repetir a que ja esta ao lado.
    const auto pick = [&](std::initializer_list<int> options, glm::vec3 avoid) {
        std::array<int, 5> allowed{};
        int count = 0;
        for (int option : options)
            if (glm::length(slot[option] - avoid) > 0.30f) allowed[count++] = option;
        if (count == 0) for (int option : options) allowed[count++] = option;
        return slot[allowed[rng.range(0, count - 1)]];
    };
    Palette p{};
    p.back = slot[0];
    p.belly = pick({1, 4, 2}, p.back);
    switch (rng.range(0, 4)) {
        case 0: p.crown = p.face = p.back; p.throat = p.belly; break;        // Liso.
        case 1: p.crown = p.face = p.throat = pick({1, 2, 3}, p.back); break; // Capuz.
        case 2: p.crown = pick({2, 3}, p.back); p.face = pick({4, 1}, p.crown); p.throat = p.belly; break;   // Bone.
        case 3: p.crown = p.back; p.face = pick({3, 2}, p.back); p.throat = pick({4, 1}, p.face); break;    // Mascara.
        default: p.crown = p.face = p.back; p.throat = pick({2, 3, 4}, p.belly); break;                    // Babador.
    }
    p.collarBand = rng.unit() < 0.35f;
    p.collar = pick({4, 2, 3}, p.back);
    p.breastBand = rng.unit() < 0.30f;
    p.breast = pick({2, 3, 1}, p.belly);
    p.wing = rng.unit() < 0.5f ? p.back : pick({2, 1}, p.back);
    p.wingRear = rng.unit() < 0.5f ? pick({1, 2, 4, 3}, p.wing) : p.wing;
    p.wingTip = pick({1, 2, 4, 3}, p.wing);
    p.wingBand = rng.unit() < 0.45f;
    p.wingBar = pick({4, 1, 2}, p.wing);
    p.barPanel = rng.range(1, 2);
    p.tail = rng.unit() < 0.5f ? p.wing : pick({3, 1, 2}, p.back);
    p.tailBand = rng.unit() < 0.5f;
    p.tailTip = pick({4, 2, 1}, p.tail);
    p.crest = pick({2, 1, 4}, p.crown);
    switch (rng.range(0, 3)) {
        case 0: p.beak = hsv(rng.range(0.04f, 0.15f), 0.9f, 1.0f); break;
        case 1: p.beak = hsv(hue, 0.3f, 0.12f); break;
        case 2: p.beak = hsv(0.12f, 0.18f, 0.95f); break;
        default: p.beak = pick({2, 1}, p.face); break;
    }
    p.beakLow = rng.unit() < 0.6f ? p.beak * 0.78f : pick({3, 2, 4}, p.beak);
    p.legs = rng.unit() < 0.5f ? hsv(0, 0, rng.range(0.30f, 0.55f)) : hsv(rng.range(0.0f, 0.08f), 0.6f, 0.85f);
    p.eyeRingOn = rng.unit() < 0.35f;
    p.eyeRing = pick({4, 2}, p.face);
    return p;
}

Genome makeGenome(uint32_t seed) {
    Rng rng(seed ^ 0xB1D5EEDu);
    Genome g{};
    g.length = rng.range(0.95f, 2.5f);
    g.width = g.length * rng.range(0.15f, 0.26f);
    g.height = g.length * rng.range(0.17f, 0.29f);
    g.head = rng.range(0.75f, 1.35f);
    g.neckReach = rng.range(0.90f, 1.25f);
    g.posture = rng.range(0.15f, 0.55f);
    g.chest = rng.range(0.92f, 1.10f);
    g.rump = rng.range(0.55f, 0.92f);
    const float beak = rng.unit();
    g.beakLength = 0.07f + 0.42f * beak * beak;
    g.beakDepth = rng.range(0.30f, 0.95f);
    g.beakCurve = rng.unit() < 0.35f ? rng.range(0.30f, 0.80f) : rng.range(-0.05f, 0.12f);
    g.crest = static_cast<Crest>(rng.range(0, 3));
    g.crestHeight = rng.range(0.45f, 1.30f);
    g.crestSweep = rng.unit();
    g.crestPlumes = rng.range(1, 3);
    g.tail = static_cast<Tail>(rng.range(0, 5));
    g.tailLength = rng.range(0.25f, 0.60f);
    g.tailSpread = rng.range(0.45f, 1.25f);
    g.tip = static_cast<Tip>(rng.range(0, 2));
    g.span = g.length * rng.range(0.85f, 1.60f);
    g.chord = g.length * rng.range(0.28f, 0.48f);
    g.sweep = rng.range(0.10f, 0.60f);
    g.dihedral = rng.range(-0.06f, 0.14f);
    g.legs = rng.range(0.40f, 0.90f);
    g.eye = rng.range(0.15f, 0.24f);
    g.palette = makePalette(rng);
    return g;
}

void outwardFace(MeshBuilder& mesh, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                 glm::vec3 inside, glm::vec3 color) {
    const glm::vec3 center = (a + b + c) / 3.0f;
    if (glm::dot(glm::cross(b - a, c - a), center - inside) < 0) std::swap(b, c);
    mesh.triangle(a, b, c, color);
}

// Placa fina de 3 ou 4 cantos com face de cima e de baixo, usada em asas, cauda e crista.
void sheet(MeshBuilder& mesh, std::initializer_list<glm::vec3> outline, float thickness,
           glm::vec3 top, glm::vec3 bottom) {
    const glm::vec3* p = outline.begin();
    const std::size_t count = outline.size();
    glm::vec3 center(0);
    for (std::size_t i = 0; i < count; ++i) center += p[i];
    center /= static_cast<float>(count);
    glm::vec3 normal = glm::cross(p[1] - p[0], p[count - 1] - p[0]);
    const float length = glm::length(normal);
    if (length < 1e-7f) return;
    normal /= length;
    if (normal.y < 0 || (std::abs(normal.y) < 1e-4f && normal.x < 0)) normal = -normal;
    for (float side : {1.0f, -1.0f}) {
        const glm::vec3 lift = normal * (side * thickness);
        for (std::size_t i = 1; i + 1 < count; ++i)
            outwardFace(mesh, p[0] + lift, p[i] + lift, p[i + 1] + lift, center, side > 0 ? top : bottom);
    }
}

// Forma fina em duas faces, com espessura central para iluminar as facetas.
void appendPlume(MeshBuilder& mesh, std::initializer_list<glm::vec3> rim,
                 glm::vec3 ridge, glm::vec3 underside, const glm::vec3 color, const glm::vec3 shade) {
    const glm::vec3 inside = (ridge + underside) * 0.5f;
    const glm::vec3* edge = rim.begin();
    for (std::size_t i = 0; i < rim.size(); ++i) {
        const glm::vec3 a = edge[i], b = edge[(i + 1) % rim.size()];
        outwardFace(mesh, a, b, ridge, inside, color);
        outwardFace(mesh, b, a, underside, inside, shade);
    }
}

struct Section {
    glm::vec3 center;
    float halfWidth, halfHeight;
};

// O contorno octogonal fica no plano XY, que e perpendicular ao eixo do voo (-Z).
using Ring = std::array<glm::vec3, 8>;
Ring makeRing(const Section& section) {
    constexpr float d = 0.70710678f;
    const std::array<glm::vec2, 8> octagon = {{{1, 0}, {d, d}, {0, 1}, {-d, d},
                                              {-1, 0}, {-d, -d}, {0, -1}, {d, -d}}};
    Ring points;
    for (std::size_t i = 0; i < points.size(); ++i) {
        points[i] = section.center + glm::vec3(octagon[i].x * section.halfWidth,
                                              octagon[i].y * section.halfHeight, 0);
    }
    return points;
}

// Medidas da cabeca, compartilhadas por corpo, bico, olhos e crista.
struct Head {
    glm::vec3 center;
    float halfWidth, halfHeight, halfLength;
};

Head headOf(const Genome& g) {
    return {{0, g.height * (0.15f + 0.6f * g.posture), -0.40f * g.length * g.neckReach},
            g.head * 0.62f * g.width, g.head * 0.58f * g.height, g.head * 0.12f * g.length};
}

void appendBody(MeshBuilder& mesh, const Genome& g, const Head& head) {
    const float l = g.length, w = g.width, h = g.height;
    const glm::vec3 hc = head.center;
    const float neckW = (head.halfWidth * 0.65f + w * 0.35f) * 0.86f;
    const float neckH = (head.halfHeight * 0.65f + h * 0.35f) * 0.86f;
    const std::array<Section, 9> sections = {{
        {hc + glm::vec3(0, -0.05f * head.halfHeight, -head.halfLength), 0, 0},
        {hc + glm::vec3(0, -0.02f * head.halfHeight, -0.6f * head.halfLength), 0.70f * head.halfWidth, 0.72f * head.halfHeight},
        {hc, head.halfWidth, head.halfHeight},
        {hc + glm::vec3(0, -0.05f * head.halfHeight, 0.7f * head.halfLength), 0.86f * head.halfWidth, 0.86f * head.halfHeight},
        {{0, hc.y * 0.5f, hc.z + head.halfLength + 0.04f * l}, neckW, neckH},
        {{0, 0, -0.10f * l}, g.chest * w, g.chest * h},
        {{0, -0.02f * h, 0.10f * l}, 0.92f * w, 0.95f * h},
        {{0, 0.05f * h, 0.30f * l}, g.rump * w, g.rump * h},
        {{0, 0.08f * h, 0.46f * l}, 0, 0}
    }};
    const Palette& p = g.palette;
    Ring front = makeRing(sections.front());
    for (std::size_t i = 1; i < sections.size(); ++i) {
        const Ring back = makeRing(sections[i]);
        const glm::vec3 inside = (sections[i - 1].center + sections[i].center) * 0.5f;
        for (std::size_t j = 0; j < front.size(); ++j) {
            const auto next = (j + 1) % front.size();
            // Faces 1 e 2 ficam no alto, 0 e 3 nos lados de cima, 4 e 7 nos de baixo, 5 e 6 embaixo.
            const bool top = j == 1 || j == 2, upper = j == 0 || j == 3, bottom = j == 5 || j == 6;
            glm::vec3 color = top || upper ? p.back : p.belly;
            if (i <= 3) color = top ? p.crown : upper ? p.face : p.throat;
            else if (i == 4) color = p.collarBand ? p.collar : (top || upper ? p.back : p.throat);
            else if (i == 5 && !top && !upper && p.breastBand) color = p.breast;
            else if (i == 8 && (top || upper)) color = p.tail;
            if (i >= 6 && bottom) color = p.belly;
            outwardFace(mesh, front[j], back[j], back[next], inside, color);
            outwardFace(mesh, front[j], back[next], front[next], inside, color);
        }
        front = back;
    }
}

// Bico em losango com curva para baixo; a mandibula de baixo pode ter outra cor.
void appendBeak(MeshBuilder& mesh, const Genome& g, const Head& head) {
    const glm::vec3 base = head.center + glm::vec3(0, -0.12f * head.halfHeight, -0.72f * head.halfLength);
    const float halfWidth = 0.45f * head.halfWidth, halfHeight = 0.55f * g.beakDepth * head.halfHeight;
    const float reach = g.beakLength * g.length;
    const float drop = g.beakCurve * reach * 0.7f;
    const glm::vec3 middle = base + glm::vec3(0, -drop * 0.25f, -0.55f * reach);
    const glm::vec3 tip = base + glm::vec3(0, -drop, -reach);
    const auto diamond = [](glm::vec3 c, float x, float y) {
        return std::array<glm::vec3, 4>{c + glm::vec3(0, y, 0), c + glm::vec3(x, 0, 0),
                                        c + glm::vec3(0, -y, 0), c + glm::vec3(-x, 0, 0)};
    };
    const auto a = diamond(base, halfWidth, halfHeight);
    const auto b = diamond(middle, 0.55f * halfWidth, 0.60f * halfHeight);
    const auto& p = g.palette;
    for (int k = 0; k < 4; ++k) {
        const int n = (k + 1) % 4;
        // As faces 0 e 3 encostam no topo: mandibula de cima.
        const glm::vec3 color = (k == 0 || k == 3) ? p.beak : p.beakLow;
        const glm::vec3 inside = (base + middle) * 0.5f;
        outwardFace(mesh, a[k], b[k], b[n], inside, color);
        outwardFace(mesh, a[k], b[n], a[n], inside, color);
        outwardFace(mesh, b[k], tip, b[n], (middle + tip) * 0.5f, color);
    }
}

void appendCrest(MeshBuilder& mesh, const Genome& g, const Head& head) {
    if (g.crest == Crest::None) return;
    const glm::vec3 base = head.center + glm::vec3(0, 0.90f * head.halfHeight, -0.15f * head.halfLength);
    const int plumes = g.crest == Crest::Tuft ? 1 : g.crestPlumes + (g.crest == Crest::Fan ? 1 : 0);
    const float height = (g.crest == Crest::Tuft ? 0.8f : 2.2f) * g.crestHeight * head.halfHeight;
    const float sweep = g.crest == Crest::Swept ? 0.8f + g.crestSweep : 0.15f * g.crestSweep;
    for (int k = 0; k < plumes; ++k) {
        const float offset = plumes == 1 ? 0.0f : (static_cast<float>(k) / (plumes - 1) - 0.5f);
        const float x = offset * 0.7f * head.halfWidth;
        const float lean = g.crest == Crest::Fan ? offset * 0.9f : 0.0f;
        const glm::vec3 front = base + glm::vec3(x, 0, -0.30f * head.halfLength);
        const glm::vec3 back = base + glm::vec3(x, -0.15f * head.halfHeight, 0.70f * head.halfLength);
        const glm::vec3 tip = base + glm::vec3(x + lean * height, height * (1.0f - 0.35f * sweep),
                                               sweep * height + 0.3f * head.halfLength);
        const glm::vec3 side(0.10f * head.halfWidth, 0, 0);
        const glm::vec3 middle = (front + back + tip) / 3.0f;
        const glm::vec3 color = k % 2 == 0 ? g.palette.crest : g.palette.crest * 0.82f + g.palette.wingTip * 0.18f;
        appendPlume(mesh, {front, tip, back}, middle + side, middle - side, color, color * 0.75f);
    }
}

// Penas da cauda em leque, sem sobreposicao, com faixa opcional na ponta.
void appendTail(MeshBuilder& mesh, const Genome& g) {
    const Palette& p = g.palette;
    const glm::vec3 base(0, 0.08f * g.height, 0.40f * g.length);
    float length = g.tailLength * g.length, spread = g.tailSpread;
    switch (g.tail) {
        case Tail::Fan: spread *= 1.4f; break;
        case Tail::Forked: spread *= 0.9f; break;
        case Tail::Wedge: spread *= 0.8f; break;
        case Tail::Square: spread *= 0.7f; break;
        case Tail::Long: length *= 1.9f; spread *= 0.35f; break;
        case Tail::Streamers: length *= 0.75f; spread *= 0.7f; break;
    }
    const auto profile = [&](float u) {
        const float a = std::abs(u);
        switch (g.tail) {
            case Tail::Fan: return 1.0f - 0.12f * u * u;
            case Tail::Forked: return 0.62f + 0.50f * u * u;
            case Tail::Wedge: return 1.15f - 0.55f * a;
            case Tail::Long: return 1.0f - 0.35f * a;
            default: return 0.95f;
        }
    };
    constexpr int kRays = 6;
    const float baseHalf = 0.35f * g.width, thickness = 0.008f * g.length;
    std::array<glm::vec3, kRays> inner{}, band{}, outer{};
    for (int k = 0; k < kRays; ++k) {
        const float u = -1.0f + 2.0f * k / (kRays - 1);
        const float reach = profile(u);
        inner[k] = base + glm::vec3(u * baseHalf, 0, 0);
        outer[k] = base + glm::vec3(u * (baseHalf + spread * length * 0.55f * reach), 0.12f * g.height * reach, length * reach);
        band[k] = inner[k] + (outer[k] - inner[k]) * 0.70f;
    }
    const glm::vec3 shade = p.tail * 0.6f + p.belly * 0.2f;
    for (int k = 0; k + 1 < kRays; ++k) {
        sheet(mesh, {inner[k], inner[k + 1], band[k + 1], band[k]}, thickness, p.tail, shade);
        const glm::vec3 tipColor = p.tailBand ? p.tailTip : p.tail;
        sheet(mesh, {band[k], band[k + 1], outer[k + 1], outer[k]}, thickness, tipColor, tipColor * 0.7f);
    }
    if (g.tail == Tail::Streamers) {
        for (int k : {0, kRays - 1}) {
            const glm::vec3 dir = outer[k] - inner[k];
            const glm::vec3 side(0.06f * g.width, 0, 0);
            const glm::vec3 end = outer[k] + dir * 1.6f;
            sheet(mesh, {outer[k] - side, outer[k] + side, end}, thickness, p.tailTip, p.tailTip * 0.7f);
        }
    }
}

glm::vec3 shoulderOf(const Genome& g) { return {0.62f * g.width, 0.25f * g.height, -0.12f * g.length}; }

// Asa em 4 paineis ao longo da envergadura e 2 fileiras (coberteiras e remiges).
void appendWing(MeshBuilder& mesh, const Genome& g, float side) {
    const Palette& p = g.palette;
    const glm::vec3 root = shoulderOf(g) * glm::vec3(side, 1, 1);
    const float s = g.span, c = g.chord, sweep = g.sweep * c * 1.4f;
    constexpr std::array<float, 5> station = {0.0f, 0.28f, 0.52f, 0.76f, 1.0f};
    constexpr std::array<float, 5> leading = {-0.45f, -0.58f, -0.55f, -0.42f, -0.25f};
    constexpr std::array<float, 5> backward = {0.0f, 0.05f, 0.25f, 0.60f, 1.0f};
    std::array<float, 5> chord{};
    switch (g.tip) {
        case Tip::Pointed: chord = {1.0f, 0.92f, 0.78f, 0.52f, 0.12f}; break;
        case Tip::Rounded: chord = {1.0f, 0.96f, 0.90f, 0.80f, 0.55f}; break;
        case Tip::Slotted: chord = {1.0f, 0.97f, 0.92f, 0.85f, 0.70f}; break;
    }
    std::array<glm::vec3, 5> lead{}, middle{}, trail{};
    for (int k = 0; k < 5; ++k) {
        const float t = station[k];
        lead[k] = root + glm::vec3(side * t * s, g.dihedral * t * s, c * leading[k] + sweep * backward[k]);
        trail[k] = lead[k] + glm::vec3(0, 0, c * chord[k]);
        middle[k] = lead[k] + (trail[k] - lead[k]) * 0.55f;
    }
    const auto thick = [&](int k) { return 0.03f * g.height * (1.0f - station[k]) + 0.006f * g.length; };
    for (int k = 0; k < 4; ++k) {
        const bool tipPanel = k == 3;
        glm::vec3 front = tipPanel ? p.wingTip : p.wing, rear = tipPanel ? p.wingTip : p.wingRear;
        if (p.wingBand && k == p.barPanel) front = p.wingBar;
        const float t = thick(k);
        sheet(mesh, {lead[k], lead[k + 1], middle[k + 1], middle[k]}, t, front, front * 0.6f + p.belly * 0.4f);
        sheet(mesh, {middle[k], middle[k + 1], trail[k + 1], trail[k]}, t, rear, rear * 0.7f + p.belly * 0.3f);
    }
    const float t = thick(4);
    const glm::vec3 edge = trail[4] - lead[4];
    switch (g.tip) {
        case Tip::Pointed:
            sheet(mesh, {lead[4], lead[4] + glm::vec3(side * 0.07f * s, 0, 0.05f * c), trail[4]}, t, p.wingTip, p.wingTip * 0.6f);
            break;
        case Tip::Rounded:
            sheet(mesh, {lead[4], lead[4] + edge * 0.5f + glm::vec3(side * 0.09f * s, 0, 0), trail[4]}, t, p.wingTip, p.wingTip * 0.6f);
            break;
        case Tip::Slotted:
            // Dedos abertos na ponta, como nas aves que planam em termicas.
            for (int f = 0; f < 4; ++f) {
                const float u = 0.12f + 0.25f * f;
                const glm::vec3 a = lead[4] + edge * (u - 0.10f), b = lead[4] + edge * (u + 0.10f);
                const glm::vec3 tip = lead[4] + edge * u + glm::vec3(side * 0.17f * s, 0.01f * s * f, (u - 0.3f) * 0.18f * s);
                sheet(mesh, {a, tip, b}, t, p.wingTip, p.wingTip * 0.6f);
            }
            break;
    }
}

void appendLeg(MeshBuilder& mesh, const Genome& g, float side) {
    const glm::vec3 hip(side * 0.40f * g.width, -0.60f * g.height, 0.06f * g.length);
    const glm::vec3 ankle = hip + glm::vec3(0, -g.legs * g.height, 0.10f * g.length);
    const float radius = 0.09f * g.width;
    mesh.tube({hip, ankle}, {radius * 1.4f, radius}, 5, g.palette.legs);
    for (float toe : {-1.0f, 0.0f, 1.0f}) {
        const glm::vec3 tip = ankle + glm::vec3(toe * 0.20f * g.width, 0, -0.12f * g.length);
        mesh.tube({ankle, tip}, {radius * 0.82f, radius * 0.12f}, 5, g.palette.legs);
    }
}

void appendEye(MeshBuilder& mesh, const Genome& g, const Head& head, Rng& rng, float side) {
    const glm::vec3 center = head.center + glm::vec3(side * 0.86f * head.halfWidth, 0.22f * head.halfHeight,
                                                     -0.30f * head.halfLength);
    const float radius = g.eye * head.halfHeight;
    // A malha de bolha varia o tom em ate 8%: as cores entram um pouco abaixo de 1 para nao estourar.
    if (g.palette.eyeRingOn)
        mesh.blob(center - glm::vec3(side * 0.25f * radius, 0, 0), radius * 1.5f, g.palette.eyeRing * 0.92f, rng, 0, 0);
    mesh.blob(center, radius, {0.04f, 0.04f, 0.05f}, rng, 0, 0);
    const glm::vec3 glint = center + glm::vec3(side * 0.6f, 0.4f, -0.3f) * radius;
    mesh.blob(glint, 0.30f * radius, {0.90f, 0.90f, 0.90f}, rng, 0, 0);
}

float log2Ratio(float a, float b) { return std::abs(std::log2(std::max(a, 1e-3f) / std::max(b, 1e-3f))); }
}

static MeshBuilder assembleBird(uint32_t seed, BirdFlightParts* parts) {
    const Genome bird = makeGenome(seed);
    const Head head = headOf(bird);
    MeshBuilder mesh;
    appendBody(mesh, bird, head);
    appendBeak(mesh, bird, head);
    appendCrest(mesh, bird, head);
    appendTail(mesh, bird);
    Rng eyeRng(seed);
    for (float side : {-1.0f, 1.0f}) {
        auto& wing = parts ? (side < 0 ? parts->leftWing : parts->rightWing) : mesh;
        appendWing(wing, bird, side);
        appendLeg(mesh, bird, side);
        appendEye(mesh, bird, head, eyeRng, side);
    }
    if (parts) {
        parts->rightShoulder = shoulderOf(bird);
        parts->wingspan = 2 * (bird.span + parts->rightShoulder.x);
    }
    return mesh;
}

MeshBuilder buildBirdMesh(uint32_t seed) {
    return assembleBird(seed, nullptr);
}

BirdFlightParts buildBirdFlightParts(uint32_t seed) {
    BirdFlightParts parts;
    parts.body = assembleBird(seed, &parts);
    return parts;
}

BirdTraits describeBird(uint32_t seed) {
    const Genome g = makeGenome(seed);
    const Palette& p = g.palette;
    float tail = g.tailLength * (g.tail == Tail::Long ? 1.9f : g.tail == Tail::Streamers ? 2.0f : 1.0f);
    const float crest = g.crest == Crest::None ? 0.0f : g.crestHeight * (g.crest == Crest::Tuft ? 0.35f : 1.0f);
    return {g.length, g.width, g.height, g.head, g.beakLength, g.beakDepth, crest, tail, g.tailSpread,
            g.span, g.chord,
            {p.back, p.belly, p.crown * 0.5f + p.face * 0.5f, p.wing * 0.5f + p.wingRear * 0.5f, p.wingTip,
             g.crest == Crest::None ? p.tailTip : p.crest}};
}

float birdLookDistance(const BirdTraits& a, const BirdTraits& b) {
    float color = 0;
    for (std::size_t i = 0; i < a.colors.size(); ++i) color += glm::length(a.colors[i] - b.colors[i]);
    color /= static_cast<float>(a.colors.size());
    const float shape = (log2Ratio(a.length, b.length) + log2Ratio(a.width / a.length, b.width / b.length)
                         + log2Ratio(a.height / a.length, b.height / b.length) + log2Ratio(a.head, b.head)
                         + log2Ratio(a.beak, b.beak) + log2Ratio(a.crest + 0.2f, b.crest + 0.2f)
                         + log2Ratio(a.tail, b.tail) + log2Ratio(a.span / a.length, b.span / b.length)
                         + log2Ratio(a.chord / a.length, b.chord / b.length)) / 9.0f;
    return color + shape;
}

float birdCollisionRadius(const BirdTraits& bird) {
    return std::max(1.5f, 0.30f * (bird.length + bird.span) * kBirdScale);
}

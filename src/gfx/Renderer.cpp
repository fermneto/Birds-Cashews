#include "gfx/Renderer.h"
#include "shared/BirdFactory.h"
#include <array>
#include <cmath>
#include <unordered_set>
#include <opencv2/imgproc.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace {
const glm::vec3 kSunDirection = glm::normalize(glm::vec3(0.45f, 0.75f, 0.35f));
const glm::vec3 kHorizon(0.76f, 0.87f, 0.93f);

// A interface e desenhada em 1280x720 e ajustada a janela sem deformar.
constexpr float kLogicalWidth = 1280.0f, kLogicalHeight = 720.0f;
constexpr float kPreviewX = 1036.0f, kPreviewY = 28.0f, kPreviewWidth = 204.0f, kPreviewHeight = 153.0f;

struct IslandBird {
    uint32_t seed;
    glm::vec3 offset;
    float scale;
};

constexpr std::array<IslandBird, 4> kIslandBirds = {{
    {0, {-48, 10, -112}, 2.1f},
    {1, {-15, -8, -93}, 2.5f},
    {2, {21, 6, -103}, 2.2f},
    {264, {53, -5, -128}, 2.9f},
}};

// Triangulos afilados atravessam a periferia da imagem como rajadas de ar.
const char* const kWindVertex = R"(#version 330 core
uniform float uPhase, uWind, uAspect;
out float opacity;
float random01(uint seed) {
    seed ^= seed >> 16u;
    seed *= 0x7feb352du;
    seed ^= seed >> 15u;
    seed *= 0x846ca68bu;
    return float(seed ^ (seed >> 16u)) / 4294967296.0;
}

void main() {
    uint lane = uint(gl_InstanceID) + 1u;
    float clock = uPhase * mix(0.72, 1.27, random01(lane)) + random01(lane + 47u);
    uint pass = lane * 65537u + uint(floor(clock)) * 17u;
    float delay = 0.28 * random01(pass + 1u);
    float duration = mix(0.42, 0.70, random01(pass + 2u));
    float age = clamp((fract(clock) - delay) / duration, 0.0, 1.0);
    float side = random01(pass + 3u) < 0.5 ? -1.0 : 1.0;
    float offset = mix(-0.94, 0.94, random01(pass + 4u));
    vec2 edge = random01(pass + 5u) < 0.68 ? vec2(side, offset) : vec2(offset, side);
    vec2 start = edge * mix(0.48, 0.78, random01(pass + 6u));
    vec2 end = edge * 1.35;
    vec2 tangent = vec2(-edge.y, edge.x);
    end += tangent * mix(-0.25, 0.25, random01(pass + 7u));
    vec2 aspect = vec2(uAspect, 1.0);
    vec2 dir = normalize((end - start) * aspect);
    vec2 pos = mix(start, end, age * (0.55 + 0.45 * age)) * aspect;
    float width = mix(0.004, 0.009, age) * uWind;
    float trail = mix(0.045, 0.22, age) * uWind;
    if (gl_VertexID == 0) pos -= trail * dir;
    else pos += vec2(-dir.y, dir.x) * (gl_VertexID == 1 ? width : -width);
    gl_Position = vec4(pos / aspect, 0, 1);
    opacity = 0.85 * uWind * smoothstep(0.0, 0.22, age) * (1.0 - smoothstep(0.82, 1.0, age));
})";
const char* const kWindFragment = R"(#version 330 core
in float opacity;
out vec4 color;
void main() { color = vec4(0.83, 0.93, 0.99, opacity); })";

const char* const kWorldVertex = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform mat4 uViewProjection;
uniform mat4 uModel;
out vec3 vWorld;
out vec3 vNormal;
out vec3 vColor;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    gl_Position = uViewProjection * world;
})";

const char* const kWorldFragment = R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec3 vColor;
uniform vec3 uCamera;
uniform vec3 uSun;
uniform vec3 uFog;
uniform float uGlow;
out vec4 color;
void main() {
    vec3 normal = normalize(vNormal);
    if (dot(normal, uCamera - vWorld) < 0.0) normal = -normal;
    float sun = max(dot(normal, uSun), 0.0);
    float sky = 0.5 + 0.5 * normal.y;
    vec3 light = vec3(1.0, 0.96, 0.86) * sun * 0.72 + mix(vec3(0.36, 0.33, 0.30), vec3(0.50, 0.57, 0.65), sky);
    light = mix(light, vec3(1.0), uGlow);
    float distance = length(vWorld - uCamera) * 0.00055;
    color = vec4(mix(vColor * light, uFog, 1.0 - exp(-distance * distance)), 1.0);
})";

const char* const kSkyVertex = R"(#version 330 core
out vec2 vNdc;
void main() {
    vNdc = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
})";

const char* const kSkyFragment = R"(#version 330 core
in vec2 vNdc;
uniform mat4 uInverse;
uniform vec3 uCamera;
uniform vec3 uSun;
uniform vec3 uFog;
out vec4 color;
void main() {
    vec4 far = uInverse * vec4(vNdc, 1.0, 1.0);
    vec3 ray = normalize(far.xyz / far.w - uCamera);
    vec3 sky = mix(uFog, vec3(0.27, 0.53, 0.84), pow(clamp(ray.y, 0.0, 1.0), 0.55));
    float sun = max(dot(ray, uSun), 0.0);
    sky += vec3(1.0, 0.9, 0.7) * (pow(sun, 900.0) + 0.22 * pow(sun, 24.0));
    color = vec4(sky, 1.0);
})";

const char* const kWaterFragment = R"(#version 330 core
in vec3 vWorld;
uniform vec3 uCamera;
uniform vec3 uSun;
uniform vec3 uFog;
uniform float uTime;
out vec4 color;
void main() {
    vec2 p = vWorld.xz;
    float ripple = exp(-length(vWorld - uCamera) * 0.0035);
    float dx = 0.07 * cos(p.x * 0.11 + uTime * 1.3) + 0.05 * cos((p.x + p.y) * 0.23 + uTime * 1.9);
    float dz = 0.07 * cos(p.y * 0.13 + uTime * 1.1) + 0.05 * cos((p.x - p.y) * 0.19 + uTime * 1.6);
    vec3 normal = normalize(vec3(dx * ripple, 1.0, dz * ripple));
    vec3 view = normalize(uCamera - vWorld);
    float fresnel = pow(1.0 - max(dot(normal, view), 0.0), 3.0);
    vec3 water = mix(vec3(0.05, 0.42, 0.52), vec3(0.55, 0.78, 0.88), fresnel);
    water += vec3(1.0, 0.95, 0.8) * pow(max(dot(reflect(-uSun, normal), view), 0.0), 120.0);
    float distance = length(vWorld - uCamera) * 0.00055;
    float opaque = smoothstep(760.0, 1040.0, length(vWorld.xz));
    color = vec4(mix(water, uFog, 1.0 - exp(-distance * distance)), mix(0.8, 1.0, opaque));
})";

const char* const kPreviewVertex = R"(#version 330 core
uniform vec4 uRect;
out vec2 vUv;
void main() {
    vec2 corner = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    vUv = vec2(corner.x, 1.0 - corner.y);
    gl_Position = vec4(uRect.xy + corner * uRect.zw, 0.0, 1.0);
})";

const char* const kPreviewFragment = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uImage;
out vec4 color;
void main() { color = vec4(texture(uImage, vUv).bgr, 1.0); })";

// A camada da interface ja chega com a cor multiplicada pelo alfa.
const char* const kOverlayFragment = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uImage;
out vec4 color;
void main() { color = texture(uImage, vUv); })";

// Etiqueta presa ao mundo: o retangulo ja vem em coordenadas da tela, com a profundidade do ponto ancorado,
// para as copas e o relevo esconderem o nome de quem esta atras deles.
const char* const kLabelVertex = R"(#version 330 core
uniform vec4 uRect;
uniform float uDepth;
out vec2 vUv;
void main() {
    vec2 corner = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    vUv = vec2(corner.x, 1.0 - corner.y);
    gl_Position = vec4(uRect.xy + corner * uRect.zw, uDepth, 1.0);
})";

const char* const kLabelFragment = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uImage;
uniform float uOpacity;
out vec4 color;
void main() { color = texture(uImage, vUv) * uOpacity; })";

void configureTexture(GLuint texture) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

std::vector<Vertex> waterQuad() {
    constexpr float s = 7000.0f;
    MeshBuilder mesh;
    mesh.quad({-s, 0, -s}, {-s, 0, s}, {s, 0, s}, {s, 0, -s}, glm::vec3(0));
    return mesh.vertices();
}
}

Renderer::Renderer(const MeshBuilder& worldMesh, const MeshBuilder& cloudMesh, glm::vec3 spawnPoint)
    : worldShader_(kWorldVertex, kWorldFragment), skyShader_(kSkyVertex, kSkyFragment),
      waterShader_(kWorldVertex, kWaterFragment), previewShader_(kPreviewVertex, kPreviewFragment),
      overlayShader_(kPreviewVertex, kOverlayFragment), windShader_(kWindVertex, kWindFragment),
      labelShader_(kLabelVertex, kLabelFragment),
      worldMesh_(worldMesh.vertices()), cloudMesh_(cloudMesh.vertices()), waterMesh_(waterQuad()),
      spawnPoint_(spawnPoint) {
    for (const auto& bird : kIslandBirds) cacheBird(bird.seed);
    glGenVertexArrays(1, &emptyVao_);
    glGenTextures(1, &previewTexture_);
    glGenTextures(1, &overlayTexture_);
    configureTexture(previewTexture_);
    configureTexture(overlayTexture_);
    glEnable(GL_CULL_FACE);
}

Renderer::~Renderer() {
    glDeleteVertexArrays(1, &emptyVao_);
    glDeleteTextures(1, &previewTexture_);
    glDeleteTextures(1, &overlayTexture_);
    for (auto& [key, label] : labelTextures_) glDeleteTextures(1, &label.texture);
    if (targetFbo_) {
        glDeleteFramebuffers(1, &targetFbo_);
        glDeleteTextures(1, &targetColor_);
        glDeleteRenderbuffers(1, &targetDepth_);
    }
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void Renderer::drawSky(const Camera3D& camera, float) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    skyShader_.use();
    skyShader_.set("uInverse", glm::inverse(camera.viewProjection()));
    skyShader_.set("uCamera", camera.position);
    skyShader_.set("uSun", kSunDirection);
    skyShader_.set("uFog", kHorizon);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::drawWorld(const Camera3D& camera) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    worldShader_.use();
    worldShader_.set("uViewProjection", camera.viewProjection());
    worldShader_.set("uModel", glm::mat4(1.0f));
    worldShader_.set("uCamera", camera.position);
    worldShader_.set("uSun", kSunDirection);
    worldShader_.set("uFog", kHorizon);
    worldShader_.set("uGlow", 0.0f);
    worldMesh_.draw();
    worldShader_.set("uGlow", 0.72f);
    cloudMesh_.draw();
    worldShader_.set("uGlow", 0.0f);
}

void Renderer::cacheBird(uint32_t seed) {
    if (birdMeshes_.count(seed)) return;
    const auto parts = buildBirdFlightParts(seed);
    birdMeshes_.emplace(seed, FlyingBird{std::make_unique<Mesh>(parts.body.vertices()),
                                        std::make_unique<Mesh>(parts.leftWing.vertices()),
                                        std::make_unique<Mesh>(parts.rightWing.vertices()),
                                        parts.rightShoulder, 24.0f / std::sqrt(parts.wingspan)});
}

void Renderer::setBirdInstances(std::vector<BirdInstance> birds) {
    demoBirds_ = false;
    if (birds.empty() && birds_.empty() && birdMeshes_.empty()) return;
    std::unordered_set<uint32_t> active;
    for (const auto& bird : birds) {
        if (active.insert(bird.seed).second) cacheBird(bird.seed);
    }
    for (auto it = birdMeshes_.begin(); it != birdMeshes_.end();) {
        if (active.count(it->first)) ++it;
        else it = birdMeshes_.erase(it);
    }
    birds_ = std::move(birds);
}

// Cada asa gira no ombro: sobe ou desce, recolhe no comprimento e recua a ponta.
void Renderer::drawBird(const BirdInstance& bird, const WingPose& wings) {
    auto model = glm::translate(glm::mat4(1.0f), bird.position);
    model = glm::rotate(model, -bird.yaw, glm::vec3(0, 1, 0));
    model = glm::rotate(model, bird.pitch, glm::vec3(1, 0, 0));
    model = glm::rotate(model, -bird.roll, glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(bird.scale));
    worldShader_.set("uModel", model);
    const auto& flyer = birdMeshes_.at(bird.seed);
    flyer.body->draw();
    for (float side : {-1.0f, 1.0f}) {
        auto shoulder = flyer.rightShoulder;
        shoulder.x *= side;
        auto wing = glm::translate(model, shoulder);
        wing = glm::rotate(wing, side * wings.lift, glm::vec3(0, 0, 1));
        wing = glm::rotate(wing, -side * wings.sweep, glm::vec3(0, 1, 0));
        wing = glm::scale(wing, glm::vec3(wings.spread, 1.0f, 1.0f));
        worldShader_.set("uModel", glm::translate(wing, -shoulder));
        (side < 0 ? flyer.leftWing : flyer.rightWing)->draw();
    }
}

void Renderer::drawBirds(float time) {
    // Um leve brilho deixa as cores dos passaros vivas mesmo na sombra e contra as copas.
    worldShader_.set("uGlow", 0.15f);
    if (!demoBirds_) {
        for (const auto& bird : birds_)
            drawBird(bird, birdMotion_.advance(bird, birdMeshes_.at(bird.seed).beatRate, time));
        birdMotion_.forgetUnseen(time);
        worldShader_.set("uGlow", 0.0f);
        return;
    }
    for (size_t i = 0; i < kIslandBirds.size(); ++i) {
        const auto& bird = kIslandBirds[i];
        const float phase = time * (0.62f + 0.04f * i) + 0.7f * i;
        const glm::vec3 drift(12.0f * std::sin(phase), 2.5f * std::sin(2.0f * phase),
                              12.0f * std::cos(phase));
        BirdInstance demo{bird.seed, spawnPoint_ + bird.offset + drift, glm::radians(90.0f) - phase,
                          0.0f, 0.20f, bird.scale};
        demo.key = 0x80000000u + static_cast<uint32_t>(i);
        demo.glideFlaps = 0.5f;
        drawBird(demo, birdMotion_.advance(demo, birdMeshes_.at(bird.seed).beatRate, time));
    }
    worldShader_.set("uGlow", 0.0f);
}

void Renderer::setCashewMeshes(const MeshBuilder& fruit, const MeshBuilder& farFruit, const MeshBuilder& stems) {
    cashewFruit_ = std::make_unique<Mesh>(fruit.vertices());
    cashewFarFruit_ = std::make_unique<Mesh>(farFruit.vertices());
    cashewStems_ = std::make_unique<Mesh>(stems.vertices());
}

void Renderer::setCashewInstances(std::vector<glm::mat4> nearby, std::vector<glm::mat4> distant) {
    nearbyCashews_ = std::move(nearby);
    distantCashews_ = std::move(distant);
}

// Um leve brilho destaca os cajus contra a sombra das copas.
void Renderer::drawCashews() {
    if (!cashewFruit_) return;
    worldShader_.set("uModel", glm::mat4(1.0f));
    cashewStems_->draw();
    worldShader_.set("uGlow", 0.2f);
    for (const glm::mat4& transform : nearbyCashews_) {
        worldShader_.set("uModel", transform);
        cashewFruit_->draw();
    }
    for (const glm::mat4& transform : distantCashews_) {
        worldShader_.set("uModel", transform);
        cashewFarFruit_->draw();
    }
    worldShader_.set("uGlow", 0.0f);
}

void Renderer::setNestMesh(const MeshBuilder& nest) { nestMesh_ = std::make_unique<Mesh>(nest.vertices()); }

void Renderer::setNestBeaconMesh(const MeshBuilder& beacon) { nestBeaconMesh_ = std::make_unique<Mesh>(beacon.vertices()); }

// Sem rivais na sala a malha fica vazia e nada e desenhado.
void Renderer::setRivalNestMesh(const MeshBuilder& nests) {
    rivalNestsMesh_ = nests.vertices().empty() ? nullptr : std::make_unique<Mesh>(nests.vertices());
}

void Renderer::setRivalBeaconMesh(const MeshBuilder& beacon) { rivalBeaconMesh_ = std::make_unique<Mesh>(beacon.vertices()); }

void Renderer::drawRivalNests() {
    if (!rivalNestsMesh_ || !rivalBeaconMesh_) return;
    glDisable(GL_CULL_FACE);
    worldShader_.set("uModel", glm::mat4(1.0f));
    rivalNestsMesh_->draw();
    worldShader_.set("uGlow", 0.6f);
    for (const glm::mat4& transform : rivalBeacons_) {
        worldShader_.set("uModel", transform);
        rivalBeaconMesh_->draw();
    }
    worldShader_.set("uGlow", 0.0f);
    worldShader_.set("uModel", glm::mat4(1.0f));
    glEnable(GL_CULL_FACE);
}

// O ninho e visto por dentro e por fora, entao as duas faces sao desenhadas.
void Renderer::drawNest() {
    if (!nestMesh_ || !nestBeaconMesh_) return;
    glDisable(GL_CULL_FACE);
    worldShader_.set("uModel", glm::mat4(1.0f));
    nestMesh_->draw();
    worldShader_.set("uGlow", 0.6f);
    worldShader_.set("uModel", nestBeaconTransform_);
    nestBeaconMesh_->draw();
    worldShader_.set("uGlow", 0.0f);
    worldShader_.set("uModel", glm::mat4(1.0f));
    glEnable(GL_CULL_FACE);
}

// O mar acompanha a camera para nunca mostrar a borda.
void Renderer::drawWater(const Camera3D& camera, float time) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    waterShader_.use();
    waterShader_.set("uViewProjection", camera.viewProjection());
    waterShader_.set("uModel", glm::translate(glm::mat4(1.0f), glm::vec3(camera.position.x, 0, camera.position.z)));
    waterShader_.set("uCamera", camera.position);
    waterShader_.set("uSun", kSunDirection);
    waterShader_.set("uFog", kHorizon);
    waterShader_.set("uTime", 0.0f + time);
    waterMesh_.draw();
    glDisable(GL_BLEND);
}

void Renderer::drawViewModel(const Camera3D& camera, const BirdAnimation& animation) {
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    Camera3D near = camera;
    near.fovDegrees = 62.0f;
    worldShader_.use();
    worldShader_.set("uViewProjection", near.viewProjection());
    worldShader_.set("uCamera", camera.position);
    viewModel_.draw(worldShader_, glm::inverse(camera.view()), animation);
}

void Renderer::setPreview(const cv::Mat& image) {
    glBindTexture(GL_TEXTURE_2D, previewTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, image.cols, image.rows, 0, GL_RGB, GL_UNSIGNED_BYTE, image.data);
    previewWidth_ = image.cols;
    previewHeight_ = image.rows;
}

// Converte um retangulo da resolucao logica para coordenadas da tela.
glm::vec4 Renderer::logicalRect(float x, float y, float width, float height) const {
    const float windowWidth = static_cast<float>(width_), windowHeight = static_cast<float>(height_);
    const float scale = std::min(windowWidth / kLogicalWidth, windowHeight / kLogicalHeight);
    const float left = (windowWidth - kLogicalWidth * scale) * 0.5f + x * scale;
    const float top = (windowHeight - kLogicalHeight * scale) * 0.5f + y * scale;
    return {2.0f * left / windowWidth - 1.0f, 1.0f - 2.0f * (top + height * scale) / windowHeight,
            2.0f * width * scale / windowWidth, 2.0f * height * scale / windowHeight};
}

void Renderer::drawWind(const BirdAnimation& animation) {
    if (animation.wind < 0.01f) return;
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    windShader_.use();
    windShader_.set("uPhase", animation.windPhase);
    windShader_.set("uWind", animation.wind);
    windShader_.set("uAspect", float(width_) / std::max(1, height_));
    glBindVertexArray(emptyVao_);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 14);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawPreview() {
    if (previewWidth_ == 0 || !previewVisible_) return;
    glDisable(GL_DEPTH_TEST);
    previewShader_.use();
    previewShader_.set("uRect", logicalRect(kPreviewX, kPreviewY, kPreviewWidth, kPreviewHeight));
    previewShader_.set("uImage", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, previewTexture_);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::setOverlay(const cv::Mat& image) {
    CV_Assert(image.type() == CV_8UC4 && image.isContinuous());
    glBindTexture(GL_TEXTURE_2D, overlayTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (image.cols != overlayWidth_ || image.rows != overlayHeight_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.cols, image.rows, 0, GL_BGRA, GL_UNSIGNED_BYTE, image.data);
        overlayWidth_ = image.cols;
        overlayHeight_ = image.rows;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.cols, image.rows, GL_BGRA, GL_UNSIGNED_BYTE, image.data);
    }
}

void Renderer::drawOverlay() {
    if (overlayWidth_ == 0) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    overlayShader_.use();
    overlayShader_.set("uRect", logicalRect(0.0f, 0.0f, kLogicalWidth, kLogicalHeight));
    overlayShader_.set("uImage", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, overlayTexture_);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
}

// Texturas de nomes que sairam da tela sao apagadas, para a memoria nao crescer com quem entra e sai.
void Renderer::setLabels(std::vector<WorldLabel> labels) {
    for (auto it = labelTextures_.begin(); it != labelTextures_.end();) {
        const bool used = std::any_of(labels.begin(), labels.end(), [&](const WorldLabel& l) { return l.key == it->first; });
        if (used) {
            ++it;
            continue;
        }
        glDeleteTextures(1, &it->second.texture);
        it = labelTextures_.erase(it);
    }
    for (const WorldLabel& label : labels) {
        if (labelTextures_.count(label.key) || label.image.empty()) continue;
        CV_Assert(label.image.type() == CV_8UC4 && label.image.isContinuous());
        LabelTexture texture{0, label.image.cols, label.image.rows};
        glGenTextures(1, &texture.texture);
        configureTexture(texture.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, label.image.data);
        labelTextures_[label.key] = texture;
    }
    labels_ = std::move(labels);
}

void Renderer::drawLabels(const Camera3D& camera) {
    if (labels_.empty()) return;
    const glm::mat4 viewProjection = camera.viewProjection();
    const float scale = std::min(width_ / kLogicalWidth, height_ / kLogicalHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    labelShader_.use();
    labelShader_.set("uImage", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(emptyVao_);
    for (const WorldLabel& label : labels_) {
        const auto found = labelTextures_.find(label.key);
        const glm::vec4 clip = viewProjection * glm::vec4(label.anchor, 1.0f);
        if (found == labelTextures_.end() || clip.w <= 0.01f || label.opacity <= 0.0f) continue;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z > 1.0f) continue;
        const float w = 2.0f * found->second.width * scale / width_, h = 2.0f * found->second.height * scale / height_;
        labelShader_.set("uRect", glm::vec4(ndc.x - w * 0.5f, ndc.y, w, h));
        labelShader_.set("uDepth", ndc.z);
        labelShader_.set("uOpacity", label.opacity);
        glBindTexture(GL_TEXTURE_2D, found->second.texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void Renderer::render(const Camera3D& camera, const BirdAnimation& animation, float time) {
    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSky(camera, time);
    drawWorld(camera);
    drawBirds(time);
    drawCashews();
    drawNest();
    drawRivalNests();
    drawSkyPlanes();
    drawWater(camera, time);
    drawWind(animation);
    drawLabels(camera);
    if (viewModelVisible_) drawViewModel(camera, animation);
    drawPreview();
    drawOverlay();
}

void Renderer::createTarget() {
    if (targetFbo_) return;
    glGenFramebuffers(1, &targetFbo_);
    glGenTextures(1, &targetColor_);
    glGenRenderbuffers(1, &targetDepth_);
    glBindTexture(GL_TEXTURE_2D, targetColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width_, height_, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, targetDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetColor_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, targetDepth_);
}

// Renderiza fora da tela, usado pelos testes automaticos.
cv::Mat Renderer::capture(const Camera3D& camera, const BirdAnimation& animation, float time) {
    createTarget();
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
    render(camera, animation, time);
    glFinish();
    cv::Mat image(height_, width_, CV_8UC3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_BGR, GL_UNSIGNED_BYTE, image.data);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    cv::flip(image, image, 0);
    return image;
}

void Renderer::setSkyPlaneMeshes(const MeshBuilder& body, const MeshBuilder& propeller) {
    skyPlaneBody_ = std::make_unique<Mesh>(body.vertices());
    skyPlanePropeller_ = std::make_unique<Mesh>(propeller.vertices());
}

void Renderer::setSkyPlanes(std::vector<glm::mat4> bodies, std::vector<glm::mat4> propellers) {
    skyPlaneBodies_ = std::move(bodies);
    skyPlanePropellers_ = std::move(propellers);
}

// As pecas finas (asas, leme, pas) sao vistas dos dois lados.
void Renderer::drawSkyPlanes() {
    if (!skyPlaneBody_ || skyPlaneBodies_.empty()) return;
    glDisable(GL_CULL_FACE);
    worldShader_.set("uGlow", 0.12f);
    for (const glm::mat4& transform : skyPlaneBodies_) {
        worldShader_.set("uModel", transform);
        skyPlaneBody_->draw();
    }
    for (const glm::mat4& transform : skyPlanePropellers_) {
        worldShader_.set("uModel", transform);
        skyPlanePropeller_->draw();
    }
    worldShader_.set("uGlow", 0.0f);
    worldShader_.set("uModel", glm::mat4(1.0f));
    glEnable(GL_CULL_FACE);
}

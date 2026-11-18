#include "gfx/Renderer.h"
#include "shared/BirdFactory.h"
#include <array>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
const glm::vec3 kSunDirection = glm::normalize(glm::vec3(0.45f, 0.75f, 0.35f));
const glm::vec3 kHorizon(0.76f, 0.87f, 0.93f);

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
      worldMesh_(worldMesh.vertices()), cloudMesh_(cloudMesh.vertices()), waterMesh_(waterQuad()),
      spawnPoint_(spawnPoint) {
    for (const auto& bird : kIslandBirds) {
        const auto parts = buildBirdFlightParts(bird.seed);
        islandBirds_.push_back({std::make_unique<Mesh>(parts.body.vertices()),
                                std::make_unique<Mesh>(parts.leftWing.vertices()),
                                std::make_unique<Mesh>(parts.rightWing.vertices()),
                                parts.rightShoulder, 24.0f / std::sqrt(parts.wingspan)});
    }
    glGenVertexArrays(1, &emptyVao_);
    glGenTextures(1, &previewTexture_);
    glBindTexture(GL_TEXTURE_2D, previewTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEnable(GL_CULL_FACE);
}

Renderer::~Renderer() {
    glDeleteVertexArrays(1, &emptyVao_);
    glDeleteTextures(1, &previewTexture_);
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

void Renderer::drawBirds(float time) {
    for (size_t i = 0; i < kIslandBirds.size(); ++i) {
        const auto& bird = kIslandBirds[i];
        const float phase = time * (0.62f + 0.04f * i) + 0.7f * i;
        const glm::vec3 drift(12.0f * std::sin(phase), 2.5f * std::sin(2.0f * phase),
                              12.0f * std::cos(phase));
        const glm::vec3 position = spawnPoint_ + bird.offset + drift;
        auto model = glm::translate(glm::mat4(1.0f), position);
        model = glm::rotate(model, phase - glm::radians(90.0f), glm::vec3(0, 1, 0));
        model = glm::rotate(model, -0.20f, glm::vec3(0, 0, 1));
        model = glm::scale(model, glm::vec3(bird.scale));
        worldShader_.set("uModel", model);
        const auto& flyer = islandBirds_[i];
        flyer.body->draw();
        const float activity = glm::smoothstep(-0.4f, 0.4f, std::sin(time * 1.3f + i * 2.0f));
        const float beat = std::sin(time * flyer.beatRate + i * 1.7f);
        const float angle = 0.08f + activity * (0.17f + 0.78f * beat);
        for (float side : {-1.0f, 1.0f}) {
            auto shoulder = flyer.rightShoulder;
            shoulder.x *= side;
            auto wing = glm::translate(model, shoulder);
            wing = glm::rotate(wing, side * angle, glm::vec3(0, 0, 1));
            worldShader_.set("uModel", glm::translate(wing, -shoulder));
            (side < 0 ? flyer.leftWing : flyer.rightWing)->draw();
        }
    }
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

void Renderer::drawPreview() {
    if (previewWidth_ == 0) return;
    glDisable(GL_DEPTH_TEST);
    const float w = 2.0f * previewWidth_ / width_, h = 2.0f * previewHeight_ / height_;
    const float margin = 24.0f / width_;
    previewShader_.use();
    previewShader_.set("uRect", glm::vec4(1.0f - w - margin, 1.0f - h - margin * width_ / height_, w, h));
    previewShader_.set("uImage", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, previewTexture_);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::render(const Camera3D& camera, const BirdAnimation& animation, float time) {
    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSky(camera, time);
    drawWorld(camera);
    drawBirds(time);
    drawCashews();
    drawWater(camera, time);
    drawViewModel(camera, animation);
    drawPreview();
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

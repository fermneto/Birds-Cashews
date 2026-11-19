#pragma once
#include "gfx/BirdViewModel.h"
#include "gfx/Camera3D.h"
#include "gfx/Mesh.h"
#include "gfx/Shader.h"
#include <opencv2/core.hpp>
#include <memory>
#include <utility>
#include <vector>

// Desenha ceu, mundo, mar, passaro em primeira pessoa e a imagem da webcam.
class Renderer {
public:
    Renderer(const MeshBuilder& worldMesh, const MeshBuilder& cloudMesh);
    ~Renderer();

    void resize(int width, int height);
    void render(const Camera3D& camera, const BirdAnimation& animation, float time);
    void setPreview(const cv::Mat& bgrImage);
    void setPreviewVisible(bool visible) { previewVisible_ = visible; }
    void setOverlay(const cv::Mat& premultipliedBgra);
    void setCashewMeshes(const MeshBuilder& fruit, const MeshBuilder& farFruit, const MeshBuilder& stems);
    void setCashewInstances(std::vector<glm::mat4> nearby, std::vector<glm::mat4> distant);
    cv::Mat capture(const Camera3D& camera, const BirdAnimation& animation, float time);

private:
    void drawSky(const Camera3D& camera, float time);
    void drawWorld(const Camera3D& camera);
    void drawCashews();
    void drawWater(const Camera3D& camera, float time);
    void drawViewModel(const Camera3D& camera, const BirdAnimation& animation);
    void drawPreview();
    void drawOverlay();
    glm::vec4 logicalRect(float x, float y, float width, float height) const;
    void createTarget();

    Shader worldShader_, skyShader_, waterShader_, previewShader_, overlayShader_;
    Mesh worldMesh_, cloudMesh_, waterMesh_;
    std::unique_ptr<Mesh> cashewFruit_, cashewFarFruit_, cashewStems_;
    std::vector<glm::mat4> nearbyCashews_, distantCashews_;
    BirdViewModel viewModel_;
    GLuint emptyVao_ = 0, previewTexture_ = 0, overlayTexture_ = 0;
    GLuint targetFbo_ = 0, targetColor_ = 0, targetDepth_ = 0;
    int width_ = 1280, height_ = 720;
    int previewWidth_ = 0, previewHeight_ = 0;
    int overlayWidth_ = 0, overlayHeight_ = 0;
    bool previewVisible_ = true;
};

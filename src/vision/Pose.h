#pragma once
#include <opencv2/core.hpp>
#include <array>

// Indices dos 17 pontos do corpo no padrao COCO.
enum Joint { Nose = 0, LEye, REye, LEar, REar, LShoulder, RShoulder, LElbow, RElbow,
             LWrist, RWrist, LHip, RHip, LKnee, RKnee, LAnkle, RAnkle, JointCount };

struct Keypoint {
    cv::Point2f pos{0, 0};
    float conf = 0;
};

// Esqueleto de uma pessoa em coordenadas de imagem.
struct Pose {
    std::array<Keypoint, JointCount> joints;
    bool valid = false;

    bool has(int joint, float minConf = 0.3f) const { return valid && joints[joint].conf >= minConf; }
    cv::Point2f at(int joint) const { return joints[joint].pos; }

    // Espelha na horizontal mantendo os rotulos anatomicos. A deteccao roda na imagem
    // original porque o modelo erra o lado do corpo em imagens invertidas.
    Pose mirrored(int imageWidth) const {
        Pose out = *this;
        for (auto& joint : out.joints) joint.pos.x = static_cast<float>(imageWidth - 1) - joint.pos.x;
        return out;
    }
};

#pragma once
#include "ui/DemoGameplaySource.h"
#include "ui/GameUi.h"
#include "ui/PoseSource.h"
#include <functional>
#include <string>

// Webcam simulada: permite testar e fotografar a calibracao sem abrir a camera.
class ScriptedPoseSource : public PoseSource {
public:
    ScriptedPoseSource();

    bool latestPose(cv::Mat& mirroredFrame, Pose& pose) override;
    bool bodyDetected() const override { return detected_; }

    void setPose(const Pose& pose);
    void setDetected(bool detected) { detected_ = detected; }
    const cv::Mat& frame() const { return frame_; }

    // Jogador perto da camera, no quadro de 640x480: cabeca, ombros e bracos aparecem,
    // os quadris ficam rentes a borda de baixo e as pernas fora da imagem.
    static Pose upperBodyPose();
    static Pose withWrist(Pose pose, cv::Point2f position);

private:
    cv::Mat frame_;
    Pose pose_;
    bool fresh_ = false, detected_ = true;
};

// Eventos sinteticos, iguais aos que o SDL entrega durante o jogo.
void sendKey(GameUi& ui, SDL_Keycode key);
void sendText(GameUi& ui, const std::string& text);
void sendKeys(GameUi& ui, SDL_Keycode key, int times);

// Percorre todas as telas e chama "shot" com o nome de cada foto.
void runUiShowcase(GameUi& ui, DemoGameplaySource& demo, ScriptedPoseSource& camera,
                   const std::function<void(const std::string&)>& shot);

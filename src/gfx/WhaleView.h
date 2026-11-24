#pragma once
#include "gfx/Mesh.h"
#include "gfx/Shader.h"
#include "gfx/WhaleScene.h"
#include <array>
#include <memory>

// Baleia gigante low-poly, gerada em codigo: corpo com a cabeca e a mandibula separada, que gira na
// dobradica para abrir e fechar a boca. Tambem desenha as marcas no mar (sombra, espuma, borrifo e bolhas).
class WhaleView {
public:
    static constexpr float kHingeZ = -13.0f;   // Dobradica da mandibula, atras da ponta do focinho.

    WhaleView();
    // Usa o shader do mundo ja ativo, com a camera e a luz configuradas.
    void draw(const Shader& shader, const WhaleScene& scene) const;

    // Malhas em coordenadas da baleia: ponta do focinho na origem, +Z para a frente, +Y para cima.
    static MeshBuilder buildBody();   // Corpo, nadadeiras, cauda, cabeca, ceu da boca e garganta.
    static MeshBuilder buildJaw();    // Mandibula com lingua e dentes, fechada.
    static MeshBuilder buildMark(SeaMarkKind kind);
    static glm::mat4 jawTransform(float jaw);   // Gira a mandibula na dobradica.

private:
    std::unique_ptr<Mesh> body_, jaw_;
    std::array<std::unique_ptr<Mesh>, static_cast<size_t>(SeaMarkKind::Count)> marks_;
};

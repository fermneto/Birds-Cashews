#pragma once
#include "gfx/GL.h"
#include "gfx/MeshBuilder.h"

// Malha enviada para a placa de video.
class Mesh {
public:
    explicit Mesh(const std::vector<Vertex>& vertices);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    void draw() const;

private:
    GLuint vao_ = 0, vbo_ = 0;
    GLsizei count_ = 0;
};

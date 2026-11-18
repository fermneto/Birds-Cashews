#include "gfx/Mesh.h"
#include <cstddef>

Mesh::Mesh(const std::vector<Vertex>& vertices) : count_(static_cast<GLsizei>(vertices.size())) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    const auto attribute = [](GLuint index, size_t offset) {
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(index, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offset));
    };
    attribute(0, offsetof(Vertex, position));
    attribute(1, offsetof(Vertex, normal));
    attribute(2, offsetof(Vertex, color));
    glBindVertexArray(0);
}

Mesh::~Mesh() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void Mesh::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, count_);
}

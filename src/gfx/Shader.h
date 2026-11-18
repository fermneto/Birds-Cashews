#pragma once
#include "gfx/GL.h"
#include <glm/glm.hpp>
#include <string>

// Programa de shader com atalhos para os uniforms.
class Shader {
public:
    Shader(const std::string& vertexSource, const std::string& fragmentSource);
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void use() const { glUseProgram(program_); }
    void set(const char* name, float value) const;
    void set(const char* name, int value) const;
    void set(const char* name, const glm::vec3& value) const;
    void set(const char* name, const glm::vec4& value) const;
    void set(const char* name, const glm::mat4& value) const;

private:
    static GLuint compile(GLenum type, const std::string& source);
    GLuint program_;
};

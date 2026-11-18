#include "gfx/Shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

GLuint Shader::compile(GLenum type, const std::string& source) {
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Erro ao compilar shader: ") + log);
    }
    return shader;
}

Shader::Shader(const std::string& vertexSource, const std::string& fragmentSource) {
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Erro ao ligar shader: ") + log);
    }
}

Shader::~Shader() { glDeleteProgram(program_); }

void Shader::set(const char* name, float value) const { glUniform1f(glGetUniformLocation(program_, name), value); }
void Shader::set(const char* name, int value) const { glUniform1i(glGetUniformLocation(program_, name), value); }
void Shader::set(const char* name, const glm::vec3& value) const {
    glUniform3fv(glGetUniformLocation(program_, name), 1, glm::value_ptr(value));
}
void Shader::set(const char* name, const glm::vec4& value) const {
    glUniform4fv(glGetUniformLocation(program_, name), 1, glm::value_ptr(value));
}
void Shader::set(const char* name, const glm::mat4& value) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, glm::value_ptr(value));
}

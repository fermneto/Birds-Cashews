#include "gfx/Window.h"
#include <stdexcept>

Window::Window(const std::string& title, int width, int height, bool hidden) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!window_) throw std::runtime_error(SDL_GetError());
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetSwapInterval(1);
}

Window::~Window() {
    if (context_) SDL_GL_DeleteContext(context_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void Window::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

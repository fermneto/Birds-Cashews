#pragma once
#include <SDL.h>
#include <string>

// Janela SDL com contexto OpenGL 3.3.
class Window {
public:
    Window(const std::string& title, int width, int height, bool hidden);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void swap() { SDL_GL_SwapWindow(window_); }
    void toggleFullscreen();
    void drawableSize(int& width, int& height) const { SDL_GL_GetDrawableSize(window_, &width, &height); }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool fullscreen_ = false;
};

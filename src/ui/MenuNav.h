#pragma once
#include "entity/FlightControls.h"
#include <SDL.h>
#include <cstddef>
#include <string>

enum class NavAction { None, Next, Previous, Confirm, Back };

// Braco esquerdo avanca a opcao e braco direito confirma. Q e E chegam pelo mesmo campo.
NavAction navFromControls(const FlightControls& controls);

// Com um campo de texto ativo, a barra de espaco escreve em vez de confirmar.
NavAction navFromKey(SDL_Keycode key, bool editingText);

// Q e E digitados em um campo nao podem navegar. O gesto real sempre vem com o braco erguido.
bool isBodyGesture(const FlightControls& controls);

// Opcao destacada em uma lista circular.
class MenuCursor {
public:
    int index() const { return index_; }
    int count() const { return count_; }
    void setCount(int count);
    void select(int index);
    void next();
    void previous();

private:
    int count_ = 0, index_ = 0;
};

// Texto digitado pelo jogador, sempre em UTF-8 valido.
class TextField {
public:
    explicit TextField(size_t maxBytes) : maxBytes_(maxBytes) {}

    const std::string& text() const { return text_; }
    void set(const std::string& text);
    void insert(const std::string& utf8);
    void backspace();
    void clear() { text_.clear(); }

private:
    size_t maxBytes_;
    std::string text_;
};

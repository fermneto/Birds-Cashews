#include "ui/MenuNav.h"
#include <algorithm>

namespace {
bool isContinuationByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Tamanho do caractere UTF-8 iniciado por este byte, ou 0 se o byte nao puder inicia-lo.
size_t sequenceLength(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

bool isControl(unsigned char c) { return c < 0x20 || c == 0x7F; }
}

NavAction navFromControls(const FlightControls& controls) {
    if (controls.menuPressed == ArmSide::Left) return NavAction::Next;
    if (controls.menuPressed == ArmSide::Right) return NavAction::Confirm;
    return NavAction::None;
}

NavAction navFromKey(SDL_Keycode key, bool editingText) {
    switch (key) {
        case SDLK_DOWN:
        case SDLK_RIGHT: return NavAction::Next;
        case SDLK_UP:
        case SDLK_LEFT: return NavAction::Previous;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return NavAction::Confirm;
        case SDLK_SPACE: return editingText ? NavAction::None : NavAction::Confirm;
        case SDLK_ESCAPE: return NavAction::Back;
        default: return NavAction::None;
    }
}

bool isBodyGesture(const FlightControls& controls) {
    return controls.menuPressed != ArmSide::None && controls.raisedArm == controls.menuPressed;
}

// So o gesto real arma a repeticao. Q segurado no teclado avanca uma unica vez.
NavAction GestureRepeat::update(const FlightControls& controls, float seconds) {
    const NavAction action = navFromControls(controls);
    if (action != NavAction::None) {
        reset();
        const bool repeatable = action == NavAction::Next || action == NavAction::Previous;
        if (repeatable && isBodyGesture(controls)) {
            held_ = action;
            arm_ = controls.menuPressed;
            wait_ = kDelay;
        }
        return action;
    }
    if (held_ == NavAction::None || controls.raisedArm != arm_) {
        reset();
        return NavAction::None;
    }
    wait_ -= seconds;
    if (wait_ > 0) return NavAction::None;
    wait_ = kInterval;
    return held_;
}

void GestureRepeat::reset() {
    held_ = NavAction::None;
    arm_ = ArmSide::None;
    wait_ = 0;
}

void MenuCursor::setCount(int count) {
    count_ = std::max(count, 0);
    index_ = count_ == 0 ? 0 : std::clamp(index_, 0, count_ - 1);
}

void MenuCursor::select(int index) {
    index_ = count_ == 0 ? 0 : std::clamp(index, 0, count_ - 1);
}

void MenuCursor::next() {
    if (count_ > 0) index_ = (index_ + 1) % count_;
}

void MenuCursor::previous() {
    if (count_ > 0) index_ = (index_ + count_ - 1) % count_;
}

void TextField::set(const std::string& text) {
    clear();
    insert(text);
}

// Copia apenas caracteres inteiros: um acento nunca fica pela metade no limite do campo.
void TextField::insert(const std::string& utf8) {
    size_t i = 0;
    while (i < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        const size_t length = sequenceLength(lead);
        if (length == 0 || i + length > utf8.size()) return;
        if (!std::all_of(utf8.begin() + i + 1, utf8.begin() + i + length, isContinuationByte)) return;
        if (text_.size() + length > maxBytes_) return;
        if (!isControl(lead)) text_.append(utf8, i, length);
        i += length;
    }
}

void TextField::backspace() {
    while (!text_.empty()) {
        const bool continuation = isContinuationByte(text_.back());
        text_.pop_back();
        if (!continuation) return;
    }
}

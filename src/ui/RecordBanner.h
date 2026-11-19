#pragma once

// Animacao do aviso "Novo record!": entra com um salto, fica visivel e some aos poucos.
class RecordBanner {
public:
    void update(bool beatenNow, float seconds);

    bool visible() const { return active_; }
    float opacity() const;
    float scale() const;

private:
    bool active_ = false, wasBeaten_ = false;
    float elapsed_ = 0, fading_ = 0;
};

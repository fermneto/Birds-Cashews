#pragma once
#include <chrono>

// Tempo monotonico em segundos, usado para medir gestos e latencia.
inline double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

#pragma once
#include <iostream>
#include <string>

// Acumula o resultado dos testes automaticos.
struct TestRun {
    int failures = 0;
    void check(const std::string& name, bool passed) {
        std::cout << (passed ? "[ok]   " : "[FALHA] ") << name << "\n";
        if (!passed) failures++;
    }
};

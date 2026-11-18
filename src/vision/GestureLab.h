#pragma once
#include <string>

// Ferramentas da prova tecnica de visao.
int runInferenceBenchmark(const std::string& modelPath);
int runCameraTest(int cameraIndex, const std::string& modelPath, int seconds);
int runGestureLab(int cameraIndex, const std::string& modelPath);
int runGestureSelfTest();

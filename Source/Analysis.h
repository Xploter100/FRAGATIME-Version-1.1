#pragma once
#include <JuceHeader.h>

// Resultado del analisis del audio cargado (BPM + rasgos para la red de Tabie)
struct AnalysisResult
{
    bool   valid = false;
    double bpm = 0.0;
    // 0 volumen, 1 cruces por cero, 2 brillo, 3 densidad de golpes, 4 dinamica, 5 BPM normalizado
    float  feat[6] { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    // posicion (0..1 del archivo) de tres zonas interesantes: energia, golpes, brillo
    float  segStart[3] { 0.f, 0.33f, 0.66f };
};

AnalysisResult analyzeAudio (const juce::AudioBuffer<float>& buf, double sampleRate);

#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

// Red neuronal ligera de Tabie: 19 entradas -> 32 neuronas (tanh) -> 15 salidas (sigmoide).
// ~1.1 mil pesos. Arranca desde un "prior" basado en reglas por genero y se ajusta
// localmente con los likes / dislikes del usuario. Todo se guarda en la PC del usuario.
class TabieModel
{
public:
    static constexpr int kIn = 19, kHid = 32, kOut = 15, kGenres = 10;
    using In  = std::array<float, kIn>;
    using Vec = std::array<float, kOut>;
    // Vec: 0 density, 1 size, 2 pitch, 3 arp, 4 grain, 5 feedback, 6 scan, 7 reverb,
    //      8 delay, 9 crush, 10 filter, 11 drive, 12 dir, 13 sync, 14 intensity

    TabieModel() = default;

    void ensureReady();                                     // carga el modelo o lo entrena la primera vez
    Vec  predict (const In& x) const;
    void addFeedback (const In& x, const Vec& used, bool like);
    int  numFeedback() const;
    void resetToPrior();

    static In  makeInput (int genre, int style, const float feat[6]);
    static Vec prior (int genre, int style, const float feat[6]);
    static const char* genreName (int g);

private:
    struct Weights
    {
        float w1[kHid][kIn];
        float b1[kHid];
        float w2[kOut][kHid];
        float b2[kOut];
    };
    struct Sample { In x; Vec t; float w; };

    static void forward (const Weights& w, const In& x, std::array<float, kHid>& h, Vec& o);
    void initWeights();
    void bootstrap();
    void train (const std::vector<Sample>& data, int epochs, float lr);
    juce::File modelFile() const;
    void save() const;
    bool load();

    Weights W {};
    std::vector<Sample> replay;
    bool ready = false;
    mutable juce::CriticalSection lock;
    juce::Random rng { 20260101 };
};

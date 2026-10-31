#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

// Tablas del modo SYNC (densidad = granos por pulso, tamano = duracion en pulsos)
struct SyncTables
{
    static constexpr float kDivs[8] { 0.5f, 1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f };
    static constexpr const char* kDivNames[8] { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" };
    static constexpr float kLens[8] { 0.125f, 0.1667f, 0.25f, 0.3333f, 0.5f, 1.f, 2.f, 4.f };
    static constexpr const char* kLenNames[8] { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", "1/2", "1 bar" };
    static int index (float v01) { return juce::jlimit (0, 7, (int) (v01 * 8.f)); }
};

struct GrainParams
{
    float density = 0.45f, size = 0.45f;                       // 0..1
    float pitchPct = 50.f;                                     // 0..100 (50 = sin cambio, +-12 st)
    float arp = 0.f, grain = 0.35f, feedback = 0.f, position = 0.f;   // 0..1
    float scan = 0.f;                                          // -1..1
    float reverb = 0.15f, delay = 0.1f, crush = 0.f, drive = 0.f;     // 0..1
    float filter = 0.f;                                        // -1..1 (negativo LP, positivo HP)
    float outDb = 0.f;
    int   source = 0;                                          // 0 = FILE, 1 = LIVE
    int   dir = 0;                                             // 0 FWD, 1 REV, 2 RND
    bool  sync = false;
    bool  spawn = true;                                        // false = no crear granos nuevos (cola)
    double bpm = 120.0;
};

struct FileView
{
    const float* L = nullptr;
    const float* R = nullptr;
    int N = 0;
    double rate = 44100.0;
    bool valid() const { return L != nullptr && R != nullptr && N > 16; }
};

class GranularEngine
{
public:
    static constexpr int kGrains = 48;

    void prepare (double sampleRate);
    void reset();
    void resetFile() { scanOffset = 0.0; }

    // Escribe la senal procesada (sin mezcla con la original) en wetL/wetR.
    // inL/inR pueden ser nullptr (silencio).
    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n,
                  const GrainParams& p, const FileView& file, double ppqStart, bool ppqValid);

    void fillDisplay (std::array<std::atomic<float>, 32>& dst) const;
    float getCenterNorm() const { return centerNorm; }

private:
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    using Filter = juce::dsp::IIR::Filter<float>;

    struct Grain
    {
        bool active = false, file = true;
        double pos = 0.0, inc = 1.0;
        int age = 0, len = 1;
        float gL = 1.f, gR = 1.f, level = 1.f, sharp = 0.f;
        float normPos = -1.f;
    };

    double fs = 44100.0;
    juce::Random rng;
    std::array<Grain, kGrains> grains;
    float hannT[1026] {}, trapT[1026] {};
    int arpStep = 0;
    float spawnCounter = 0.f;
    double scanOffset = 0.0;
    double ppq = 0.0;
    juce::int64 lastCell = 0;
    bool haveCell = false;
    float centerNorm = 0.f;

    std::vector<float> ringL, ringR;
    int ringLen = 0, ringW = 0;

    Filter fltL, fltR;
    int fltMode = 0;
    float lastFilt = 999.f;
    int holdCnt = 0;
    float heldL = 0.f, heldR = 0.f;
    std::vector<float> dlyL, dlyR;
    int dlySize = 0, dlyW = 0;
    float dlySm = 0.f, dlpL = 0.f, dlpR = 0.f;
    juce::Reverb reverb;
};

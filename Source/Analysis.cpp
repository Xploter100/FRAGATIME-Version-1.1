#include "Analysis.h"

AnalysisResult analyzeAudio (const juce::AudioBuffer<float>& b, double sr)
{
    AnalysisResult r;
    const int n = b.getNumSamples();
    if (n < (int) (sr * 2.0) || b.getNumChannels() < 2) return r;

    // se analizan como maximo los primeros 90 s, a ~22 kHz
    const int nUse = (int) juce::jmin ((juce::int64) n, (juce::int64) (sr * 90.0));
    const int dec = juce::jmax (1, (int) std::round (sr / 22050.0));
    const double sr2 = sr / (double) dec;
    const float* L = b.getReadPointer (0);
    const float* R = b.getReadPointer (1);

    std::vector<float> m;
    m.reserve ((size_t) (nUse / dec + 1));
    for (int i = 0; i + dec <= nUse; i += dec)
    {
        float s = 0.f;
        for (int d = 0; d < dec; ++d) s += L[i + d] + R[i + d];
        m.push_back (s * (0.5f / (float) dec));
    }

    const int hop = 128, win = 512;
    if ((int) m.size() < win + hop * 64) return r;
    const int frames = ((int) m.size() - win) / hop;

    // estadisticas globales
    double totE = 0.0, totD = 0.0, peak = 0.0;
    int zc = 0;
    for (size_t i = 1; i < m.size(); ++i)
    {
        const double x = m[i], d = m[i] - m[i - 1];
        totE += x * x; totD += d * d;
        peak = juce::jmax (peak, std::abs (x));
        if ((m[i] >= 0.f) != (m[i - 1] >= 0.f)) ++zc;
    }
    const double rms = std::sqrt (totE / (double) m.size());

    // envolventes por cuadro
    std::vector<float> env ((size_t) frames), hi ((size_t) frames);
    for (int k = 0; k < frames; ++k)
    {
        const int s0 = k * hop;
        double e = 0.0, ed = 0.0;
        for (int j = 0; j < win; ++j)
        {
            const double x = m[(size_t) (s0 + j)];
            e += x * x;
            if (j > 0) { const double d = x - m[(size_t) (s0 + j - 1)]; ed += d * d; }
        }
        env[(size_t) k] = (float) std::sqrt (e / win);
        hi[(size_t) k]  = (float) std::sqrt (ed / win);
    }

    // funcion de onset (flujo de la envolvente de agudos)
    std::vector<float> on ((size_t) frames, 0.f);
    for (int k = 1; k < frames; ++k)
    {
        const float v = std::log1p (100.f * hi[(size_t) k]) - std::log1p (100.f * hi[(size_t) k - 1]);
        on[(size_t) k] = juce::jmax (0.f, v);
    }
    {   // quitar la media local
        std::vector<double> pre ((size_t) frames + 1, 0.0);
        for (int k = 0; k < frames; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + on[(size_t) k];
        const int w = 32;
        for (int k = 0; k < frames; ++k)
        {
            const int a = juce::jmax (0, k - w), c = juce::jmin (frames, k + w);
            const double mean = (pre[(size_t) c] - pre[(size_t) a]) / (double) (c - a);
            on[(size_t) k] = juce::jmax (0.f, on[(size_t) k] - (float) mean);
        }
    }

    // BPM por autocorrelacion (60 - 200 BPM) con preferencia suave hacia ~115
    const double fps = sr2 / (double) hop;
    const int lagMin = (int) std::floor (fps * 60.0 / 200.0);
    const int lagMax = juce::jmin ((int) std::ceil (fps * 60.0 / 60.0), frames / 2);
    if (lagMax <= lagMin + 2) return r;

    std::vector<double> ac ((size_t) lagMax + 2, 0.0);
    int best = lagMin;
    double bestScore = -1.0;
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        double sum = 0.0;
        for (int k = 0; k + lag < frames; ++k) sum += (double) on[(size_t) k] * (double) on[(size_t) (k + lag)];
        ac[(size_t) lag] = sum / (double) (frames - lag);
        const double bpm = 60.0 * fps / (double) lag;
        const double lg = std::log2 (bpm / 115.0) / 0.7;
        const double score = ac[(size_t) lag] * std::exp (-0.5 * lg * lg);
        if (score > bestScore) { bestScore = score; best = lag; }
    }
    double lagF = (double) best;
    if (best > lagMin && best < lagMax)
    {
        const double y0 = ac[(size_t) best - 1], y1 = ac[(size_t) best], y2 = ac[(size_t) best + 1];
        const double den = y0 - 2.0 * y1 + y2;
        if (std::abs (den) > 1e-12) lagF += juce::jlimit (-1.0, 1.0, 0.5 * (y0 - y2) / den);
    }
    double bpm = 60.0 * fps / lagF;
    while (bpm < 70.0)  bpm *= 2.0;
    while (bpm > 180.0) bpm *= 0.5;
    r.bpm = bpm;

    // golpes por segundo
    float onMax = 0.f;
    for (auto v : on) onMax = juce::jmax (onMax, v);
    int peaks = 0;
    for (int k = 1; k + 1 < frames; ++k)
        if (on[(size_t) k] > 0.4f * onMax && on[(size_t) k] >= on[(size_t) k - 1] && on[(size_t) k] > on[(size_t) k + 1]) ++peaks;
    const double seconds = (double) frames / fps;
    const float onsetRate = (float) (peaks / juce::jmax (1.0, seconds));

    // rasgos para la red
    const double rmsDb = 20.0 * std::log10 (juce::jmax (rms, 1e-6));
    r.feat[0] = juce::jlimit (0.f, 1.f, (float) ((rmsDb + 50.0) / 50.0));
    r.feat[1] = juce::jlimit (0.f, 1.f, (float) ((double) zc / (double) m.size() * 2.0));
    r.feat[2] = juce::jlimit (0.f, 1.f, (float) (std::sqrt (totD / juce::jmax (totE, 1e-12)) / 1.5));
    r.feat[3] = juce::jlimit (0.f, 1.f, onsetRate / 8.f);
    r.feat[4] = juce::jlimit (0.f, 1.f, (float) (20.0 * std::log10 (juce::jmax (peak, 1e-6) / juce::jmax (rms, 1e-6)) / 20.0));
    r.feat[5] = juce::jlimit (0.f, 1.f, (float) ((bpm - 60.0) / 120.0));

    // tres zonas interesantes (16 segmentos): mas energia, mas golpes, mas brillo
    double segE[16] {}, segO[16] {}, segH[16] {};
    int segN[16] {};
    for (int k = 0; k < frames; ++k)
    {
        const int s = juce::jmin (15, k * 16 / frames);
        segE[s] += env[(size_t) k]; segO[s] += on[(size_t) k]; segH[s] += hi[(size_t) k]; ++segN[s];
    }
    bool used[16] {};
    auto pick = [&] (const double* v)
    {
        int bi = -1; double bv = -1.0;
        for (int s = 0; s < 16; ++s)
            if (! used[s] && segN[s] > 0 && v[s] / segN[s] > bv) { bv = v[s] / segN[s]; bi = s; }
        if (bi < 0) bi = 0;
        used[bi] = true;
        return bi;
    };
    const float span = (float) nUse / (float) n;
    r.segStart[0] = (float) pick (segE) / 16.f * span;
    r.segStart[1] = (float) pick (segO) / 16.f * span;
    r.segStart[2] = (float) pick (segH) / 16.f * span;

    r.valid = true;
    return r;
}

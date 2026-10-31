#include "GranularEngine.h"

namespace
{
constexpr float kPi = 3.14159265358979f;
const float arpPattern[8] { 0.f, 7.f, 12.f, 16.f, 19.f, 24.f, 19.f, 12.f };   // arpegio mayor ascendente
}

void GranularEngine::prepare (double sampleRate)
{
    fs = sampleRate;
    ringLen = (int) (6.0 * fs);
    ringL.assign ((size_t) ringLen, 0.f);
    ringR.assign ((size_t) ringLen, 0.f);

    dlySize = (int) (2.6 * fs) + 8;
    dlyL.assign ((size_t) dlySize, 0.f);
    dlyR.assign ((size_t) dlySize, 0.f);

    for (int i = 0; i < 1026; ++i)
    {
        const float t = juce::jmin (1.f, (float) i / 1024.f);
        const float s = std::sin (kPi * t);
        hannT[i] = s * s;
        trapT[i] = juce::jlimit (0.f, 1.f, juce::jmin (t / 0.08f, (1.f - t) / 0.08f));
    }

    fltL.coefficients = Coeffs::makeLowPass (fs, 18000.f);
    fltR.coefficients = fltL.coefficients;
    reverb.setSampleRate (fs);
    reset();
}

void GranularEngine::reset()
{
    for (auto& g : grains) g = Grain();
    std::fill (ringL.begin(), ringL.end(), 0.f);
    std::fill (ringR.begin(), ringR.end(), 0.f);
    std::fill (dlyL.begin(), dlyL.end(), 0.f);
    std::fill (dlyR.begin(), dlyR.end(), 0.f);
    ringW = 0; dlyW = 0; dlySm = 0.f; dlpL = dlpR = 0.f;
    spawnCounter = 0.f; arpStep = 0; scanOffset = 0.0;
    ppq = 0.0; lastCell = 0; haveCell = false;
    holdCnt = 0; heldL = heldR = 0.f;
    fltL.reset(); fltR.reset();
    fltMode = 0; lastFilt = 999.f;
    reverb.reset();
}

void GranularEngine::process (const float* inL, const float* inR, float* wetL, float* wetR, int n,
                              const GrainParams& p, const FileView& file, double ppqStart, bool ppqValid)
{
    if (n <= 0 || ringLen == 0) return;

    const bool fileAvail = file.valid();
    const int N = fileAvail ? file.N : 0;
    const float* fL = file.L;
    const float* fR = file.R;
    const double fileRate = file.rate;

    // ---- valores derivados ----
    const double beatSec = 60.0 / juce::jlimit (30.0, 300.0, p.bpm);
    float densityHz, lenSec, syncDiv = 1.f;
    if (p.sync)
    {
        syncDiv   = SyncTables::kDivs[SyncTables::index (p.density)];
        densityHz = syncDiv / (float) beatSec;
        lenSec    = SyncTables::kLens[SyncTables::index (p.size)] * (float) beatSec;
    }
    else
    {
        densityHz = 2.f * std::pow (60.f, p.density);                 // 2 .. 120 granos/s
        lenSec    = 0.01f * std::pow (50.f, p.size);                  // 10 .. 500 ms
    }
    const int   lenBase   = juce::jmax (64, (int) (lenSec * (float) fs));
    const float baseSemis = (p.pitchPct - 50.f) / 50.f * 12.f;
    const float overlap   = densityHz * lenSec;
    const float level     = 0.8f / std::sqrt (juce::jmax (1.f, overlap));
    const int   arpLen    = 2 + (int) (p.arp * 6.f);
    const float arpProb   = juce::jmin (1.f, p.arp * 3.3f);
    const bool  ringInput = (p.source == 1) || ! fileAvail;
    const float fbGain    = p.feedback * 0.85f;
    const float fbProb    = ringInput ? 0.f : p.feedback * 0.7f;
    const double ppqInc   = p.bpm / 60.0 / fs;

    if (ppqValid) ppq = ppqStart;

    // filtro (negativo = low-pass, positivo = high-pass)
    if (p.filter != lastFilt)
    {
        lastFilt = p.filter;
        if (p.filter < -0.02f)
        {
            fltMode = 1;
            fltL.coefficients = Coeffs::makeLowPass (fs, juce::jlimit (60.f, 18000.f, 18000.f * std::pow (0.0125f, -p.filter)));
            fltR.coefficients = fltL.coefficients;
        }
        else if (p.filter > 0.02f)
        {
            fltMode = 2;
            fltL.coefficients = Coeffs::makeHighPass (fs, juce::jlimit (20.f, 8000.f, 20.f * std::pow (300.f, p.filter)));
            fltR.coefficients = fltL.coefficients;
        }
        else fltMode = 0;
    }

    // bitcrusher
    const float bits   = 16.f - p.crush * 12.f;
    const float levels = std::pow (2.f, bits - 1.f);
    const int   holdN  = 1 + (int) (p.crush * p.crush * 24.f);
    const float dgain  = 1.f + p.drive * 9.f, dcomp = 1.f / (1.f + p.drive * 0.6f);
    const float dWet   = p.delay * 0.8f;
    const float delayTarget = (float) (juce::jlimit (0.05, 2.4, beatSec * 0.75) * fs);

    // ---- crear un grano ----
    auto spawn = [&]
    {
        int slot = -1;
        for (int k = 0; k < kGrains; ++k) if (! grains[(size_t) k].active) { slot = k; break; }
        if (slot < 0) return;

        Grain& g = grains[(size_t) slot];
        g = Grain();
        g.active = true;

        const bool useFile = fileAvail && p.source == 0 && rng.nextFloat() >= fbProb;

        float semis = baseSemis;
        if (p.arp > 0.001f && rng.nextFloat() < arpProb) { semis += arpPattern[arpStep % arpLen]; ++arpStep; }
        semis += (rng.nextFloat() * 2.f - 1.f) * p.grain * 0.25f;
        semis = juce::jlimit (-24.f, 24.f, semis);
        const float rate = std::pow (2.f, semis / 12.f);

        const float dir = p.dir == 0 ? 1.f : (p.dir == 1 ? -1.f : (rng.nextBool() ? 1.f : -1.f));

        const float lenJitter = p.sync ? 0.f : (rng.nextFloat() - 0.5f) * p.grain * 0.6f;
        g.len = juce::jmax (64, (int) ((float) lenBase * (1.f + lenJitter)));
        g.level = level;
        g.sharp = p.grain;

        const float pan = (rng.nextFloat() * 2.f - 1.f) * p.grain;
        const float ang = (pan + 1.f) * kPi * 0.25f;
        g.gL = std::cos (ang) * 1.4142f;
        g.gR = std::sin (ang) * 1.4142f;
        g.file = useFile;

        if (useFile)
        {
            g.inc = (double) rate * fileRate / fs * (double) dir;
            const double lenF = (double) g.len * std::abs (g.inc);
            const double center = (double) p.position * (double) N + scanOffset;
            const double spray = (double) (rng.nextFloat() * 2.f - 1.f) * (0.01 + (double) p.grain * 0.6) * fileRate;
            double start = center + spray;
            if (dir < 0.f) start += lenF;
            start = std::fmod (start, (double) N);
            if (start < 0.0) start += (double) N;
            g.pos = start;
        }
        else
        {
            g.inc = (double) rate * (double) dir;
            const double lenF = (double) g.len * std::abs (g.inc);
            const double maxLook = juce::jmax (0.0, (double) ringLen - 2.0 * lenF - 256.0);
            double look = (double) p.position * maxLook + (double) rng.nextFloat() * (double) p.grain * 0.3 * fs;
            look = juce::jmin (look, maxLook);
            double start = dir > 0.f ? (double) ringW - 64.0 - lenF - look : (double) ringW - 64.0 - look;
            while (start < 0.0) start += (double) ringLen;
            g.pos = start;
        }
    };

    // ---- bucle por muestra ----
    for (int i = 0; i < n; ++i)
    {
        const float xl = inL != nullptr ? inL[i] : 0.f;
        const float xr = inR != nullptr ? inR[i] : 0.f;

        if (fileAvail)
        {
            scanOffset += (double) p.scan * fileRate / fs;
            if (scanOffset >= (double) N) scanOffset -= (double) N;
            else if (scanOffset < 0.0)    scanOffset += (double) N;
        }

        bool doSpawn = false;
        if (p.spawn)
        {
            if (p.sync && ppqValid)
            {
                ppq += ppqInc;
                const juce::int64 cell = (juce::int64) std::floor (ppq * (double) syncDiv);
                if (! haveCell || cell != lastCell) { lastCell = cell; haveCell = true; doSpawn = true; }
            }
            else
            {
                spawnCounter -= 1.f;
                if (spawnCounter <= 0.f)
                {
                    doSpawn = true;
                    const float jitter = p.sync ? 1.f : (1.f + (rng.nextFloat() - 0.5f) * p.grain * 0.9f);
                    spawnCounter += ((float) fs / densityHz) * jitter;
                }
            }
        }
        else if (ppqValid) ppq += ppqInc;
        if (doSpawn) spawn();

        // suma de granos
        float gl = 0.f, gr = 0.f;
        for (auto& g : grains)
        {
            if (! g.active) continue;

            const float t  = (float) g.age / (float) g.len;
            const float fi = t * 1024.f;
            const int   i0 = (int) fi;
            const float fr = fi - (float) i0;
            const float wh = hannT[i0] + fr * (hannT[i0 + 1] - hannT[i0]);
            const float wt = trapT[i0] + fr * (trapT[i0 + 1] - trapT[i0]);
            const float w  = wh + g.sharp * (wt - wh);

            float sl, sr;
            if (g.file && fileAvail)
            {
                const int ip = (int) g.pos;
                if (g.pos < 0.0 || ip >= N - 1) { g.active = false; continue; }
                const float f = (float) (g.pos - (double) ip);
                sl = fL[ip] + f * (fL[ip + 1] - fL[ip]);
                sr = fR[ip] + f * (fR[ip + 1] - fR[ip]);
            }
            else
            {
                double pp = g.pos;
                while (pp < 0.0) pp += (double) ringLen;
                while (pp >= (double) ringLen) pp -= (double) ringLen;
                const int ip = (int) pp;
                int ip1 = ip + 1; if (ip1 >= ringLen) ip1 = 0;
                const float f = (float) (pp - (double) ip);
                sl = ringL[(size_t) ip] + f * (ringL[(size_t) ip1] - ringL[(size_t) ip]);
                sr = ringR[(size_t) ip] + f * (ringR[(size_t) ip1] - ringR[(size_t) ip]);
            }

            const float amp = w * g.level;
            gl += sl * amp * g.gL;
            gr += sr * amp * g.gR;

            g.pos += g.inc;
            if (++g.age >= g.len) g.active = false;
        }

        // buffer circular: entrada en vivo + feedback de la nube
        ringL[(size_t) ringW] = (ringInput ? xl : 0.f) + std::tanh (gl) * fbGain;
        ringR[(size_t) ringW] = (ringInput ? xr : 0.f) + std::tanh (gr) * fbGain;
        if (++ringW >= ringLen) ringW = 0;

        float l = gl, r = gr;

        if (p.drive > 0.001f) { l = std::tanh (l * dgain) * dcomp; r = std::tanh (r * dgain) * dcomp; }

        if (p.crush > 0.01f)
        {
            if (--holdCnt <= 0)
            {
                holdCnt = holdN;
                heldL = std::round (l * levels) / levels;
                heldR = std::round (r * levels) / levels;
            }
            l = heldL; r = heldR;
        }

        if (fltMode != 0) { l = fltL.processSample (l); r = fltR.processSample (r); }

        // delay ping-pong sincronizado al tempo
        dlySm += (delayTarget - dlySm) * 0.0005f;
        float rd = (float) dlyW - dlySm;
        if (rd < 0.f) rd += (float) dlySize;
        const int d0 = juce::jmin ((int) rd, dlySize - 1);
        const float df = rd - (float) d0;
        int d1 = d0 + 1; if (d1 >= dlySize) d1 = 0;
        const float wl = dlyL[(size_t) d0] + df * (dlyL[(size_t) d1] - dlyL[(size_t) d0]);
        const float wr = dlyR[(size_t) d0] + df * (dlyR[(size_t) d1] - dlyR[(size_t) d0]);
        dlpL += 0.3f * (wl - dlpL);
        dlpR += 0.3f * (wr - dlpR);
        dlyL[(size_t) dlyW] = l + dlpR * 0.45f;
        dlyR[(size_t) dlyW] = r + dlpL * 0.45f;
        if (++dlyW >= dlySize) dlyW = 0;
        l += wl * dWet; r += wr * dWet;

        wetL[i] = l;
        wetR[i] = r;
    }

    // ---- reverb ----
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.8f; rp.damping = 0.5f; rp.width = 1.f; rp.freezeMode = 0.f;
    rp.wetLevel = p.reverb * 0.5f;
    rp.dryLevel = 1.f;
    reverb.setParameters (rp);
    reverb.processStereo (wetL, wetR, n);

    // ---- ganancia de salida + limitador suave ----
    const float outGain = juce::Decibels::decibelsToGain (p.outDb);
    for (int i = 0; i < n; ++i)
    {
        wetL[i] = std::tanh (wetL[i] * outGain);
        wetR[i] = std::tanh (wetR[i] * outGain);
    }

    // ---- datos para la pantalla ----
    for (auto& g : grains)
        g.normPos = (g.active && g.file && N > 0) ? juce::jlimit (0.f, 1.f, (float) (g.pos / (double) N)) : -1.f;
    centerNorm = N > 0 ? (float) std::fmod (((double) p.position * (double) N + scanOffset) / (double) N, 1.0) : 0.f;
}

void GranularEngine::fillDisplay (std::array<std::atomic<float>, 32>& dst) const
{
    int k = 0;
    for (auto& g : grains)
        if (g.active && g.normPos >= 0.f && k < 32) dst[(size_t) k++].store (g.normPos);
    for (; k < 32; ++k) dst[(size_t) k].store (-1.f);
}

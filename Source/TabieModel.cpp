#include "TabieModel.h"

//==============================================================================
const char* TabieModel::genreName (int g)
{
    static const char* n[kGenres] { "TRAP", "HIP-HOP", "LO-FI", "HOUSE", "TECHNO", "DNB", "AMBIENT", "POP", "REGGAETON", "PHONK" };
    return n[juce::jlimit (0, kGenres - 1, g)];
}

TabieModel::In TabieModel::makeInput (int genre, int style, const float feat[6])
{
    In x {};
    x[(size_t) juce::jlimit (0, kGenres - 1, genre)] = 1.f;
    x[(size_t) (kGenres + juce::jlimit (0, 2, style))] = 1.f;
    for (int i = 0; i < 6; ++i) x[(size_t) (kGenres + 3 + i)] = juce::jlimit (0.f, 1.f, feat[i]);
    return x;
}

// Conocimiento inicial: ajustes tipicos por genero y por estilo (A tight, B groove, C wide)
TabieModel::Vec TabieModel::prior (int genre, int style, const float f[6])
{
    //                  dens  size  arp   grain fb    rev   del   crush drive filt  dir   sync
    static const float B[kGenres][12] = {
        { .70f, .25f, .35f, .30f, .10f, .20f, .25f, .10f, .25f, .50f, .15f, 1.f },   // trap
        { .55f, .35f, .15f, .35f, .08f, .15f, .20f, .15f, .30f, .45f, .10f, 1.f },   // hip-hop
        { .45f, .45f, .20f, .45f, .15f, .35f, .25f, .35f, .20f, .30f, .15f, 1.f },   // lo-fi
        { .65f, .30f, .30f, .25f, .12f, .25f, .30f, .05f, .20f, .52f, .10f, 1.f },   // house
        { .75f, .22f, .20f, .30f, .20f, .20f, .30f, .20f, .45f, .55f, .10f, 1.f },   // techno
        { .85f, .18f, .40f, .40f, .15f, .15f, .20f, .15f, .35f, .52f, .20f, 1.f },   // dnb
        { .35f, .80f, .15f, .60f, .45f, .70f, .45f, .00f, .00f, .40f, .40f, 0.f },   // ambient
        { .50f, .35f, .25f, .20f, .08f, .25f, .20f, .00f, .10f, .50f, .05f, 1.f },   // pop
        { .60f, .28f, .25f, .25f, .08f, .20f, .25f, .05f, .20f, .50f, .10f, 1.f },   // reggaeton
        { .65f, .30f, .30f, .50f, .25f, .30f, .30f, .40f, .55f, .35f, .30f, 1.f }    // phonk
    };
    const int g = juce::jlimit (0, kGenres - 1, genre);
    const float* b = B[g];
    float dens = b[0], size = b[1], arp = b[2], grain = b[3], fb = b[4], rev = b[5], del = b[6];
    float crush = b[7], drive = b[8], filt = b[9], dir = b[10], sync = b[11];
    float pitch = 0.5f, scan = 0.5f, inten = 0.85f;

    switch (style)
    {
        case 0:  dens += .15f; size -= .12f; arp -= .10f; grain -= .10f; fb -= .05f; rev -= .08f; dir -= .05f;
                 scan = .5f; inten = .80f; sync = .55f + .4f * sync; break;
        case 1:  arp += .25f; grain += .05f; del += .05f;
                 scan = .62f; inten = .85f; sync = .55f + .4f * sync; break;
        default: dens -= .20f; size += .35f; arp -= .05f; grain += .25f; fb += .20f; rev += .25f; del += .15f;
                 pitch = .375f; dir += .30f; scan = .56f; inten = .90f; sync = sync * .2f; break;
    }
    if (g == 2) pitch -= .04f;
    if (g == 9) pitch -= .08f;
    if (g == 6) pitch += .04f;

    // el material cambia un poco el resultado
    crush -= (f[2] - .5f) * .20f;
    drive -= (f[2] - .5f) * .15f;
    dens  -= (f[3] - .5f) * .12f;
    rev   -= (f[0] - .5f) * .05f;

    Vec v { dens, size, pitch, arp, grain, fb, scan, rev, del, crush, filt, drive, dir, sync, inten };
    for (auto& e : v) e = juce::jlimit (0.f, 1.f, e);
    return v;
}

//==============================================================================
void TabieModel::forward (const Weights& w, const In& x, std::array<float, kHid>& h, Vec& o)
{
    for (int i = 0; i < kHid; ++i)
    {
        float s = w.b1[i];
        for (int k = 0; k < kIn; ++k) s += w.w1[i][k] * x[(size_t) k];
        h[(size_t) i] = std::tanh (s);
    }
    for (int j = 0; j < kOut; ++j)
    {
        float s = w.b2[j];
        for (int i = 0; i < kHid; ++i) s += w.w2[j][i] * h[(size_t) i];
        o[(size_t) j] = 1.f / (1.f + std::exp (-s));
    }
}

void TabieModel::initWeights()
{
    juce::Random r (777);
    for (int i = 0; i < kHid; ++i)
    {
        for (int k = 0; k < kIn; ++k) W.w1[i][k] = (r.nextFloat() * 2.f - 1.f) * 0.35f;
        W.b1[i] = 0.f;
    }
    for (int j = 0; j < kOut; ++j)
    {
        for (int i = 0; i < kHid; ++i) W.w2[j][i] = (r.nextFloat() * 2.f - 1.f) * 0.3f;
        W.b2[j] = 0.f;
    }
}

void TabieModel::train (const std::vector<Sample>& data, int epochs, float lr)
{
    if (data.empty()) return;
    std::vector<int> idx (data.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int) i;

    for (int e = 0; e < epochs; ++e)
    {
        for (int i = (int) idx.size() - 1; i > 0; --i) std::swap (idx[(size_t) i], idx[(size_t) rng.nextInt (i + 1)]);

        for (int id : idx)
        {
            const Sample& s = data[(size_t) id];
            std::array<float, kHid> h;
            Vec o;
            forward (W, s.x, h, o);

            float dOut[kOut], dH[kHid];
            for (int j = 0; j < kOut; ++j) dOut[j] = (o[(size_t) j] - s.t[(size_t) j]) * s.w;
            for (int i = 0; i < kHid; ++i)
            {
                float sum = 0.f;
                for (int j = 0; j < kOut; ++j) sum += W.w2[j][i] * dOut[j];
                dH[i] = sum * (1.f - h[(size_t) i] * h[(size_t) i]);
            }
            for (int j = 0; j < kOut; ++j)
            {
                for (int i = 0; i < kHid; ++i) W.w2[j][i] -= lr * dOut[j] * h[(size_t) i];
                W.b2[j] -= lr * dOut[j];
            }
            for (int i = 0; i < kHid; ++i)
            {
                for (int k = 0; k < kIn; ++k) W.w1[i][k] -= lr * dH[i] * s.x[(size_t) k];
                W.b1[i] -= lr * dH[i];
            }
        }
        lr *= 0.96f;
    }
}

static void randomFeat (juce::Random& r, float* f)
{
    for (int i = 0; i < 6; ++i) f[i] = r.nextFloat();
}

void TabieModel::bootstrap()
{
    std::vector<Sample> data;
    data.reserve (2400);
    for (int i = 0; i < 2400; ++i)
    {
        float f[6]; randomFeat (rng, f);
        const int g = rng.nextInt (kGenres), s = rng.nextInt (3);
        data.push_back ({ makeInput (g, s, f), prior (g, s, f), 1.f });
    }
    train (data, 30, 0.08f);
}

//==============================================================================
juce::File TabieModel::modelFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("TapeLab").getChildFile ("FRAGATIME").getChildFile ("tabie_model_v1.bin");
}

void TabieModel::save() const
{
    auto f = modelFile();
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (! out.openedOk()) return;
    out.writeInt (0x54424945);
    out.writeInt (1);
    out.writeInt (kIn); out.writeInt (kHid); out.writeInt (kOut);
    for (int i = 0; i < kHid; ++i) { for (int k = 0; k < kIn; ++k) out.writeFloat (W.w1[i][k]); out.writeFloat (W.b1[i]); }
    for (int j = 0; j < kOut; ++j) { for (int i = 0; i < kHid; ++i) out.writeFloat (W.w2[j][i]); out.writeFloat (W.b2[j]); }
    out.writeInt ((int) replay.size());
    for (auto& s : replay)
    {
        for (auto v : s.x) out.writeFloat (v);
        for (auto v : s.t) out.writeFloat (v);
        out.writeFloat (s.w);
    }
    out.flush();
}

bool TabieModel::load()
{
    auto f = modelFile();
    if (! f.existsAsFile()) return false;
    juce::FileInputStream in (f);
    if (! in.openedOk()) return false;
    if (in.readInt() != 0x54424945 || in.readInt() != 1) return false;
    if (in.readInt() != kIn || in.readInt() != kHid || in.readInt() != kOut) return false;
    for (int i = 0; i < kHid; ++i) { for (int k = 0; k < kIn; ++k) W.w1[i][k] = in.readFloat(); W.b1[i] = in.readFloat(); }
    for (int j = 0; j < kOut; ++j) { for (int i = 0; i < kHid; ++i) W.w2[j][i] = in.readFloat(); W.b2[j] = in.readFloat(); }
    const int cnt = in.readInt();
    if (cnt < 0 || cnt > 5000) return false;
    replay.clear();
    for (int c = 0; c < cnt; ++c)
    {
        Sample s;
        for (auto& v : s.x) v = in.readFloat();
        for (auto& v : s.t) v = in.readFloat();
        s.w = in.readFloat();
        replay.push_back (s);
    }
    return true;
}

//==============================================================================
void TabieModel::ensureReady()
{
    const juce::ScopedLock sl (lock);
    if (ready) return;
    if (load()) { ready = true; return; }
    initWeights();
    bootstrap();
    ready = true;
    save();
}

TabieModel::Vec TabieModel::predict (const In& x) const
{
    const juce::ScopedLock sl (lock);
    std::array<float, kHid> h;
    Vec o;
    forward (W, x, h, o);
    return o;
}

void TabieModel::addFeedback (const In& x, const Vec& used, bool like)
{
    const juce::ScopedLock sl (lock);
    if (! ready) return;

    Sample s;
    s.x = x;
    s.w = like ? 1.f : 0.5f;
    for (int j = 0; j < kOut; ++j)
    {
        const float y = used[(size_t) j];
        // like: acercarse a lo que gusto; dislike: alejarse un poco hacia el lado opuesto
        s.t[(size_t) j] = like ? y : juce::jlimit (0.f, 1.f, y + 0.3f * (1.f - 2.f * y));
    }
    replay.push_back (s);
    if (replay.size() > 300) replay.erase (replay.begin());

    // entrenamiento corto: votos recientes + muestras del prior (para no olvidar lo basico)
    std::vector<Sample> data;
    const size_t from = replay.size() > 48 ? replay.size() - 48 : 0;
    for (size_t i = from; i < replay.size(); ++i) { data.push_back (replay[i]); data.push_back (replay[i]); }
    for (int i = 0; i < 96; ++i)
    {
        float f[6]; randomFeat (rng, f);
        const int g = rng.nextInt (kGenres), st = rng.nextInt (3);
        data.push_back ({ makeInput (g, st, f), prior (g, st, f), 1.f });
    }
    train (data, 20, 0.05f);
    save();
}

int TabieModel::numFeedback() const
{
    const juce::ScopedLock sl (lock);
    return (int) replay.size();
}

void TabieModel::resetToPrior()
{
    const juce::ScopedLock sl (lock);
    replay.clear();
    initWeights();
    bootstrap();
    ready = true;
    save();
}

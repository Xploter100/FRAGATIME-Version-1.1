#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FragatimeAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto addF = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (lo, hi, 0.1f), def));
    };

    addF ("intensity", "Intensity", 0.f, 100.f, 70.f);
    addF ("density",   "Density",   0.f, 100.f, 45.f);
    addF ("size",      "Size",      0.f, 100.f, 45.f);
    addF ("pitch",     "Pitch",     0.f, 100.f, 50.f);    // 50 = sin cambio, +-12 semitonos
    addF ("arp",       "Arp",       0.f, 100.f, 0.f);
    addF ("grain",     "Grain",     0.f, 100.f, 35.f);
    addF ("feedback",  "Feedback",  0.f, 100.f, 0.f);
    addF ("position",  "Position",  0.f, 100.f, 0.f);
    addF ("scan",      "Scan",   -100.f, 100.f, 0.f);
    addF ("reverb",    "Reverb",    0.f, 100.f, 15.f);
    addF ("delay",     "Delay",     0.f, 100.f, 10.f);
    addF ("crush",     "Crush",     0.f, 100.f, 0.f);
    addF ("filter",    "Filter", -100.f, 100.f, 0.f);     // negativo = low-pass, positivo = high-pass
    addF ("drive",     "Drive",     0.f, 100.f, 0.f);
    addF ("out",       "Output",  -24.f,   6.f, 0.f);

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "source", 1 }, "Source", StringArray { "FILE", "LIVE" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dir", 1 }, "Direction", StringArray { "FWD", "REV", "RND" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "sync", 1 }, "Sync", false));
    return l;
}

FragatimeAudioProcessor::FragatimeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& g : grainPos) g.store (-1.f);
}

FragatimeAudioProcessor::~FragatimeAudioProcessor()
{
    loader.stopThread (4000);
    exporter.stopThread (6000);
}

bool FragatimeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto o = layouts.getMainOutputChannelSet();
    if (o != juce::AudioChannelSet::mono() && o != juce::AudioChannelSet::stereo()) return false;
    return o == layouts.getMainInputChannelSet();
}

//==============================================================================
GrainParams FragatimeAudioProcessor::readParams() const
{
    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    GrainParams g;
    g.density  = P ("density") * 0.01f;
    g.size     = P ("size") * 0.01f;
    g.pitchPct = P ("pitch");
    g.arp      = P ("arp") * 0.01f;
    g.grain    = P ("grain") * 0.01f;
    g.feedback = P ("feedback") * 0.01f;
    g.position = P ("position") * 0.01f;
    g.scan     = P ("scan") * 0.01f;
    g.reverb   = P ("reverb") * 0.01f;
    g.delay    = P ("delay") * 0.01f;
    g.crush    = P ("crush") * 0.01f;
    g.filter   = P ("filter") * 0.01f;
    g.drive    = P ("drive") * 0.01f;
    g.outDb    = P ("out");
    g.source   = (int) std::round (P ("source"));
    g.dir      = (int) std::round (P ("dir"));
    g.sync     = P ("sync") > 0.5f;
    g.bpm      = getEffectiveBpm();
    return g;
}

double FragatimeAudioProcessor::getEffectiveBpm() const
{
    const double h = (double) hostBpm.load();
    if (h > 0.0) return h;
    const double d = getDetectedBpm();
    if (d > 0.0) return d;
    return 120.0;
}

void FragatimeAudioProcessor::setBpmMultiplier (float m)
{
    bpmMul.store (m);
    apvts.state.setProperty ("bpmMul", (double) m, nullptr);
}

AnalysisResult FragatimeAudioProcessor::getAnalysis() const
{
    const juce::SpinLock::ScopedLockType sl (fileLock);
    return analysis;
}

bool FragatimeAudioProcessor::hasFile() const
{
    const juce::SpinLock::ScopedLockType sl (fileLock);
    return fileBuf != nullptr;
}

//==============================================================================
// Carga de archivos (hilo aparte, nunca bloquea el audio)
void FragatimeAudioProcessor::loadFile (const juce::File& f, bool keepBpmMul)
{
    loader.stopThread (4000);
    if (! keepBpmMul) setBpmMultiplier (1.f);
    { const juce::ScopedLock sl (pendingLock); pendingFile = f; }
    apvts.state.setProperty ("file", f.getFullPathName(), nullptr);
    loader.startThread();
}

void FragatimeAudioProcessor::doLoad()
{
    loading = true;
    juce::File f;
    { const juce::ScopedLock sl (pendingLock); f = pendingFile; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));

    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        const juce::SpinLock::ScopedLockType sl (fileLock);
        statusText = "Could not open: " + f.getFileName();
        loading = false;
        return;
    }

    const juce::int64 maxFrames = (juce::int64) (reader->sampleRate * kMaxSeconds);
    const int n = (int) juce::jmin (reader->lengthInSamples, maxFrames);
    auto buf = std::make_shared<juce::AudioBuffer<float>> (2, n);
    reader->read (buf.get(), 0, n, 0, true, true);
    if (reader->numChannels == 1) buf->copyFrom (1, 0, *buf, 0, 0, n);

    // miniatura de la forma de onda (min / max por columna)
    std::vector<float> pk ((size_t) kPeaks * 2, 0.f);
    const float* a = buf->getReadPointer (0);
    const float* b = buf->getReadPointer (1);
    for (int c = 0; c < kPeaks; ++c)
    {
        const int s0 = (int) ((juce::int64) c * n / kPeaks);
        const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (c + 1) * n / kPeaks));
        float mn = 0.f, mx = 0.f;
        const int step = juce::jmax (1, (s1 - s0) / 64);
        for (int i = s0; i < s1 && i < n; i += step)
        {
            const float v = 0.5f * (a[i] + b[i]);
            mn = juce::jmin (mn, v); mx = juce::jmax (mx, v);
        }
        pk[(size_t) c * 2] = mn; pk[(size_t) c * 2 + 1] = mx;
    }

    // BPM + rasgos para Tabie
    AnalysisResult an = analyzeAudio (*buf, reader->sampleRate);

    const double secs = (double) n / reader->sampleRate;
    juce::String st = f.getFileName() + "   " + juce::String ((int) secs / 60) + ":"
                      + juce::String ((int) secs % 60).paddedLeft ('0', 2) + "   "
                      + juce::String (reader->sampleRate / 1000.0, 1) + " kHz";
    if (an.valid) st += "   ~" + juce::String ((int) std::round (an.bpm)) + " BPM";
    if (reader->lengthInSamples > maxFrames) st += "   (first 4:00 only)";

    {
        const juce::SpinLock::ScopedLockType sl (fileLock);
        fileBuf = buf;
        peaks = std::move (pk);
        fileRate = reader->sampleRate;
        analysis = an;
        statusText = st;
    }
    detectedBpm.store (an.valid ? (float) an.bpm : 0.f);
    ++fileVersion;
    loading = false;
}

juce::String FragatimeAudioProcessor::getStatus() const
{
    if (loading.load()) return "Loading...";
    const juce::SpinLock::ScopedLockType sl (fileLock);
    return statusText;
}

void FragatimeAudioProcessor::getWaveform (std::vector<float>& out) const
{
    const juce::SpinLock::ScopedLockType sl (fileLock);
    out = peaks;
}

//==============================================================================
// Exportacion: renderiza el efecto sin conexion (mas rapido que tiempo real) a un WAV de 24 bits
void FragatimeAudioProcessor::startExport (int bars, const juce::File& target)
{
    if (exporter.isThreadRunning()) return;
    exportBars = juce::jlimit (1, 16, bars);
    exportTarget = target;
    exportParams = readParams();
    exportFs = fs;
    exportStatus = 1;
    exporter.startThread();
}

juce::File FragatimeAudioProcessor::getExportFile() const
{
    const juce::ScopedLock sl (exportLock);
    return exportResult;
}

juce::String FragatimeAudioProcessor::getExportMessage() const
{
    const juce::ScopedLock sl (exportLock);
    return exportMsg;
}

void FragatimeAudioProcessor::finishExport (int status, const juce::File& f, const juce::String& msg)
{
    {
        const juce::ScopedLock sl (exportLock);
        exportResult = f;
        exportMsg = msg;
    }
    exportStatus = status;
    ++exportSerial;
}

void FragatimeAudioProcessor::doExport()
{
    std::shared_ptr<const juce::AudioBuffer<float>> fb;
    double rate = 44100.0;
    {
        const juce::SpinLock::ScopedLockType sl (fileLock);
        fb = fileBuf;
        rate = fileRate;
    }
    if (fb == nullptr || fb->getNumSamples() < 16)
    {
        finishExport (3, {}, "Load an audio file first (export needs FILE mode)");
        return;
    }

    GrainParams gp = exportParams;
    gp.source = 0;
    const double sr = exportFs;
    const double bpm = juce::jlimit (30.0, 300.0, gp.bpm);
    gp.bpm = bpm;

    const int bodyN = (int) ((double) exportBars * 4.0 * 60.0 / bpm * sr);
    const int tailN = (int) (1.0 * sr);
    const int total = bodyN + tailN;

    juce::AudioBuffer<float> out (2, total);
    out.clear();

    auto eng = std::make_unique<GranularEngine>();
    eng->prepare (sr);

    FileView fv;
    fv.L = fb->getReadPointer (0);
    fv.R = fb->getReadPointer (1);
    fv.N = fb->getNumSamples();
    fv.rate = rate;

    std::vector<float> wl (1024), wr (1024);
    double ppqPos = 0.0;
    int pos = 0;
    while (pos < total)
    {
        if (exporter.threadShouldExit()) { finishExport (3, {}, "Export cancelled"); return; }
        const int nn = juce::jmin (1024, total - pos);
        gp.spawn = pos < bodyN;
        eng->process (nullptr, nullptr, wl.data(), wr.data(), nn, gp, fv, ppqPos, true);
        out.copyFrom (0, pos, wl.data(), nn);
        out.copyFrom (1, pos, wr.data(), nn);
        ppqPos += (double) nn * bpm / 60.0 / sr;
        pos += nn;
    }

    // fundido de la cola + normalizar si hace falta
    const int fade = (int) (0.3 * sr);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = out.getWritePointer (ch);
        for (int i = 0; i < fade && i < total; ++i)
            d[total - 1 - i] *= (float) i / (float) fade;
    }
    const float peak = out.getMagnitude (0, total);
    if (peak > 0.89f) out.applyGain (0.89f / peak);

    exportTarget.getParentDirectory().createDirectory();
    exportTarget.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (exportTarget),
                                                                          sr, 2, 24, {}, 0));
    if (writer == nullptr)
    {
        finishExport (3, {}, "Could not write: " + exportTarget.getFullPathName());
        return;
    }
    writer->writeFromAudioSampleBuffer (out, 0, total);
    writer.reset();

    finishExport (2, exportTarget, "Exported: " + exportTarget.getFileName());
}

//==============================================================================
void FragatimeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    engine.prepare (sampleRate);
    const size_t nb = (size_t) juce::jmax (samplesPerBlock, 512);
    wetL.assign (nb, 0.f);
    wetR.assign (nb, 0.f);
    intSm = apvts.getRawParameterValue ("intensity")->load() * 0.01f;
    seenVersion = -1;           // fuerza a recoger el archivo ya cargado
    prepared = true;
}

void FragatimeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n == 0 || nch == 0 || ! prepared) return;

    if ((int) wetL.size() < n) { wetL.resize ((size_t) n); wetR.resize ((size_t) n); }

    // recoger el archivo nuevo (sin bloquear: si el hilo de carga esta escribiendo, se reintenta)
    const int ver = fileVersion.load();
    if (ver != seenVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (fileLock);
        if (tl.isLocked()) { activeBuf = fileBuf; activeRate = fileRate; seenVersion = ver; engine.resetFile(); }
    }
    FileView fv;
    if (activeBuf != nullptr && activeBuf->getNumSamples() > 16)
    {
        fv.L = activeBuf->getReadPointer (0);
        fv.R = activeBuf->getReadPointer (1);
        fv.N = activeBuf->getNumSamples();
        fv.rate = activeRate;
    }

    // tempo y posicion del host
    double ppq = 0.0, hb = -1.0;
    bool ppqValid = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) hb = *b;
            if (pos->getIsPlaying())
                if (auto pp = pos->getPpqPosition()) { ppq = *pp; ppqValid = true; }
        }
    if (hb > 0.0) hostBpm.store ((float) hb);

    const GrainParams gp = readParams();
    const float* inL = buffer.getReadPointer (0);
    const float* inR = nch > 1 ? buffer.getReadPointer (1) : inL;

    engine.process (inL, inR, wetL.data(), wetR.data(), n, gp, fv, ppq, ppqValid);

    // mezcla final (Intensity)
    float* outL = buffer.getWritePointer (0);
    float* outR = nch > 1 ? buffer.getWritePointer (1) : nullptr;
    const float target = apvts.getRawParameterValue ("intensity")->load() * 0.01f;
    for (int i = 0; i < n; ++i)
    {
        intSm += (target - intSm) * 0.002f;
        const float dl = inL[i], dr = inR[i];
        outL[i] = dl * (1.f - intSm) + wetL[(size_t) i] * intSm;
        if (outR != nullptr) outR[i] = dr * (1.f - intSm) + wetR[(size_t) i] * intSm;
    }

    engine.fillDisplay (grainPos);
    centerNorm.store (engine.getCenterNorm());
}

//==============================================================================
juce::AudioProcessorEditor* FragatimeAudioProcessor::createEditor()
{
    return new FragatimeAudioProcessorEditor (*this);
}

void FragatimeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void FragatimeAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const juce::String path = apvts.state.getProperty ("file").toString();
            if (path.isNotEmpty())
            {
                const juce::File f (path);
                if (f.existsAsFile()) loadFile (f, true);
            }
            // el multiplicador de BPM se aplica despues de que termine la carga
            bpmMul.store ((float) (double) apvts.state.getProperty ("bpmMul", 1.0));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FragatimeAudioProcessor();
}

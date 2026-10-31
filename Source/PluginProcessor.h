#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>
#include "GranularEngine.h"
#include "Analysis.h"
#include "TabieModel.h"

class FragatimeAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int    kPeaks      = 1024;    // columnas de la forma de onda
    static constexpr double kMaxSeconds = 240.0;   // maximo 4 min de audio cargado (~90 MB)

    FragatimeAudioProcessor();
    ~FragatimeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FRAGATIME"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // ---- archivo de audio ----
    void loadFile (const juce::File& f, bool keepBpmMul = false);
    bool isLoading() const { return loading.load(); }
    bool hasFile() const;
    juce::String getStatus() const;
    void getWaveform (std::vector<float>& out) const;
    std::atomic<int> fileVersion { 0 };

    // ---- Tabie ----
    AnalysisResult getAnalysis() const;
    TabieModel& getTabie() { return tabie; }
    void setBpmMultiplier (float m);
    float getBpmMultiplier() const { return bpmMul.load(); }
    double getDetectedBpm() const { return (double) detectedBpm.load() * (double) bpmMul.load(); }
    double getHostBpm() const { return (double) hostBpm.load(); }
    double getEffectiveBpm() const;       // tempo del host si existe; si no, el detectado; si no, 120

    // ---- exportar ----
    GrainParams readParams() const;
    void startExport (int bars, const juce::File& target);
    int  getExportStatus() const { return exportStatus.load(); }     // 0 libre, 1 renderizando, 2 listo, 3 error
    int  getExportSerial() const { return exportSerial.load(); }
    juce::File getExportFile() const;
    juce::String getExportMessage() const;

    // ---- datos para la interfaz ----
    std::array<std::atomic<float>, 32> grainPos;
    std::atomic<float> centerNorm { 0.f };

private:
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    struct Loader : public juce::Thread
    {
        explicit Loader (FragatimeAudioProcessor& p) : juce::Thread ("FRAGATIME loader"), proc (p) {}
        void run() override { proc.doLoad(); }
        FragatimeAudioProcessor& proc;
    };
    struct Exporter : public juce::Thread
    {
        explicit Exporter (FragatimeAudioProcessor& p) : juce::Thread ("FRAGATIME export"), proc (p) {}
        void run() override { proc.doExport(); }
        FragatimeAudioProcessor& proc;
    };

    void doLoad();
    void doExport();
    void finishExport (int status, const juce::File& f, const juce::String& msg);

    // archivo
    Loader loader { *this };
    juce::File pendingFile;
    mutable juce::CriticalSection pendingLock;
    mutable juce::SpinLock fileLock;
    std::shared_ptr<const juce::AudioBuffer<float>> fileBuf;
    std::vector<float> peaks;
    double fileRate = 44100.0;
    AnalysisResult analysis;
    juce::String statusText { "No file loaded - using live input" };
    std::atomic<bool> loading { false };
    std::atomic<float> detectedBpm { 0.f }, hostBpm { -1.f }, bpmMul { 1.f };

    std::shared_ptr<const juce::AudioBuffer<float>> activeBuf;   // hilo de audio
    double activeRate = 44100.0;
    int seenVersion = 0;

    // motor
    double fs = 44100.0;
    bool prepared = false;
    GranularEngine engine;
    std::vector<float> wetL, wetR;
    float intSm = 0.f;

    // Tabie
    TabieModel tabie;

    // exportacion
    Exporter exporter { *this };
    int exportBars = 4;
    juce::File exportTarget;
    GrainParams exportParams;
    double exportFs = 44100.0;
    std::atomic<int> exportStatus { 0 }, exportSerial { 0 };
    mutable juce::CriticalSection exportLock;
    juce::File exportResult;
    juce::String exportMsg;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FragatimeAudioProcessor)
};

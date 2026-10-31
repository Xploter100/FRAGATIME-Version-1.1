#pragma once
#include "PluginProcessor.h"

// Perillas monocromaticas. Si el rango es bipolar (o tiene la propiedad "center"), el arco parte del centro.
struct FLAF : public juce::LookAndFeel_V4
{
    juce::Colour fg { juce::Colours::white }, track { 0xff333333 };

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (12.f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = startAngle + pos * (endAngle - startAngle);

        const bool bipolar = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0);
        const bool centered = bipolar || (bool) s.getProperties().getWithDefault ("center", false);
        const float zero = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.5f;
        const float zeroAng = startAngle + zero * (endAngle - startAngle);

        const float thick = r > 40.f ? 3.f : 2.f;
        juce::Path trackP, valP, ptr;
        trackP.addCentredArc (c.x, c.y, r, r, 0.f, startAngle, endAngle, true);
        if (centered) valP.addCentredArc (c.x, c.y, r, r, 0.f, juce::jmin (zeroAng, ang), juce::jmax (zeroAng, ang), true);
        else          valP.addCentredArc (c.x, c.y, r, r, 0.f, startAngle, ang, true);

        g.setColour (track);
        g.strokePath (trackP, juce::PathStrokeType (thick));
        g.setColour (fg);
        g.strokePath (valP, juce::PathStrokeType (thick));

        ptr.startNewSubPath (0.f, -r * 0.45f);
        ptr.lineTo (0.f, -r * 0.8f);
        g.strokePath (ptr, juce::PathStrokeType (thick), juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }
};

// Zona desde donde se arrastra el WAV exportado hacia el playlist de FL Studio
class DragPad : public juce::Component
{
public:
    std::function<void()> onDragStart;
    juce::Colour fg { juce::Colours::white }, bg { juce::Colours::black };

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (fg.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 5.f);
        g.setColour (fg);
        g.drawRoundedRectangle (r, 5.f, 1.2f);
        g.setFont (11.f);
        g.drawText ("DRAG TO FL", getLocalBounds(), juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 5 && onDragStart) onDragStart();
    }
    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::DraggingHandCursor; }
};

class FragatimeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public juce::FileDragAndDropTarget,
                                      public juce::DragAndDropContainer,
                                      private juce::Timer
{
public:
    explicit FragatimeAudioProcessorEditor (FragatimeAudioProcessor&);
    ~FragatimeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    struct Pal { juce::Colour bg, fg, dim, grid; };

    // una de las tres propuestas de Tabie
    struct Option
    {
        bool valid = false;
        TabieModel::In input {};
        TabieModel::Vec vec {};
        float position = 0.f;
    };

    Pal  pal() const;
    bool isDark() const;
    void applyTheme();
    void setParam (const juce::String& id, float value);
    void cycleChoice (const char* id, int numItems);
    void setPositionFromMouse (const juce::MouseEvent& e);
    void timerCallback() override;

    // Tabie
    void generateOptions();
    void applyOption (int i);
    void vote (int i, bool like);
    TabieModel::Vec currentKnobsVec() const;
    juce::String describe (const Option& o) const;
    juce::String describeFx (const Option& o) const;
    int  genreIndex() const;

    // Export
    void showExportMenu();
    void startExportTo (const juce::File& target, bool forDrag);
    juce::String makeExportName() const;
    int  exportBars() const;

    FragatimeAudioProcessor& proc;
    FLAF laf;

    std::array<juce::Slider, 15> knobs;
    std::array<std::unique_ptr<SliderAtt>, 15> atts;
    juce::TextButton loadBtn, exportBtn, srcBtn, dirBtn, syncBtn, themeBtn;
    juce::TextButton tabieBtn, resetBtn;
    juce::ComboBox genreBox;
    std::array<juce::TextButton, 3> applyBtn, likeBtn, dislikeBtn;
    DragPad dragPad;
    std::unique_ptr<juce::FileChooser> chooser;

    std::array<Option, 3> options;
    int appliedOpt = -1;
    juce::Random jitter;

    std::vector<float> wave;
    int lastVersion = -1, lastExportSerial = 0;
    bool lastDark = true;
    bool exportForDrag = false;
    juce::File dragFile;
    juce::String message;
    juce::Rectangle<int> waveArea, bpmArea;
    std::array<juce::Rectangle<int>, 3> cards;
    std::atomic<float>* srcP = nullptr;
    std::atomic<float>* dirP = nullptr;
    std::atomic<float>* syncP = nullptr;
};

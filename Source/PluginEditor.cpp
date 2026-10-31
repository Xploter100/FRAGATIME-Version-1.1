#include "PluginEditor.h"

using namespace juce;

static const char* kIds[15]    { "intensity", "density", "size", "pitch", "arp", "grain", "feedback", "position",
                                 "scan", "reverb", "delay", "crush", "filter", "drive", "out" };
static const char* kLabels[15] { "INTENSITY", "DENSITY", "SIZE", "PITCH", "ARP", "GRAIN", "FEEDBACK", "POSITION",
                                 "SCAN", "REVERB", "DELAY", "CRUSH", "FILTER", "DRIVE", "OUTPUT" };
static const char* kOptNames[3] { "A   TIGHT", "B   GROOVE", "C   WIDE" };

static String fmtValue (int idx, double v, bool sync)
{
    switch (idx)
    {
        case 1:   // density: en SYNC es una subdivision del pulso
            if (sync) return String (SyncTables::kDivNames[SyncTables::index ((float) v * 0.01f)]);
            return String (v, 0) + "%";
        case 2:   // size: en SYNC es una duracion musical
            if (sync) return String (SyncTables::kLenNames[SyncTables::index ((float) v * 0.01f)]);
            return String (v, 0) + "%";
        case 3:   // pitch: 50% = sin cambio, +-12 semitonos
        {
            const double st = (v - 50.0) / 50.0 * 12.0;
            return String (v, 0) + "%  " + (st >= 0 ? "+" : "") + String (st, 1) + "st";
        }
        case 12:  // filtro bipolar
            if (v < -1.0) return "LP " + String (-v, 0) + "%";
            if (v > 1.0)  return "HP " + String (v, 0) + "%";
            return "OFF";
        case 14:  return String (v, 1) + " dB";
        default:  return String (v, 0) + "%";
    }
}

//==============================================================================
FragatimeAudioProcessorEditor::FragatimeAudioProcessorEditor (FragatimeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    srcP  = proc.apvts.getRawParameterValue ("source");
    dirP  = proc.apvts.getRawParameterValue ("dir");
    syncP = proc.apvts.getRawParameterValue ("sync");

    for (size_t i = 0; i < 15; ++i)
    {
        auto& s = knobs[i];
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::TextBoxBelow, false, 92, 16);
        if (i == 3) s.getProperties().set ("center", true);
        addAndMakeVisible (s);
        atts[i] = std::make_unique<SliderAtt> (proc.apvts, kIds[i], s);

        const int idx = (int) i;
        s.textFromValueFunction = [this, idx] (double v) { return fmtValue (idx, v, syncP->load() > 0.5f); };
        s.updateText();
    }

    // --- barra superior ---
    loadBtn.setButtonText ("LOAD AUDIO");
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Select an audio file (wav / mp3)", File(),
                                                 "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) proc.loadFile (f);
                              });
    };
    addAndMakeVisible (loadBtn);

    exportBtn.setButtonText ("EXPORT");
    exportBtn.onClick = [this] { showExportMenu(); };
    addAndMakeVisible (exportBtn);

    dragPad.onDragStart = [this]
    {
        if (dragFile.existsAsFile())
            DragAndDropContainer::performExternalDragDropOfFiles (StringArray (dragFile.getFullPathName()), false, &dragPad);
    };
    addChildComponent (dragPad);

    srcBtn.onClick  = [this] { cycleChoice ("source", 2); };
    dirBtn.onClick  = [this] { cycleChoice ("dir", 3); };
    syncBtn.setButtonText ("SYNC");
    syncBtn.onClick = [this] { setParam ("sync", syncP->load() > 0.5f ? 0.f : 1.f); };
    addAndMakeVisible (srcBtn);
    addAndMakeVisible (dirBtn);
    addAndMakeVisible (syncBtn);

    themeBtn.onClick = [this]
    {
        proc.apvts.state.setProperty ("dark", ! isDark(), nullptr);
        applyTheme();
    };
    addAndMakeVisible (themeBtn);

    // --- Tabie ---
    tabieBtn.setButtonText ("TABIE");
    tabieBtn.onClick = [this] { generateOptions(); };
    addAndMakeVisible (tabieBtn);

    for (int g = 0; g < TabieModel::kGenres; ++g) genreBox.addItem (TabieModel::genreName (g), g + 1);
    genreBox.setSelectedId (1 + jlimit (0, TabieModel::kGenres - 1, (int) proc.apvts.state.getProperty ("genre", 0)),
                            dontSendNotification);
    genreBox.onChange = [this] { proc.apvts.state.setProperty ("genre", genreBox.getSelectedId() - 1, nullptr); };
    addAndMakeVisible (genreBox);

    resetBtn.setButtonText ("RESET");
    resetBtn.onClick = [this]
    {
        Component::SafePointer<FragatimeAudioProcessorEditor> sp (this);
        AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Reset Tabie",
                                      "Forget everything Tabie learned from your likes and dislikes?",
                                      "RESET", "CANCEL", this,
                                      ModalCallbackFunction::create ([sp] (int r)
                                      {
                                          if (sp == nullptr || r != 1) return;
                                          sp->proc.getTabie().resetToPrior();
                                          for (auto& o : sp->options) o.valid = false;
                                          sp->appliedOpt = -1;
                                          for (size_t i = 0; i < 3; ++i)
                                          {
                                              sp->applyBtn[i].setEnabled (false);
                                              sp->likeBtn[i].setEnabled (false);
                                              sp->dislikeBtn[i].setEnabled (false);
                                          }
                                          sp->message = "Tabie reset to its original knowledge";
                                      }));
    };
    addAndMakeVisible (resetBtn);

    for (int i = 0; i < 3; ++i)
    {
        const size_t k = (size_t) i;
        applyBtn[k].setButtonText ("APPLY");
        likeBtn[k].setButtonText ("LIKE");
        dislikeBtn[k].setButtonText ("DISLIKE");
        applyBtn[k].onClick   = [this, i] { applyOption (i); };
        likeBtn[k].onClick    = [this, i] { vote (i, true); };
        dislikeBtn[k].onClick = [this, i] { vote (i, false); };
        applyBtn[k].setEnabled (false);
        likeBtn[k].setEnabled (false);
        dislikeBtn[k].setEnabled (false);
        addAndMakeVisible (applyBtn[k]);
        addAndMakeVisible (likeBtn[k]);
        addAndMakeVisible (dislikeBtn[k]);
    }

    applyTheme();
    setSize (960, 710);
    startTimerHz (30);
}

FragatimeAudioProcessorEditor::~FragatimeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
bool FragatimeAudioProcessorEditor::isDark() const
{
    return (bool) proc.apvts.state.getProperty ("dark", true);
}

FragatimeAudioProcessorEditor::Pal FragatimeAudioProcessorEditor::pal() const
{
    if (isDark()) return { Colour (0xff0c0c0c), Colours::white, Colour (0xff7a7a7a), Colour (0xff1b1b1b) };
    return { Colour (0xfff2f2f0), Colour (0xff111111), Colour (0xff808080), Colour (0xffe2e2df) };
}

void FragatimeAudioProcessorEditor::applyTheme()
{
    const auto p = pal();
    laf.fg = p.fg;
    laf.track = p.dim.withAlpha (0.35f);

    laf.setColour (Slider::textBoxTextColourId, p.fg);
    laf.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    laf.setColour (ComboBox::backgroundColourId, p.bg);
    laf.setColour (ComboBox::textColourId, p.fg);
    laf.setColour (ComboBox::outlineColourId, p.dim);
    laf.setColour (ComboBox::arrowColourId, p.fg);
    laf.setColour (PopupMenu::backgroundColourId, p.bg);
    laf.setColour (PopupMenu::textColourId, p.fg);
    laf.setColour (PopupMenu::highlightedBackgroundColourId, p.fg);
    laf.setColour (PopupMenu::highlightedTextColourId, p.bg);
    laf.setColour (TextButton::buttonColourId, p.bg);
    laf.setColour (TextButton::buttonOnColourId, p.fg);
    laf.setColour (TextButton::textColourOffId, p.fg);
    laf.setColour (TextButton::textColourOnId, p.bg);
    laf.setColour (Label::textColourId, p.fg);

    dragPad.fg = p.fg;
    dragPad.bg = p.bg;
    themeBtn.setButtonText (isDark() ? "LIGHT" : "DARK");
    lastDark = isDark();
    sendLookAndFeelChange();
    repaint();
}

void FragatimeAudioProcessorEditor::setParam (const String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void FragatimeAudioProcessorEditor::cycleChoice (const char* id, int numItems)
{
    const int cur = (int) std::round (proc.apvts.getRawParameterValue (id)->load());
    setParam (id, (float) ((cur + 1) % numItems));
}

int FragatimeAudioProcessorEditor::genreIndex() const
{
    return jlimit (0, TabieModel::kGenres - 1, genreBox.getSelectedId() - 1);
}

//==============================================================================
// TABIE: genero + BPM detectado -> tres opciones de fragmentacion (red neuronal local)
void FragatimeAudioProcessorEditor::generateOptions()
{
    auto& tab = proc.getTabie();
    tab.ensureReady();                         // la primera vez entrena el modelo base (< 1 s)

    const auto an = proc.getAnalysis();
    float feat[6] { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    if (an.valid)
    {
        for (int i = 0; i < 5; ++i) feat[i] = an.feat[i];
        feat[5] = jlimit (0.f, 1.f, (float) ((proc.getDetectedBpm() - 60.0) / 120.0));
    }
    else feat[5] = jlimit (0.f, 1.f, (float) ((proc.getEffectiveBpm() - 60.0) / 120.0));

    const int genre = genreIndex();
    for (int s = 0; s < 3; ++s)
    {
        Option& o = options[(size_t) s];
        o.input = TabieModel::makeInput (genre, s, feat);
        o.vec = tab.predict (o.input);
        for (auto& v : o.vec) v = jlimit (0.f, 1.f, v + (jitter.nextFloat() - 0.5f) * 0.08f);   // variedad entre pulsaciones
        o.position = an.valid ? an.segStart[s] : 0.f;
        o.valid = true;

        applyBtn[(size_t) s].setEnabled (true);
        likeBtn[(size_t) s].setEnabled (true);
        dislikeBtn[(size_t) s].setEnabled (true);
    }
    appliedOpt = -1;
    message = String ("Tabie: 3 options for ") + TabieModel::genreName (genre) + " - press APPLY, then LIKE or DISLIKE";
}

void FragatimeAudioProcessorEditor::applyOption (int i)
{
    const auto& o = options[(size_t) i];
    if (! o.valid) return;
    const auto& v = o.vec;

    setParam ("density",   v[0] * 100.f);
    setParam ("size",      v[1] * 100.f);
    setParam ("pitch",     v[2] * 100.f);
    setParam ("arp",       v[3] * 100.f);
    setParam ("grain",     v[4] * 100.f);
    setParam ("feedback",  v[5] * 100.f);
    setParam ("scan",      (v[6] * 2.f - 1.f) * 100.f);
    setParam ("reverb",    v[7] * 100.f);
    setParam ("delay",     v[8] * 100.f);
    setParam ("crush",     v[9] * 100.f);
    setParam ("filter",    (v[10] * 2.f - 1.f) * 100.f);
    setParam ("drive",     v[11] * 100.f);
    setParam ("dir",       v[12] < 0.6f ? 0.f : (v[12] < 0.85f ? 2.f : 1.f));
    setParam ("sync",      v[13] > 0.5f ? 1.f : 0.f);
    setParam ("intensity", v[14] * 100.f);
    setParam ("position",  o.position * 100.f);
    if (proc.hasFile()) setParam ("source", 0.f);

    appliedOpt = i;
    message = String ("Option ") + kOptNames[i] + " applied - tweak the knobs if you like, then LIKE it";
}

TabieModel::Vec FragatimeAudioProcessorEditor::currentKnobsVec() const
{
    auto P = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    TabieModel::Vec v {};
    v[0]  = P ("density") * 0.01f;
    v[1]  = P ("size") * 0.01f;
    v[2]  = P ("pitch") * 0.01f;
    v[3]  = P ("arp") * 0.01f;
    v[4]  = P ("grain") * 0.01f;
    v[5]  = P ("feedback") * 0.01f;
    v[6]  = (P ("scan") * 0.01f + 1.f) * 0.5f;
    v[7]  = P ("reverb") * 0.01f;
    v[8]  = P ("delay") * 0.01f;
    v[9]  = P ("crush") * 0.01f;
    v[10] = (P ("filter") * 0.01f + 1.f) * 0.5f;
    v[11] = P ("drive") * 0.01f;
    const int d = (int) std::round (P ("dir"));
    v[12] = d == 0 ? 0.2f : (d == 2 ? 0.72f : 0.95f);
    v[13] = P ("sync") > 0.5f ? 0.9f : 0.1f;
    v[14] = P ("intensity") * 0.01f;
    for (auto& e : v) e = jlimit (0.f, 1.f, e);
    return v;
}

void FragatimeAudioProcessorEditor::vote (int i, bool like)
{
    auto& o = options[(size_t) i];
    if (! o.valid) return;

    // LIKE sobre la opcion aplicada aprende tambien tus ajustes manuales posteriores
    TabieModel::Vec v = (like && appliedOpt == i) ? currentKnobsVec() : o.vec;
    proc.getTabie().addFeedback (o.input, v, like);

    message = String (like ? "Liked " : "Disliked ") + kOptNames[i] + " - Tabie learned (" + String (proc.getTabie().numFeedback()) + " votes)";
}

String FragatimeAudioProcessorEditor::describe (const Option& o) const
{
    const auto& v = o.vec;
    const bool sync = v[13] > 0.5f;
    String s = sync ? String (SyncTables::kDivNames[SyncTables::index (v[0])]) + " grid  -  "
                      + SyncTables::kLenNames[SyncTables::index (v[1])] + " grains"
                    : "free  -  " + String ((int) std::round (v[0] * 100.f)) + "% dens  "
                      + String ((int) std::round (v[1] * 100.f)) + "% size";
    return s;
}

String FragatimeAudioProcessorEditor::describeFx (const Option& o) const
{
    const auto& v = o.vec;
    const float st = (v[2] - 0.5f) * 24.f;
    return "arp " + String ((int) std::round (v[3] * 100.f)) + "%   fb " + String ((int) std::round (v[5] * 100.f))
           + "%   pitch " + (st >= 0 ? "+" : "") + String (st, 1) + "st";
}

//==============================================================================
// EXPORT: renderiza a WAV 24 bits (guardar en la PC, o arrastrar al playlist de FL Studio)
int FragatimeAudioProcessorEditor::exportBars() const
{
    return jlimit (1, 16, (int) proc.apvts.state.getProperty ("exportBars", 4));
}

String FragatimeAudioProcessorEditor::makeExportName() const
{
    const String mode = appliedOpt >= 0 ? String (TabieModel::genreName (genreIndex())) : String ("MANUAL");
    return "FRAGATIME_" + mode + "_" + String ((int) std::round (proc.getEffectiveBpm())) + "bpm_"
           + Time::getCurrentTime().formatted ("%H%M%S") + ".wav";
}

void FragatimeAudioProcessorEditor::showExportMenu()
{
    PopupMenu m, bars;
    const int cur = exportBars();
    for (int b : { 1, 2, 4, 8 }) bars.addItem (100 + b, String (b) + (b == 1 ? " bar" : " bars"), true, b == cur);
    m.addSubMenu ("Length", bars);
    m.addSeparator();
    m.addItem (1, "Save WAV to computer...");
    m.addItem (2, "Send to FL Studio (drag to playlist)");

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&exportBtn), [this] (int r)
    {
        if (r >= 101 && r <= 108)
        {
            proc.apvts.state.setProperty ("exportBars", r - 100, nullptr);
            message = "Export length: " + String (r - 100) + (r == 101 ? " bar" : " bars");
        }
        else if (r == 1)
        {
            chooser = std::make_unique<FileChooser> ("Save WAV",
                          File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (makeExportName()), "*.wav");
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                                      | FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f != File()) startExportTo (f.withFileExtension ("wav"), false);
                                  });
        }
        else if (r == 2)
        {
            const auto dir = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("FRAGATIME Exports");
            startExportTo (dir.getChildFile (makeExportName()), true);
        }
    });
}

void FragatimeAudioProcessorEditor::startExportTo (const File& target, bool forDrag)
{
    if (! proc.hasFile()) { message = "Load an audio file first - export renders the loaded audio"; return; }
    if (proc.getExportStatus() == 1) { message = "Still rendering, please wait..."; return; }

    exportForDrag = forDrag;
    dragFile = File();
    dragPad.setVisible (false);
    message = "Rendering " + String (exportBars()) + (exportBars() == 1 ? " bar..." : " bars...");
    proc.startExport (exportBars(), target);
}

//==============================================================================
void FragatimeAudioProcessorEditor::resized()
{
    const int W = getWidth();
    loadBtn.setBounds (240, 14, 110, 28);
    exportBtn.setBounds (358, 14, 90, 28);
    dragPad.setBounds (456, 14, 104, 28);

    themeBtn.setBounds (W - 24 - 72, 14, 72, 28);
    syncBtn.setBounds (W - 24 - 72 - 8 - 64, 14, 64, 28);
    dirBtn.setBounds (W - 24 - 72 - 8 - 64 - 8 - 100, 14, 100, 28);
    srcBtn.setBounds (W - 24 - 72 - 8 - 64 - 8 - 100 - 8 - 110, 14, 110, 28);

    waveArea = { 24, 58, W - 48, 132 };

    // franja Tabie
    tabieBtn.setBounds (24, 216, 156, 34);
    genreBox.setBounds (24, 256, 156, 26);
    bpmArea = { 24, 286, 156, 30 };
    resetBtn.setBounds (130, 321, 50, 18);

    const int cw = (W - 24 - 196 - 20) / 3;
    for (int i = 0; i < 3; ++i)
    {
        const int x = 196 + i * (cw + 10);
        cards[(size_t) i] = { x, 212, cw, 108 };
        applyBtn[(size_t) i].setBounds (x + 10, 288, 76, 24);
        likeBtn[(size_t) i].setBounds (x + 92, 288, 64, 24);
        dislikeBtn[(size_t) i].setBounds (x + 162, 288, 68, 24);
    }

    // perillas
    knobs[0].setBounds (24, 366, 150, 150);                        // INTENSITY (grande)
    const float x0 = 196.f, slotW = ((float) W - 24.f - x0) / 8.f;
    for (int i = 1; i <= 8; ++i)
        knobs[(size_t) i].setBounds ((int) (x0 + slotW * (float) (i - 1)), 376, (int) slotW, 120);
    for (int i = 9; i <= 14; ++i)
        knobs[(size_t) i].setBounds ((int) (x0 + slotW * (float) (i - 9)), 552, (int) slotW, 120);
}

void FragatimeAudioProcessorEditor::timerCallback()
{
    if (isDark() != lastDark) applyTheme();

    const int v = proc.fileVersion.load();
    if (v != lastVersion) { lastVersion = v; proc.getWaveform (wave); }

    const int src = (int) std::round (srcP->load());
    const int dir = (int) std::round (dirP->load());
    static const char* dirNames[3] { "FWD", "REV", "RND" };
    srcBtn.setButtonText (String ("SRC: ") + (src == 0 ? "FILE" : "LIVE"));
    dirBtn.setButtonText (String ("DIR: ") + dirNames[jlimit (0, 2, dir)]);

    const bool sync = syncP->load() > 0.5f;
    if (syncBtn.getToggleState() != sync)
    {
        syncBtn.setToggleState (sync, dontSendNotification);
        knobs[1].updateText();
        knobs[2].updateText();
    }

    // resultado de la exportacion
    const int ser = proc.getExportSerial();
    if (ser != lastExportSerial)
    {
        lastExportSerial = ser;
        if (proc.getExportStatus() == 2)
        {
            const auto f = proc.getExportFile();
            if (exportForDrag)
            {
                dragFile = f;
                dragPad.setVisible (true);
                message = "Ready: " + f.getFileName() + "  -  drag the pad into the FL Studio playlist";
            }
            else message = "Saved: " + f.getFullPathName();
        }
        else message = proc.getExportMessage();
    }
    repaint();
}

//==============================================================================
bool FragatimeAudioProcessorEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
    {
        const auto e = File (f).getFileExtension().toLowerCase();
        if (e == ".wav" || e == ".mp3" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".ogg") return true;
    }
    return false;
}

void FragatimeAudioProcessorEditor::filesDropped (const StringArray& files, int, int)
{
    for (auto& f : files)
        if (File (f).existsAsFile()) { proc.loadFile (File (f)); break; }
}

void FragatimeAudioProcessorEditor::setPositionFromMouse (const MouseEvent& e)
{
    if (wave.empty() || (int) std::round (srcP->load()) != 0) return;
    const float t = jlimit (0.f, 1.f, (e.position.x - (float) waveArea.getX()) / (float) waveArea.getWidth());
    setParam ("position", t * 100.f);
}

void FragatimeAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (waveArea.contains (e.getPosition())) { setPositionFromMouse (e); return; }

    if (bpmArea.contains (e.getPosition()) && proc.getAnalysis().valid)
    {
        const float m = proc.getBpmMultiplier();
        proc.setBpmMultiplier (m < 1.5f && m > 0.75f ? 2.f : (m > 1.5f ? 0.5f : 1.f));
        message = "BPM set to " + String ((int) std::round (proc.getDetectedBpm()));
    }
}

void FragatimeAudioProcessorEditor::mouseDrag (const MouseEvent& e)
{
    if (waveArea.contains (e.getPosition())) setPositionFromMouse (e);
}

//==============================================================================
void FragatimeAudioProcessorEditor::paint (Graphics& g)
{
    const auto p = pal();
    g.fillAll (p.bg);

    // titulo
    g.setColour (p.fg);
    g.setFont (24.f);
    g.drawText ("FRAGATIME", 24, 12, 160, 34, Justification::centredLeft);
    g.setColour (p.dim);
    g.setFont (11.f);
    g.drawText ("v1.1", 176, 24, 40, 14, Justification::centredLeft);

    // ---- pantalla de forma de onda ----
    const auto wa = waveArea.toFloat();
    g.setColour (p.grid);
    g.fillRoundedRectangle (wa, 6.f);

    const bool fileMode = (int) std::round (srcP->load()) == 0;
    const bool hasWave = wave.size() >= (size_t) FragatimeAudioProcessor::kPeaks * 2;

    if (fileMode && hasWave)
    {
        const float mid = wa.getCentreY(), hh = wa.getHeight() * 0.46f;
        const int cols = waveArea.getWidth() - 8;
        g.setColour (p.fg.withAlpha (0.55f));
        for (int c = 0; c < cols; ++c)
        {
            const int idx = c * FragatimeAudioProcessor::kPeaks / cols;
            const float mn = wave[(size_t) idx * 2], mx = wave[(size_t) idx * 2 + 1];
            const float x = wa.getX() + 4.f + (float) c;
            g.drawLine (x, mid - mx * hh * 1.6f, x, mid - mn * hh * 1.6f - 0.5f, 1.f);
        }

        const float cx = wa.getX() + 4.f + proc.centerNorm.load() * (wa.getWidth() - 8.f);
        g.setColour (p.fg.withAlpha (0.18f));
        g.fillRect (cx - 6.f, wa.getY(), 12.f, wa.getHeight());
        g.setColour (p.fg);
        g.drawLine (cx, wa.getY(), cx, wa.getBottom(), 1.5f);

        g.setColour (p.fg.withAlpha (0.9f));
        for (auto& a : proc.grainPos)
        {
            const float gp = a.load();
            if (gp < 0.f) continue;
            const float x = wa.getX() + 4.f + gp * (wa.getWidth() - 8.f);
            g.drawLine (x, wa.getY() + 10.f, x, wa.getBottom() - 10.f, 1.f);
        }
    }
    else
    {
        g.setColour (p.dim.withAlpha (0.5f));
        g.drawLine (wa.getX() + 10.f, wa.getCentreY(), wa.getRight() - 10.f, wa.getCentreY(), 1.f);
        g.setColour (p.dim);
        g.setFont (14.f);
        g.drawText (fileMode ? "Drop an audio file here (wav / mp3), or press LOAD AUDIO"
                             : "LIVE  -  the plugin granulates the audio coming into this track",
                    waveArea, Justification::centred);
    }
    g.setColour (p.dim.withAlpha (0.6f));
    g.drawRoundedRectangle (wa, 6.f, 1.f);

    // linea de mensajes
    g.setColour (p.fg);
    g.setFont (11.f);
    g.drawText (message, 24, 193, getWidth() - 48, 14, Justification::centredLeft);

    // ---- franja TABIE ----
    const auto an = proc.getAnalysis();
    g.setColour (p.fg);
    g.setFont (13.f);
    g.drawText (an.valid ? "BPM  " + String ((int) std::round (proc.getDetectedBpm())) : String ("BPM  --"),
                bpmArea.withHeight (16), Justification::centredLeft);
    g.setColour (p.dim);
    g.setFont (10.f);
    g.drawText (proc.getHostBpm() > 0.0 ? "HOST  " + String ((int) std::round (proc.getHostBpm())) + "   (click BPM: x2 / /2)"
                                        : String ("no host tempo"),
                bpmArea.withTrimmedTop (16), Justification::centredLeft);
    g.drawText ("AI: " + String (proc.getTabie().numFeedback()) + " votes", 24, 321, 100, 14, Justification::centredLeft);

    for (int i = 0; i < 3; ++i)
    {
        const auto r = cards[(size_t) i].toFloat();
        const bool applied = (appliedOpt == i);
        g.setColour (applied ? p.fg.withAlpha (0.14f) : p.grid);
        g.fillRoundedRectangle (r, 8.f);
        g.setColour (applied ? p.fg : p.dim.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 8.f, applied ? 1.5f : 1.f);

        g.setColour (p.fg);
        g.setFont (13.f);
        g.drawText (kOptNames[i], (int) r.getX() + 12, (int) r.getY() + 8, (int) r.getWidth() - 24, 16, Justification::centredLeft);

        const auto& o = options[(size_t) i];
        g.setFont (11.f);
        if (o.valid)
        {
            g.setColour (p.fg);
            g.drawText (describe (o), (int) r.getX() + 12, (int) r.getY() + 32, (int) r.getWidth() - 24, 14, Justification::centredLeft);
            g.setColour (p.dim);
            g.drawText (describeFx (o), (int) r.getX() + 12, (int) r.getY() + 50, (int) r.getWidth() - 24, 14, Justification::centredLeft);
            g.drawText ("start at " + String ((int) std::round (o.position * 100.f)) + "% of the file", (int) r.getX() + 12,
                        (int) r.getY() + 68, (int) r.getWidth() - 24, 14, Justification::centredLeft);
        }
        else
        {
            g.setColour (p.dim);
            g.drawText ("Pick a genre and press TABIE", (int) r.getX() + 12, (int) r.getY() + 36, (int) r.getWidth() - 24, 14,
                        Justification::centredLeft);
        }
    }

    // ---- rotulos de secciones y perillas ----
    g.setFont (10.f);
    g.setColour (p.dim);
    g.drawText ("GRAIN CLOUD", 196, 346, 200, 14, Justification::centredLeft);
    g.drawText ("EFFECTS", 196, 522, 200, 14, Justification::centredLeft);
    g.setColour (p.grid);
    g.drawHorizontalLine (342, 24.f, (float) getWidth() - 24.f);
    g.drawHorizontalLine (518, 196.f, (float) getWidth() - 24.f);
    g.drawVerticalLine (184, 350.f, 680.f);

    g.setFont (11.f);
    g.setColour (p.dim);
    for (size_t i = 0; i < 15; ++i)
    {
        auto b = knobs[i].getBounds();
        g.drawText (kLabels[i], b.getX(), b.getY() - 14, b.getWidth(), 14, Justification::centred);
    }

    // estado del archivo
    g.setColour (p.fg);
    g.setFont (11.f);
    g.drawFittedText (proc.getStatus(), 24, 530, 150, 70, Justification::topLeft, 5);

    g.setColour (p.dim);
    g.setFont (10.f);
    g.drawText ("Click or drag on the waveform to set POSITION  -  SYNC makes DENSITY / SIZE follow the tempo  -  PITCH 50% = original",
                24, 688, getWidth() - 48, 14, Justification::centredLeft);
}

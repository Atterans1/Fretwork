#include "TransportBar.h"

#include "SeqImport.h"

#include "Params.h"
#include "WavesStyle.h"

namespace fw::ui
{

//==============================================================================
// Square Waves Tune button carrying a vector glyph instead of text. The face
// is waves::buttonFace (default / hover / down / on = violet / disabled); the
// glyph ink follows the same states as the LookAndFeel's button text.
class TransportBar::IconButton final : public FretButton
{
public:
    using Glyph = std::function<void (juce::Graphics&, const juce::Rectangle<float>&, juce::Colour)>;

    IconButton (const juce::String& name, Glyph g) : glyph (std::move (g))
    {
        setButtonText (name);
        setClickingTogglesState (true);
        setFretFont (Palette::font (11.0f));
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        const bool enabled = isEnabled();

        waves::buttonFace (g, r, on, over, down, enabled);

        auto ink = on ? juce::Colours::white
                      : Palette::text.withMultipliedAlpha (over ? 1.0f : 0.82f);
        if (! enabled)
            ink = ink.withMultipliedAlpha (0.4f);

        if (glyph != nullptr)
            glyph (g, r.reduced (6.0f), ink);
    }

private:
    Glyph glyph;
};

//==============================================================================
// Inset LCD value box for the transpose stepper (click +1, right-click -1).
class TransportBar::TransposeField final : public RightClickButton
{
public:
    using RightClickButton::RightClickButton;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool enabled = isEnabled();

        waves::valueBox (g, r, getButtonText(), Palette::Scale::large, enabled,
                         down ? Palette::text.brighter (0.35f) : Palette::text);

        if (enabled && (over || down))
        {
            g.setColour (Palette::accent.withAlpha (down ? 0.9f : 0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        }
    }
};

//==============================================================================
// Inset LCD box showing the current preset's name; clicking it opens the list.
class TransportBar::PresetField final : public FretButton
{
public:
    using FretButton::FretButton;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        waves::valueBox (g, r, getButtonText(), Palette::Scale::small, isEnabled(),
                         down ? Palette::text.brighter (0.35f) : Palette::text);

        if (isEnabled() && (over || down))
        {
            g.setColour (Palette::accent.withAlpha (down ? 0.9f : 0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        }
    }
};

//==============================================================================
// Horizontal-drag BPM readout drawn as an inset LCD value box; the accent
// outline marks hover (faint) and an active drag (full).
class TransportBar::BpmField final : public juce::Component
{
public:
    BpmField (juce::Slider& s, juce::AudioParameterFloat& p) : proxy (s), param (p)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool enabled = isEnabled();

        waves::valueBox (g, r, juce::String (param.get(), 1) + " BPM", Palette::Scale::large, enabled, Palette::text);

        if (enabled && (dragging || isMouseOver()))
        {
            g.setColour (Palette::accent.withAlpha (dragging ? 0.9f : 0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        anchor = param.get();
        anchorX = e.position.x;
        dragging = true;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        const auto next = anchor + (e.position.x - anchorX) * 0.5f;
        const auto snapped = (float) juce::roundToInt (next * 10.0f) / 10.0f;

        if (snapped != param.get())
        {
            // Routed through the attachment-backed proxy slider.
            proxy.setValue ((double) snapped, juce::sendNotification);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        proxy.setValue (100.0, juce::sendNotification);
    }

private:
    juce::Slider& proxy;
    juce::AudioParameterFloat& param;
    float anchor = 0.0f, anchorX = 0.0f;
    bool dragging = false;
};

namespace
{
    juce::PathStrokeType roundStroke (float w)
    {
        return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    }

    // A curved arrow. With turnLeft the arrow turns anticlockwise (undo); without,
    // clockwise (redo).
    void drawHistoryGlyph (juce::Graphics& g, const juce::Rectangle<float>& r, juce::Colour ink, bool turnLeft)
    {
        const auto c = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.42f;
        juce::Path arc;
        const float a0 = juce::degreesToRadians (turnLeft ? -60.0f : 60.0f);
        const float a1 = juce::degreesToRadians (turnLeft ? 200.0f : -200.0f);
        arc.addCentredArc (c.x, c.y + rad * 0.15f, rad, rad, 0.0f, a0, a1, true);
        g.setColour (ink);
        g.strokePath (arc, roundStroke (1.6f));

        const auto tip = arc.getPointAlongPath (0.0f);
        const float s = rad * 0.55f, dir = turnLeft ? 1.0f : -1.0f;
        juce::Path head;
        head.addTriangle (tip.x - dir * s, tip.y - s * 0.6f, tip.x + dir * s * 0.2f, tip.y + s * 0.75f, tip.x + dir * s * 0.75f, tip.y - s * 0.55f);
        g.fillPath (head);
    }

    void drawPowerGlyph (juce::Graphics& g, const juce::Rectangle<float>& r, juce::Colour ink)
    {
        const auto c = r.getCentre();
        const auto rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;

        juce::Path ring;
        ring.addCentredArc (c.x, c.y, rad, rad, 0.0f,
                            juce::degreesToRadians (305.0f), juce::degreesToRadians (595.0f), true);

        juce::Path stem;
        stem.startNewSubPath (c.x, c.y - rad - 2.0f);
        stem.lineTo (juce::Point<float> (c.x, c.y - rad * 0.15f));

        g.setColour (ink);
        g.strokePath (ring, roundStroke (1.7f));
        g.strokePath (stem, roundStroke (1.7f));
    }

    // Pyramid metronome: trapezoid body with the pendulum arm leaning right.
    void drawMetronomeGlyph (juce::Graphics& g, const juce::Rectangle<float>& r, juce::Colour ink)
    {
        const float h = juce::jmin (r.getWidth(), r.getHeight());
        const auto box = juce::Rectangle<float> (h * 0.8f, h).withCentre (r.getCentre());
        const float cx = box.getCentreX();

        juce::Path body;
        body.startNewSubPath (box.getX(), box.getBottom());
        body.lineTo (box.getRight(), box.getBottom());
        body.lineTo (cx + h * 0.14f, box.getY());
        body.lineTo (cx - h * 0.14f, box.getY());
        body.closeSubPath();

        const juce::Point<float> pivot (cx, box.getBottom() - h * 0.24f);
        juce::Path arm;
        arm.startNewSubPath (pivot);
        arm.lineTo (juce::Point<float> (cx + h * 0.42f, box.getY() + h * 0.02f));

        g.setColour (ink);
        g.strokePath (body, roundStroke (1.3f));
        g.strokePath (arm, roundStroke (1.5f));
        g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre (pivot));
    }
}

//==============================================================================
TransportBar::TransportBar (FretworkAudioProcessor& p, juce::AudioProcessorValueTreeState& state)
    : processor (p)
{
    transposeParam = dynamic_cast<juce::AudioParameterInt*> (state.getParameter (fw::id::transpose));
    slotParam = dynamic_cast<juce::AudioParameterInt*> (state.getParameter (fw::id::seqSlot));
    bpmParam = dynamic_cast<juce::AudioParameterFloat*> (state.getParameter (fw::id::bpm));

    // Hidden attachment-backed controls are the single write point for the
    // parameters that are stepped or dragged rather than dragged as a widget.
    for (auto* proxy : { &transposeProxy, &slotProxy, &bpmProxy })
    {
        proxy->setVisible (false);
        proxy->setInterceptsMouseClicks (false, false);
        addChildComponent (proxy);
    }

    if (transposeParam != nullptr)
    {
        transposeProxy.setRange (transposeParam->getRange().getStart(),
                                 transposeParam->getRange().getEnd(), 1.0);
        transposeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, fw::id::transpose, transposeProxy);
    }

    if (slotParam != nullptr)
    {
        slotProxy.setRange (slotParam->getRange().getStart(), slotParam->getRange().getEnd(), 1.0);
        slotAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, fw::id::seqSlot, slotProxy);
    }

    if (bpmParam != nullptr)
        bpmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, fw::id::bpm, bpmProxy);

    // ---- power
    powerButton = new IconButton ("POWER", drawPowerGlyph);
    addAndMakeVisible (powerButton);

    if (auto* bp = dynamic_cast<juce::AudioParameterBool*> (state.getParameter (fw::id::power)))
        powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, fw::id::power, *powerButton);

    // ---- transpose stepper
    transposeButton = new TransposeField ("+0");
    transposeButton->setFretFont (Palette::font (Palette::Scale::medium));
    addAndMakeVisible (transposeButton);

    transposeButton->onClick = [this] { nudgeTranspose (1); };
    transposeButton->onRightClick = [this] { nudgeTranspose (-1); };

    transposePrevButton = new FretButton ("<");
    transposePrevButton->setFretFont (Palette::font (13.0f, juce::Font::bold));
    addAndMakeVisible (transposePrevButton);
    transposePrevButton->onClick = [this] { nudgeTranspose (-1); };

    transposeNextButton = new FretButton (">");
    transposeNextButton->setFretFont (Palette::font (13.0f, juce::Font::bold));
    addAndMakeVisible (transposeNextButton);
    transposeNextButton->onClick = [this] { nudgeTranspose (1); };

    // ---- slot paging
    slotPrevButton = new FretButton ("<");
    slotPrevButton->setFretFont (Palette::font (13.0f, juce::Font::bold));
    addAndMakeVisible (slotPrevButton);
    slotPrevButton->onClick = [this] { nudgeSlot (-1); };

    playButton = new FretButton ("PLAY");
    playButton->setFretFont (Palette::font (11.0f));
    addAndMakeVisible (playButton);

    if ((playParam = dynamic_cast<juce::AudioParameterBool*> (state.getParameter (fw::id::seqPlay))) != nullptr)
        playAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, fw::id::seqPlay, *playButton);

    slotNextButton = new FretButton (">");
    slotNextButton->setFretFont (Palette::font (13.0f, juce::Font::bold));
    addAndMakeVisible (slotNextButton);
    slotNextButton->onClick = [this] { nudgeSlot (1); };

    // ---- metronome
    metroButton = new IconButton ("METRONOME", drawMetronomeGlyph);
    addAndMakeVisible (metroButton);

    if (auto* mp = dynamic_cast<juce::AudioParameterBool*> (state.getParameter (fw::id::metronome)))
        metroAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, fw::id::metronome, *metroButton);

    // ---- tempo
    if (bpmParam != nullptr)
    {
        bpmField = new BpmField (bpmProxy, *bpmParam);
        addAndMakeVisible (bpmField);
    }

    // ---- sequencer actions
    clearButton = new FretButton ("CLEAR");
    copyButton = new FretButton ("COPY");
    pasteButton = new FretButton ("PASTE");

    for (auto* b : { clearButton, copyButton, pasteButton })
    {
        b->setFretFont (Palette::font (11.0f));
        addAndMakeVisible (b);
    }

    clearButton->onClick = [this] { processor.getSeqStore().clearSlot (currentSlot()); };
    copyButton->onClick  = [this] { processor.getSeqStore().copyToClipboard (currentSlot()); };
    pasteButton->onClick = [this] { processor.getSeqStore().pasteFromClipboard (currentSlot()); };

    importButton = new FretButton ("IMPORT");
    importButton->setFretFont (Palette::font (11.0f));
    importButton->setTooltip ("Import a strum pattern (.mid, strum keys 28-33) into this slot");
    addAndMakeVisible (importButton);
    importButton->onClick = [this] { importPattern(); };

    // ---- presets
    presetPrevButton = new FretButton ("<");
    presetNextButton = new FretButton (">");
    for (auto* b : { presetPrevButton, presetNextButton })
    {
        b->setFretFont (Palette::font (13.0f, juce::Font::bold));
        addAndMakeVisible (b);
    }
    presetPrevButton->onClick = [this] { stepPreset (-1); };
    presetNextButton->onClick = [this] { stepPreset (1); };

    presetField = new PresetField ("");
    addAndMakeVisible (presetField);
    presetField->onClick = [this] { showPresetMenu(); };

    saveButton = new FretButton ("SAVE");
    saveButton->setFretFont (Palette::font (11.0f));
    addAndMakeVisible (saveButton);
    saveButton->onClick = [this] { savePresetAs(); };

    // ---- undo / redo / A-B
    undoButton = new IconButton ("UNDO", [] (juce::Graphics& g, const juce::Rectangle<float>& r, juce::Colour ink) { drawHistoryGlyph (g, r, ink, true); });
    redoButton = new IconButton ("REDO", [] (juce::Graphics& g, const juce::Rectangle<float>& r, juce::Colour ink) { drawHistoryGlyph (g, r, ink, false); });
    for (auto* b : { undoButton, redoButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    undoButton->setTooltip ("Undo");
    redoButton->setTooltip ("Redo");
    undoButton->onClick = [this] { processor.getPatches().undo(); tick(); };
    redoButton->onClick = [this] { processor.getPatches().redo(); tick(); };

    abButton = new FretButton ("A");
    abButton->setFretFont (Palette::font (11.0f, juce::Font::bold));
    abButton->setTooltip ("Compare: switch between sound A and sound B");
    addAndMakeVisible (abButton);
    abButton->onClick = [this] { processor.getPatches().toggleAB(); tick(); };

    tick();
}

TransportBar::~TransportBar()
{
    // The child buttons are created with new and owned here. The button
    // attachments detach from their buttons when destroyed, so drop them first.
    powerAttachment.reset();
    playAttachment.reset();
    metroAttachment.reset();

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             powerButton, transposeButton, transposePrevButton, transposeNextButton, slotPrevButton, playButton, slotNextButton,
             metroButton, bpmField, clearButton, copyButton, pasteButton, importButton,
             presetPrevButton, presetField, presetNextButton, saveButton, undoButton, redoButton, abButton })
        delete c;
}

void TransportBar::nudgeTranspose (int delta)
{
    if (transposeProxy.getRange().isEmpty())
        return;

    const int next = juce::jlimit ((int) transposeProxy.getMinimum(), (int) transposeProxy.getMaximum(),
                                   juce::roundToInt (transposeProxy.getValue()) + delta);
    transposeProxy.setValue ((double) next);
}

void TransportBar::nudgeSlot (int delta)
{
    if (slotProxy.getRange().isEmpty())
        return;

    const int next = juce::jlimit ((int) slotProxy.getMinimum(), (int) slotProxy.getMaximum(),
                                   juce::roundToInt (slotProxy.getValue()) + delta);
    slotProxy.setValue ((double) next);
}

int TransportBar::currentSlot() const noexcept
{
    if (slotParam == nullptr)
        return 0;

    return juce::jlimit (0, processor.getSeqStore().slotCount() - 1, (int) slotParam->get() - 1);
}

//==============================================================================
void TransportBar::tick()
{
    if (transposeButton != nullptr && transposeParam != nullptr)
    {
        const int v = (int) transposeParam->get();
        transposeButton->setButtonText (v > 0 ? ("+" + juce::String (v)) : juce::String (v));
    }

    if (playButton != nullptr && playParam != nullptr)
        playButton->setButtonText (playParam->get() ? "STOP" : "PLAY");

    auto& patches = processor.getPatches();
    patches.poll();
    undoButton->setEnabled (patches.canUndo());
    redoButton->setEnabled (patches.canRedo());
    abButton->setButtonText (patches.isB() ? "B" : "A");
    abButton->setToggleState (patches.isB(), juce::dontSendNotification);
    presetField->setButtonText (patches.currentPresetName());
}

void TransportBar::stepPreset (int delta)
{
    auto& patches = processor.getPatches();
    const int n = patches.numPresets();
    if (n <= 0)
        return;
    patches.loadPreset (((patches.currentPreset() + delta) % n + n) % n);
    tick();
}

void TransportBar::showPresetMenu()
{
    auto& patches = processor.getPatches();
    const auto& factory = fw::PatchManager::factoryPresets();
    juce::PopupMenu menu;

    static constexpr const char* kBodies[] = { "Dreadnought Steel", "Classical Nylon", "Solid Electric", "Semi-Hollow" };
    for (int body = 0; body < 4; ++body)
    {
        menu.addSectionHeader (kBodies[body]);
        for (int i = 0; i < (int) factory.size(); ++i)
            if (factory[(size_t) i].body == body)
                menu.addItem (i + 1, factory[(size_t) i].name, true, i == patches.currentPreset());
    }

    const int users = patches.numPresets() - (int) factory.size();
    if (users > 0)
    {
        menu.addSectionHeader ("User");
        for (int u = 0; u < users; ++u)
        {
            const int i = (int) factory.size() + u;
            menu.addItem (i + 1, patches.presetName (i), true, i == patches.currentPreset());
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetField),
                        [safe = juce::Component::SafePointer<TransportBar> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                            {
                                safe->processor.getPatches().loadPreset (result - 1);
                                safe->tick();
                            }
                        });
}

void TransportBar::savePresetAs()
{
    auto* w = new juce::AlertWindow ("Save Preset", "Name for this sound:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", processor.getPatches().currentPresetName());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<TransportBar> (this), w] (int result)
        {
            if (safe == nullptr || result != 1)
                return;
            const auto name = w->getTextEditorContents ("name");
            if (! safe->processor.getPatches().saveUserPreset (name))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Preset",
                                                        "Could not save \"" + name + "\".");
            safe->tick();
        }), true);
}

void TransportBar::importPattern()
{
    chooser = std::make_unique<juce::FileChooser> ("Import strum pattern", juce::File(), "*.mid;*.midi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<TransportBar> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            juce::FileInputStream in (fc.getResult());
            juce::MidiFile file;
            if (! in.openedOk() || ! file.readFrom (in))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import",
                                                        "That file could not be read as MIDI.");
                return;
            }

            auto& vts = safe->processor.getState();
            const int q = (int) std::lround (vts.getParameter (fw::id::quantize)->convertFrom0to1 (vts.getParameter (fw::id::quantize)->getValue()));
            const int s = (int) std::lround (vts.getParameter (fw::id::signature)->convertFrom0to1 (vts.getParameter (fw::id::signature)->getValue()));
            const auto r = fw::importStrumPattern (file, safe->processor.getSeqStore(), safe->currentSlot(),
                                                   fw::seqStepsPerQuarter (q), fw::seqStepsPerBar (q, s));

            if (! r.ok)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Import",
                    "No strum keys (notes 28-33) were found in the first bar, so the slot was left as it was.");
            else if (r.ignored > 0 || r.beyondBar > 0)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Import",
                    juce::String (r.imported) + " strokes imported. "
                    + (r.beyondBar > 0 ? juce::String (r.beyondBar) + " after the first bar were left out (a slot holds one bar). " : juce::String())
                    + (r.ignored > 0 ? juce::String (r.ignored) + " notes that are not strum keys were ignored." : juce::String()));
        });
}

void TransportBar::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();

    // Waves Tune bottom strip: an untitled group frame filling the bar
    // (group() starts its rule 7 px below the rectangle's top, hence the lift).
    waves::group (g, full.withTop (full.getY() - 7.0f));

    waves::caption (g, transposeCaption, "Transpose", juce::Justification::centredRight);

    g.setColour (Palette::line);
    for (const float x : dividers)
        if (x > 0.0f)
            g.fillRect (juce::Rectangle<float> (x, full.getY() + 9.0f, 1.0f, full.getHeight() - 18.0f));
}

void TransportBar::resized()
{
    constexpr int h = 26, gap = 4, clusterGap = 14;
    const int y = (getHeight() - h) / 2;

    int x = 10;

    const auto place = [&x, y, h] (juce::Component* c, int w, int after)
    {
        if (c != nullptr)
            c->setBounds (x, y, w, h);

        x += w + after;
    };

    const auto divider = [&x, clusterGap] (float& slot)
    {
        slot = (float) x - 0.5f * (float) clusterGap - 0.5f;
    };

    // power | TRANSPOSE [+0]
    place (powerButton, 36, 12);
    transposeCaption = { (float) x, (float) y, 80.0f, (float) h };
    x += 80 + 6;
    place (transposePrevButton, 20, 2);
    place (transposeButton, 52, 2);
    place (transposeNextButton, 20, clusterGap);
    divider (dividers[0]);

    // < PLAY >
    place (slotPrevButton, 28, gap);
    place (playButton, 62, gap);
    place (slotNextButton, 28, clusterGap);
    divider (dividers[1]);

    // metronome [100.0] BPM
    place (metroButton, 36, 8);
    place (bpmField, 84, clusterGap);
    divider (dividers[2]);

    // CLEAR COPY PASTE IMPORT edit the sequencer pattern, not the sound, so
    // they sit at the right edge with their own divider.
    constexpr int editW = 52 + gap + 48 + gap + 50 + gap + 56;
    const int editX = getWidth() - 10 - editW;

    // < preset name > SAVE | undo redo A/B fill the middle; the name box takes
    // whatever width is left, so nothing overlaps at small sizes.
    constexpr int fixedW = 24 + gap + gap + 24 + gap + 40 + clusterGap + 28 + gap + 28 + gap + 30;
    const int nameW = juce::jlimit (70, 220, editX - clusterGap - x - fixedW);

    place (presetPrevButton, 24, gap);
    place (presetField, nameW, gap);
    place (presetNextButton, 24, gap);
    place (saveButton, 40, clusterGap);
    divider (dividers[3]);
    place (undoButton, 28, gap);
    place (redoButton, 28, gap);
    place (abButton, 30, clusterGap);

    x = juce::jmax (x, editX);
    divider (dividers[4]);
    place (clearButton, 52, gap);
    place (copyButton, 48, gap);
    place (pasteButton, 50, gap);
    place (importButton, 56, 0);
}

} // namespace fw::ui

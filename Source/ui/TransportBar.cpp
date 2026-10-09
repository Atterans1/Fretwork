#include "TransportBar.h"

#include "Params.h"

namespace fw::ui
{

//==============================================================================
//==============================================================================
class TransportBar::IconButton final : public FretButton
{
public:
    using Glyph = std::function<void (juce::Graphics&, const juce::Rectangle<float>&)>;

    IconButton (const juce::String& name, Glyph g) : glyph (std::move (g))
    {
        setButtonText (name);
        setClickingTogglesState (true);
        setFretFont (Palette::font (11.0f));
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (getToggleState() ? Palette::accent : Palette::panelHi);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        if (glyph != nullptr)
            glyph (g, r.reduced (6.0f));
    }

private:
    Glyph glyph;
};

//==============================================================================
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
        g.setColour (Palette::panelHi);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        g.setFont (Palette::font (13.0f));
        g.setColour (Palette::text);
        g.drawText (juce::String (param.get(), 1), r.reduced (4.0f, 0.0f),
                    juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        anchor = param.get();
        anchorX = e.position.x;
        dragging = true;
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

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

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

//==============================================================================
class TransportBar::StrumButton final : public FretButton
{
public:
    StrumButton (const juce::String& caption, juce::MidiKeyboardState& k, int note)
        : FretButton (caption), state (k), midiNote (note)
    {
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        FretButton::mouseDown (e);
        state.noteOn (1, midiNote, 0.8f);
        held = true;
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        FretButton::mouseUp (e);

        if (held)
        {
            state.noteOff (1, midiNote, 0.8f);
            held = false;
        }
    }

private:
    juce::MidiKeyboardState& state;
    int midiNote;
    bool held = false;
};

namespace
{
    void drawPowerGlyph (juce::Graphics& g, const juce::Rectangle<float>& r)
    {
        const auto c = r.getCentre();
        const auto rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;

        juce::Path ring;
        ring.addCentredArc (c.x, c.y, rad, rad, 0.0f,
                            juce::degreesToRadians (305.0f), juce::degreesToRadians (595.0f), true);

        juce::Path stem;
        stem.startNewSubPath (c.x, c.y - rad - 2.0f);
        stem.lineTo (juce::Point<float> (c.x, c.y - rad * 0.15f));

        g.setColour (Palette::background);
        g.strokePath (ring, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath (stem, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawMetronomeGlyph (juce::Graphics& g, const juce::Rectangle<float>& r)
    {
        const auto cx = r.getCentreX();
        const auto bottom = r.getBottom() - 4.0f;
        const auto top = r.getY() + 5.0f;
        const auto rad = (bottom - top) * 0.6f;

        juce::Path swing;
        swing.addCentredArc (cx, bottom, rad, rad, 0.0f,
                             juce::degreesToRadians (252.0f), juce::degreesToRadians (288.0f), true);

        juce::Path arm;
        arm.startNewSubPath (cx, bottom);
        arm.lineTo (juce::Point<float> (cx + rad * 0.30f, bottom - rad * 0.86f));

        g.setColour (Palette::background);
        g.strokePath (swing, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath (arm, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (cx - 1.8f, bottom - 1.8f, 3.6f, 3.6f);
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
    transposeButton = new RightClickButton ("+0");
    transposeButton->setFretFont (Palette::font (13.0f));
    addAndMakeVisible (transposeButton);

    transposeButton->onClick = [this] { nudgeTranspose (1); };
    transposeButton->onRightClick = [this] { nudgeTranspose (-1); };

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

    // ---- strum triggers
    // Down/up triangles are written as escaped UTF-8 so the source stays pure ASCII.
    strumDownButton = new StrumButton ("STRUM v", processor.getKeyboardState(), 28);
    strumUpButton = new StrumButton ("STRUM ^", processor.getKeyboardState(), 29);

    for (auto* b : { strumDownButton, strumUpButton })
    {
        b->setFretFont (Palette::font (11.0f));
        b->setColour (juce::TextButton::buttonColourId, Palette::panelHi);
        addAndMakeVisible (b);
    }

    tick();
}

TransportBar::~TransportBar() = default;

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
}

void TransportBar::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (full, 6.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (full.reduced (0.5f), 6.0f, 1.0f);
}

void TransportBar::resized()
{
    constexpr int h = 28, gap = 8;
    const int y = (getHeight() - h) / 2;
    const int strumW = 84;

    int x = 8;

    const auto place = [this, &x, y, h, gap] (juce::Component* c, int w)
    {
        if (c == nullptr)
            return;

        c->setBounds (x, y, w, h);
        x += w + gap;
    };

    place (powerButton, 36);
    place (transposeButton, 58);
    place (slotPrevButton, 30);
    place (playButton, 62);
    place (slotNextButton, 30);
    place (metroButton, 36);
    place (bpmField, 66);
    place (clearButton, 56);
    place (copyButton, 52);
    place (pasteButton, 54);

    // flexible spacer, then the two strum triggers right-aligned
    int rx = getWidth() - 8 - strumW;

    if (strumUpButton != nullptr)
        strumUpButton->setBounds (rx, y, strumW, h);

    rx -= gap + strumW;

    if (strumDownButton != nullptr)
        strumDownButton->setBounds (rx, y, strumW, h);
}

} // namespace fw::ui

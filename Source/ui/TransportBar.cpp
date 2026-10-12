#include "TransportBar.h"

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

        waves::valueBox (g, r, getButtonText(), 14.0f, enabled,
                         down ? Palette::lcdText.brighter (0.35f) : Palette::lcdText);

        if (enabled && (over || down))
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

        waves::valueBox (g, r, juce::String (param.get(), 1), 14.0f, enabled, Palette::lcdText);

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
             powerButton, transposeButton, slotPrevButton, playButton, slotNextButton,
             metroButton, bpmField, clearButton, copyButton, pasteButton })
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
}

void TransportBar::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();

    // Waves Tune bottom strip: an untitled group frame filling the bar
    // (group() starts its rule 7 px below the rectangle's top, hence the lift).
    waves::group (g, full.withTop (full.getY() - 7.0f));

    waves::caption (g, transposeCaption, "Transpose", juce::Justification::centredRight);
    waves::caption (g, bpmCaption, "BPM", juce::Justification::centredLeft);

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
    place (transposeButton, 52, clusterGap);
    divider (dividers[0]);

    // < PLAY >
    place (slotPrevButton, 28, gap);
    place (playButton, 62, gap);
    place (slotNextButton, 28, clusterGap);
    divider (dividers[1]);

    // metronome [100.0] BPM
    place (metroButton, 36, 8);
    place (bpmField, 66, 6);
    bpmCaption = { (float) x, (float) y, 30.0f, (float) h };
    x += 30 + clusterGap;

    // CLEAR COPY PASTE edit the sequencer pattern, not the transport, so they
    // sit at the right edge with their divider, balancing the bar.
    constexpr int editW = 56 + gap + 52 + gap + 54;
    x = juce::jmax (x, getWidth() - 10 - editW);
    divider (dividers[2]);
    place (clearButton, 56, gap);
    place (copyButton, 52, gap);
    place (pasteButton, 54, 0);
}

} // namespace fw::ui

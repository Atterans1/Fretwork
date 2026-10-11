#include "KeyboardStrip.h"
#include "WavesStyle.h"

namespace fw::ui
{

//==============================================================================
KeyboardStrip::KeyboardStrip (juce::MidiKeyboardState& state, const fw::UiSnapshot& source)
    // JUCE 8 dropped the old (state, useKeyboard, drawMidiNotes) ctor; the
    // component is now driven purely by the MidiKeyboardState it is given.
    : juce::MidiKeyboardComponent (state, juce::KeyboardComponentBase::horizontalKeyboard)
    , ui (source)
{
    setAvailableRange (kLowestKey, kHighestKey);
    setKeyWidth (22.0f);

    // Visual only: the editor drives the processor's keyboard state directly.
    setWantsKeyboardFocus (false);
    setScrollButtonsVisible (false);

    // JUCE 8 renamed these ids and reduced them to one separator line colour.
    // The pressed and hover looks are painted in drawWhiteNote/drawBlackNote,
    // so the overlay colour ids are not used.
    setColour (whiteNoteColourId,            Palette::whiteKey);
    setColour (blackNoteColourId,            Palette::blackKey);
    setColour (shadowColourId,               juce::Colours::transparentBlack);
    setColour (keySeparatorLineColourId,     Palette::line.withAlpha (0.55f));
    setColour (textLabelColourId,            Palette::dim);
}

bool KeyboardStrip::isLatched (int midiNote) const noexcept
{
    return isKeyswitch (midiNote)
        && (latched & (1u << (std::uint32_t) (midiNote - kLowestKey))) != 0u;
}

//==============================================================================
void KeyboardStrip::paint (juce::Graphics& g)
{
    // The audio thread updates the latched keyswitches; one read per frame
    // keeps every key in this paint consistent.
    latched = ui.ksNotes.load();

    const bool disabled = ! isEnabled();

    if (disabled)
        g.beginTransparencyLayer (0.4f);

    juce::MidiKeyboardComponent::paint (g);

    const auto firstKey = getRectangleForKey (kLowestKey);
    const auto ksKey    = getRectangleForKey (kKsLast);

    const auto zone = juce::Rectangle<float> (firstKey.getX(), 0.0f,
                                              ksKey.getRight() - firstKey.getX(),
                                              (float) getHeight());

    // Waves Tune style indicator strip along the top of the keyswitch band.
    g.setColour (Palette::accent3.withAlpha (0.8f));
    g.fillRect (zone.withHeight (3.0f));

    drawCaption (g, zone);

    if (disabled)
        g.endTransparencyLayer();
}

void KeyboardStrip::drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                                   bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour)
{
    // Flat fill. A sounding key is lit violet; a latched keyswitch magenta;
    // the rest of the keyswitch band carries a faint blue tint. Each key fills
    // only its own rect, and black keys are drawn afterwards, so nothing here
    // can cover a neighbour.
    const bool latchedKey = isLatched (midiNoteNumber);
    auto fill = Palette::whiteKey;

    if (isKeyswitch (midiNoteNumber))
        fill = fill.overlaidWith (Palette::accent3.withAlpha (0.16f));

    if (latchedKey)
        fill = Palette::accent2;

    if (isDown)
        fill = Palette::accent;
    else if (isOver)
        fill = fill.darker (0.06f);

    g.setColour (fill);
    g.fillRect (area);

    // subtle shade towards the front edge of the key
    const auto shade = area.withTop (area.getBottom() - area.getHeight() * 0.16f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, shade.getY(),
                                             juce::Colours::black.withAlpha (isDown ? 0.20f : 0.10f),
                                             0.0f, shade.getBottom(), false));
    g.fillRect (shade);

    // octave label on the C keys, bottom-left like Waves Tune
    if (const auto text = getWhiteNoteText (midiNoteNumber); text.isNotEmpty())
    {
        g.setColour (isDown || latchedKey ? juce::Colours::white.withAlpha (0.9f) : textColour);
        g.setFont (Palette::fontSemi (9.5f));
        g.drawText (text, area.reduced (3.0f, 4.0f), juce::Justification::bottomLeft, false);
    }

    // hairline separators
    g.setColour (lineColour);
    g.fillRect (area.withWidth (1.0f));

    if (midiNoteNumber == getRangeEnd())
        g.fillRect (area.withLeft (area.getRight() - 1.0f));
}

void KeyboardStrip::drawBlackNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                                   bool isDown, bool isOver, juce::Colour noteFillColour)
{
    const bool latchedKey = isLatched (midiNoteNumber);
    const bool lit = isDown || latchedKey;

    auto base = noteFillColour;

    if (latchedKey)
        base = Palette::accent2;

    if (isDown)
        base = Palette::accent;
    else if (isOver)
        base = base.brighter (0.25f);

    // body gradient, lighter at the top
    g.setGradientFill (juce::ColourGradient (base.brighter (lit ? 0.15f : 0.12f), 0.0f, area.getY(),
                                             base.darker (lit ? 0.25f : 0.35f), 0.0f, area.getBottom(), false));
    g.fillRect (area);

    if (isKeyswitch (midiNoteNumber) && ! lit)
    {
        g.setColour (Palette::accent3.withAlpha (0.22f));
        g.fillRect (area);
    }

    // top highlight down the middle of the key
    const auto glossH = area.getHeight() * 0.45f;
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (isOver ? 0.22f : 0.14f), 0.0f, area.getY(),
                                             juce::Colours::transparentWhite, 0.0f, area.getY() + glossH, false));
    g.fillRect (area.reduced (area.getWidth() * 0.14f, 0.0f).withHeight (glossH));

    // the key's front lip
    const auto lipH = juce::jmin (6.0f, area.getHeight() * 0.07f);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.18f : 0.08f));
    g.fillRect (area.reduced (1.5f, 0.0f).withTop (area.getBottom() - lipH));

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRect (area, 1.0f);
}

void KeyboardStrip::drawCaption (juce::Graphics& g, const juce::Rectangle<float>& zone)
{
    // Small-caps caption on a little LCD plate centred in the band, just under
    // the black keys, so it never runs over a key edge or the C labels below.
    const auto font = Palette::fontSemi (10.5f).withExtraKerningFactor (0.08f);
    const auto w = juce::GlyphArrangement::getStringWidth (font, "KEYSWITCHES") + 18.0f;
    const auto y = (float) getHeight() * getBlackNoteLengthProportion() + 6.0f;
    const auto plate = juce::Rectangle<float> (w, 16.0f).withPosition (zone.getCentreX() - w * 0.5f, y);

    waves::lcd (g, plate, 3.0f);
    waves::caption (g, plate, "Keyswitches");
}

} // namespace fw::ui

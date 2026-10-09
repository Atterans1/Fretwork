#include "KeyboardStrip.h"

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
    setColour (whiteNoteColourId,            Palette::whiteKey);
    setColour (blackNoteColourId,            Palette::blackKey);
    setColour (shadowColourId,               juce::Colours::transparentBlack);
    setColour (keySeparatorLineColourId,     Palette::line.withAlpha (0.55f));
    setColour (textLabelColourId,            juce::Colours::transparentBlack);
    // These were transparent, which made a white key give no feedback at
    // all when pressed - the key was sounding but nothing on screen moved, so
    // it read as broken. Black keys hid it because they already read as pressed.
    setColour (mouseOverKeyOverlayColourId,  Palette::text.withAlpha (0.10f));
    setColour (keyDownOverlayColourId,       Palette::accent.withAlpha (0.55f));
}

juce::Rectangle<float> KeyboardStrip::keyRect (int midiNote) const
{
    return getRectangleForKey (midiNote);
}

//==============================================================================
void KeyboardStrip::paint (juce::Graphics& g)
{
    juce::MidiKeyboardComponent::paint (g);

    const auto firstKey = getRectangleForKey (kLowestKey);
    const auto ksKey    = getRectangleForKey (kKsLast);

    const auto zone = juce::Rectangle<float> (firstKey.getX(), 0.0f,
                                              ksKey.getRight() - firstKey.getX(),
                                              (float) getHeight());

    g.setColour (Palette::accent.withAlpha (0.12f));
    g.fillRect (zone);

    // Latched keyswitches are tinted, not filled. An opaque fill also covered
    // the neighbouring black keys (a white key's rect spans the full width) and
    // erased the key-down overlay the press feedback depends on.
    const auto latched = ui.ksNotes.load();

    const auto tintLatched = [this, &g, &latched] (bool wantBlack)
    {
        for (int n = kLowestKey; n <= kKsLast; ++n)
        {
            if ((latched & (1u << (std::uint32_t) (n - 12))) == 0u)
                continue;

            if (juce::MidiMessage::isMidiNoteBlack (n) != wantBlack)
                continue;

            g.setColour (Palette::accent.withAlpha (0.45f));
            g.fillRect (keyRect (n));
        }
    };

    tintLatched (false);   // white first, so the black keys drawn over it survive
    tintLatched (true);

    drawCaption (g, zone);
}

void KeyboardStrip::drawCaption (juce::Graphics& g, const juce::Rectangle<float>& zone)
{
    // Drawn as one centred run. The previous version positioned each glyph from
    // its own measured width, which overlapped them into an illegible blob.
    g.setFont (Palette::font (11.0f));
    g.setColour (Palette::dim);
    g.drawText (juce::String ("KEYSWITCHES"), zone, juce::Justification::centred, true);
}

} // namespace fw::ui

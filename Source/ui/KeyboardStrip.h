#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "FretLookAndFeel.h"
#include "UiSnapshot.h"

namespace fw::ui
{

/** Flat 2-octave-plus MIDI keyboard over the processor's keyboard state, in the
    Waves Tune style: flat white keys with hairline separators, black keys with
    a top highlight, sounding keys lit violet. The keyswitch band is tinted per
    key and its latched keys lit magenta, all inside drawWhiteNote/drawBlackNote. */
class KeyboardStrip final : public juce::MidiKeyboardComponent
{
public:
    KeyboardStrip (juce::MidiKeyboardState& state, const fw::UiSnapshot& ui);
    ~KeyboardStrip() override = default;

    void paint (juce::Graphics&) override;

private:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override;

    static constexpr int kLowestKey = 12;
    static constexpr int kHighestKey = 84;
    static constexpr int kKsLast = 37;

    static bool isKeyswitch (int midiNote) noexcept { return midiNote >= kLowestKey && midiNote <= kKsLast; }
    bool isLatched (int midiNote) const noexcept;

    void drawCaption (juce::Graphics&, const juce::Rectangle<float>& zone);

    const fw::UiSnapshot& ui;
    std::uint32_t latched = 0u;   // ui.ksNotes, read once per paint

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardStrip)
};

} // namespace fw::ui

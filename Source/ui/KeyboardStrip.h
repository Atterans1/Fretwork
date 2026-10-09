#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "FretLookAndFeel.h"
#include "UiSnapshot.h"

namespace fw::ui
{

/** Flat 2-octave-plus MIDI keyboard over the processor's keyboard state.
    The flat look comes from MidiKeyboardComponent's own ColourIds, and the
    keyswitch band plus latched-key highlight are drawn over the finished keys. */
class KeyboardStrip final : public juce::MidiKeyboardComponent
{
public:
    KeyboardStrip (juce::MidiKeyboardState& state, const fw::UiSnapshot& ui);
    ~KeyboardStrip() override = default;

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> keyRect (int midiNote) const;

    static constexpr int kLowestKey = 12;
    static constexpr int kHighestKey = 96;
    static constexpr int kKsLast = 37;
    void drawCaption (juce::Graphics&, const juce::Rectangle<float>& zone);

    const fw::UiSnapshot& ui;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardStrip)
};

} // namespace fw::ui

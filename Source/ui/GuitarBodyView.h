#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Params.h"
#include "FretLookAndFeel.h"

namespace fw::ui
{

/** Body artwork for the selected guitar, drawn in the strip under region A.

    Only the bodies we actually have artwork for get a picture. Classical Nylon
    and Solid Electric have none, so rather than leave the panel blank - which
    reads as a loading failure - the selected body's name is drawn in its place. */
class GuitarBodyView final : public juce::Component,
                             private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit GuitarBodyView (juce::AudioProcessorValueTreeState& state);
    ~GuitarBodyView() override;

    void paint (juce::Graphics&) override;

    void parameterChanged (const juce::String& parameterID, float newValue) override;

private:
    juce::AudioProcessorValueTreeState& state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarBodyView)
};

} // namespace fw::ui

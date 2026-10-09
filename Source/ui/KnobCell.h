#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "FretLookAndFeel.h"

namespace fw::ui
{

/** One rotary control: 11px uppercase caption, rotary knob, 13px value readout.
    The value text is derived from the parameter itself, so it stays correct
    whatever range the SliderAttachment applies to the underlying slider.

    JUCE 8 removed RangedAudioParameterListener, so the readout is driven by an
    AudioProcessorValueTreeState::Listener registered for this cell's parameter. */
class KnobCell final : public juce::Component,
                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    KnobCell();
    ~KnobCell() override;

    /** Binds the knob to a parameter and sets the caption.
        micro == true gives a 9px caption and hides the value readout. */
    void attach (juce::AudioProcessorValueTreeState& state,
                 const juce::String& paramId,
                 const juce::String& caption,
                 bool micro = false);

    void paint (juce::Graphics&) override {}
    void resized() override;

    void parameterChanged (const juce::String& parameterID, float newValue) override;

    juce::Slider& knob() noexcept { return slider; }

private:
    void refreshValueText();

    juce::Slider slider { juce::Slider::Rotary, juce::Slider::NoTextBox };
    juce::Label  captionLabel;
    juce::Label  valueLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String paramId;
    juce::RangedAudioParameter* param = nullptr;
    bool micro = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobCell)
};

} // namespace fw::ui

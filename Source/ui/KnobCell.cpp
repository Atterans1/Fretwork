#include "KnobCell.h"

namespace fw::ui
{

KnobCell::KnobCell()
{
    slider.setSliderStyle (juce::Slider::Rotary);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 1.0, 0.0);

    // Full 360-degree sweep so a control turns through the whole circumference.
    // The stock range only spanned 288 degrees. Angles are clockwise from the
    // top, so 0 -> 2*pi is one complete turn.
    slider.setRotaryParameters (0.0f,
                                2.0f * juce::MathConstants<float>::pi,
                                true);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, Palette::line);
    slider.setColour (juce::Slider::rotarySliderFillColourId,    Palette::accent);
    addAndMakeVisible (slider);

    captionLabel.setJustificationType (juce::Justification::centred);
    captionLabel.setFont (Palette::font (11.0f));
    captionLabel.setColour (juce::Label::textColourId, Palette::dim);
    captionLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (captionLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (Palette::font (13.0f));
    valueLabel.setColour (juce::Label::textColourId, Palette::text);
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);
}

KnobCell::~KnobCell()
{
    if (state != nullptr)
        state->removeParameterListener (paramId, this);
}

void KnobCell::attach (juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& id,
                       const juce::String& caption,
                       bool isMicro)
{
    micro = isMicro;

    auto* p = apvts.getParameter (id);
    param = p;
    state = &apvts;
    paramId = id;
    apvts.addParameterListener (paramId, this);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
    slider.onValueChange = [this] { refreshValueText(); };

    captionLabel.setText (caption.toUpperCase(), juce::dontSendNotification);
    captionLabel.setFont (Palette::font (micro ? 9.0f : 11.0f));
    valueLabel.setVisible (! micro);

    refreshValueText();
    resized();
}

void KnobCell::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == paramId)
        refreshValueText();
}

void KnobCell::refreshValueText()
{
    if (param == nullptr)
        return;

    // JUCE 8 removed RangedAudioParameter::get(); getCurrentValueAsText() is the
    // supported accessor for the parameter's formatted value.
    valueLabel.setText (param->getCurrentValueAsText(), juce::dontSendNotification);
}

void KnobCell::resized()
{
    auto b = getLocalBounds();

    if (micro)
    {
        valueLabel.setVisible (false);
        captionLabel.setBounds (b.removeFromTop (11));
        slider.setBounds (b.reduced (3, 1));
        return;
    }

    captionLabel.setBounds (b.removeFromTop (15));
    valueLabel.setBounds (b.removeFromBottom (16));
    slider.setBounds (b.reduced (6, 0));
}

} // namespace fw::ui

#include "KnobCell.h"

#include "WavesStyle.h"
#include "PluginProcessor.h"

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
    slider.onPopupMenu = [this] { showLearnMenu(); };

    captionText = caption;

    refreshValueText();
    resized();
    repaint();
}

void KnobCell::parameterChanged (const juce::String& parameterID, float)
{
    // Host automation can call this on the audio thread, where writing
    // valueText and repainting would race paint(). The SliderAttachment already
    // carries those changes to the slider on the message thread, and its
    // onValueChange refreshes the text, so only act here on the message thread.
    if (parameterID == paramId && juce::MessageManager::existsAndIsCurrentThread())
        refreshValueText();
}

void KnobCell::showLearnMenu()
{
    auto* proc = state != nullptr ? dynamic_cast<FretworkAudioProcessor*> (&state->processor) : nullptr;
    if (proc == nullptr)
        return;

    auto& learn = proc->getMidiLearn();
    const int cc = learn.ccFor (paramId);

    juce::PopupMenu menu;
    menu.addSectionHeader (captionText.isNotEmpty() ? captionText : paramId);
    if (learn.isLearning (paramId))
        menu.addItem (2, "Cancel MIDI Learn (waiting for a controller)");
    else
        menu.addItem (1, "MIDI Learn");
    if (cc >= 0)
        menu.addItem (3, "Forget MIDI CC " + juce::String (cc));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider),
        [safe = juce::Component::SafePointer<KnobCell> (this), proc] (int result)
        {
            if (safe == nullptr)
                return;
            auto& l = proc->getMidiLearn();
            if (result == 1) l.startLearning (safe->paramId);
            if (result == 2) l.cancelLearning();
            if (result == 3) l.forget (safe->paramId);
        });
}

void KnobCell::refreshValueText()
{
    if (param == nullptr)
        return;

    // JUCE 8 removed RangedAudioParameter::get(); getCurrentValueAsText() is the
    // supported accessor for the parameter's formatted value.
    const auto text = param->getCurrentValueAsText();

    if (text != valueText)
    {
        valueText = text;
        if (! micro)
            repaint (valueArea);
    }
}

void KnobCell::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();
    const auto captionColour = enabled ? Palette::caption : Palette::dim.withAlpha (0.6f);

    waves::caption (g, captionArea.toFloat(), captionText, juce::Justification::centred, captionColour);

    if (! micro && ! valueArea.isEmpty())
        waves::valueBox (g, valueArea.toFloat(), valueText,
                         (float) valueArea.getHeight() * 0.72f, enabled);
}

void KnobCell::resized()
{
    auto b = getLocalBounds();

    if (micro)
    {
        // Compact strip cell (string fine-tunes): caption only, knob fills the rest.
        captionArea = b.removeFromTop (12);
        valueArea = {};
        slider.setBounds (b.reduced (3, 0));
        return;
    }

    // Large cells (strummer, humanize) get a taller caption and readout; the
    // 56 x 64 setup-tab cells use the compact proportions.
    const bool large = getHeight() >= 90;
    const int captionH = large ? 16 : 13;
    const int valueH   = large ? 19 : 15;

    captionArea = b.removeFromTop (captionH);

    auto valueRow = b.removeFromBottom (valueH);
    const int boxW = juce::jmin (valueRow.getWidth() - 2, large ? 76 : 52);
    valueArea = valueRow.withSizeKeepingCentre (boxW, valueH);

    b.removeFromBottom (large ? 3 : 1);
    slider.setBounds (b.reduced (large ? 6 : 2, 0));
}

} // namespace fw::ui

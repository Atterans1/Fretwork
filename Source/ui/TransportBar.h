#pragma once

#include <array>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "FretLookAndFeel.h"
#include "PluginProcessor.h"

namespace fw::ui
{

// JUCE 8 removed Button::onRightClick; this reproduces it by routing the
// right-button case out of the clicked() modifier callback. Not final: the
// transport derives its LCD transpose readout from it.
class RightClickButton : public FretButton
{
public:
    explicit RightClickButton (const juce::String& text) : FretButton (text) {}

    std::function<void()> onRightClick;

protected:
    void clicked (const juce::ModifierKeys& mods) override
    {
        if (mods.isRightButtonDown())
        {
            if (onRightClick != nullptr)
                onRightClick();
            return;
        }

        FretButton::clicked (mods);
    }
};


/** Transport row: power, transpose stepper, slot paging, play, metronome,
    BPM drag field, presets, undo/redo and A/B, and the sequencer's
    clear/copy/paste/import. */
class TransportBar final : public juce::Component
{
public:
    TransportBar (FretworkAudioProcessor& processor, juce::AudioProcessorValueTreeState& state);
    ~TransportBar() override;

    /** Called from the editor timer: refreshes the readouts. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class IconButton;
    class BpmField;
    class TransposeField;
    class PresetField;

    int currentSlot() const noexcept;
    void nudgeTranspose (int delta);
    void nudgeSlot (int delta);
    void stepPreset (int delta);
    void showPresetMenu();
    void savePresetAs();
    void importPattern();

    FretworkAudioProcessor& processor;

    juce::Slider transposeProxy, slotProxy, bpmProxy;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transposeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slotAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAttachment;

    juce::AudioParameterInt* transposeParam = nullptr;
    juce::AudioParameterBool* playParam = nullptr;
    juce::AudioParameterInt* slotParam = nullptr;
    juce::AudioParameterFloat* bpmParam = nullptr;

    IconButton* powerButton = nullptr;
    RightClickButton* transposeButton = nullptr;
    FretButton* slotPrevButton = nullptr;
    FretButton* playButton = nullptr;
    FretButton* slotNextButton = nullptr;
    IconButton* metroButton = nullptr;
    BpmField* bpmField = nullptr;
    FretButton* clearButton = nullptr;
    FretButton* copyButton = nullptr;
    FretButton* pasteButton = nullptr;
    FretButton* importButton = nullptr;
    FretButton* presetPrevButton = nullptr;
    PresetField* presetField = nullptr;
    FretButton* presetNextButton = nullptr;
    FretButton* saveButton = nullptr;
    IconButton* undoButton = nullptr;
    IconButton* redoButton = nullptr;
    FretButton* abButton = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> metroAttachment;

    // Paint-only layout from resized(): small-caps captions and the hairline
    // dividers between the control clusters.
    juce::Rectangle<float> transposeCaption, bpmCaption;
    std::array<float, 5> dividers {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace fw::ui

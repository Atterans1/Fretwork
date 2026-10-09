#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/FretLookAndFeel.h"
#include "ui/FretboardView.h"
#include "ui/KeyboardStrip.h"
#include "ui/GuitarBodyView.h"
#include "ui/KnobCell.h"
#include "Params.h"
#include "PluginProcessor.h"
#include "ui/Segmented.h"
#include "ui/SeqGrid.h"
#include "ui/SetupTabs.h"
#include "ui/TransportBar.h"

namespace fw::ui
{
    class ChoiceCell;
    class SlotButton;
}

/** Fixed 1120 x 700 Fretwork editor. Every control is bound through an APVTS
    attachment; a 30 Hz timer mirrors the audio thread's UiSnapshot and applies
    pending keyswitch changes on the message thread. */
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit PluginEditor (FretworkAudioProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    void applyKeyswitchChanges();
    void layoutArticulationBar();

    FretworkAudioProcessor& processor;
    std::unique_ptr<fw::ui::FretLookAndFeel> lookAndFeel;

    fw::ui::FretboardView fretboard;
    fw::ui::SeqGrid       seqGrid;
    fw::ui::SetupTabs     setupTabs;
    fw::ui::KeyboardStrip keyboard;
    fw::ui::TransportBar  transport;

    // ---- region A: strummer knobs
    std::array<std::unique_ptr<fw::ui::KnobCell>, 6> strummerKnobs;

    // ---- region E: slot and feel
    juce::Label slotLabel;
    std::array<std::unique_ptr<fw::ui::SlotButton>, 8> slotButtons;
    std::unique_ptr<fw::ui::KnobCell> humanizeCell;
    std::array<std::unique_ptr<fw::ui::ChoiceCell>, 2> choiceCells;

    // ---- region F: articulation bar
    std::array<std::unique_ptr<fw::ui::Segmented>, 7> articulation;
    std::unique_ptr<fw::ui::GuitarBodyView> guitarView;

    // ---- attachment-backed proxy for the eight slot buttons
    juce::Slider slotProxy;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slotAttachment;
    juce::AudioParameterInt* slotParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

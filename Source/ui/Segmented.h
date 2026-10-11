#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "FretLookAndFeel.h"

namespace fw::ui
{

/** Joined Waves Tune button row (black buttons 1 px apart in a recessed tray,
    the selected one violet) under a small-caps caption.
    Option groups are written through a hidden ComboBox + ComboBoxAttachment
    bound to the choice parameter; two-state groups use a ButtonAttachment. */
class Segmented final : public juce::Component
{
public:
    Segmented();
    ~Segmented() override;

    void setCaption (const juce::String& text);

    /** One button per choice of an AudioParameterChoice. */
    void addChoices (juce::AudioProcessorValueTreeState& state, const char* paramId);

    /** A single two-state segment bound to an AudioParameterBool. */
    void addFlag (juce::AudioProcessorValueTreeState& state, const char* paramId);

    /** Re-reads the bound parameter; call from the editor timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void enablementChanged() override { repaint(); }

private:
    void syncButtons();

    static void styleSegment (FretButton& seg);
    juce::Rectangle<int> trayBounds() const;

    static constexpr int kCaptionH = 18, kTrayInset = 2, kSegmentGap = 1;

    juce::String caption;
    juce::ComboBox proxy;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
    // Held by pointer: FretButton is non-copyable, so it cannot live by value
    // in a std::vector that gets resized.
    std::vector<std::unique_ptr<FretButton>> segments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> flagAttachment;
    juce::AudioParameterChoice* choiceParam = nullptr;
    juce::AudioParameterBool* flagParam = nullptr;
    int selected = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
};

} // namespace fw::ui

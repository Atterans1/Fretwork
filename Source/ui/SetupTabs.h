#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "FretLookAndFeel.h"
#include "PluginProcessor.h"

namespace fw::ui
{

/** BODY / STRINGS / PICKUP / TONE / ARP tab stack: Atlas tab strip above a
    Waves group frame holding the pages. Every control is built once in the
    constructor; switching tabs only flips visibility. */
class SetupTabs final : public juce::Component,
                         private juce::ChangeListener
{
public:
    SetupTabs (FretworkAudioProcessor& processor, juce::AudioProcessorValueTreeState& state);
    ~SetupTabs() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** Selects a page by index, exactly as a click on the tab strip does.
        Public so the headless test can exercise the page-switching logic
        without synthesising a mouse event. */
    void setCurrentPage (int index);

private:
    class ComboCell;
    class MaskCell;

    enum Page { Body = 0, Strings, Pickup, Tone, Arp, NumPages };

    // Grid is 5 columns of kCellW (right edge 317) plus a 6-wide strip row of
    // kMaskW cells (right edge 324), so nothing ever runs off the side. Vertically
    // the tallest page is three rows: two full cells and the compact strip.
    static constexpr int kCellW = 56, kCellH = 64, kCompactH = 40;
    static constexpr int kGapX = 6, kGapY = 4, kGridX = 13, kGridY = 12;
    static constexpr int kMaskW = 48, kMaskGap = 6, kMaskX = 6;
    static constexpr int kTabStrip = 28;

    // Height PluginEditor hands the tab stack (see PluginEditor::resized). Rows
    // must fit under the tab strip; the slack is split evenly top and bottom.
    static constexpr int kPanelH = 228;
    static_assert (kGridY + 2 * (kCellH + kGapY) + kCompactH <= kPanelH - kTabStrip,
                   "setup tab rows must fit the panel content height");

    static juce::Rectangle<int> cellRect (int col, int row, int colSpan = 1);
    static juce::Rectangle<int> maskCellRect (int index);

    void buildBody (juce::Component& page, juce::AudioProcessorValueTreeState& state);
    void buildStrings (juce::Component& page, juce::AudioProcessorValueTreeState& state);
    void buildPickup (juce::Component& page, juce::AudioProcessorValueTreeState& state);
    void buildTone (juce::Component& page, juce::AudioProcessorValueTreeState& state);
    void buildArp (juce::Component& page, juce::AudioProcessorValueTreeState& state);

    FretworkAudioProcessor& processor;
    juce::TabbedButtonBar tabBar { juce::TabbedButtonBar::TabsAtTop };
    std::array<juce::Component*, NumPages> pages {};
    int currentPage = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupTabs)
};

} // namespace fw::ui

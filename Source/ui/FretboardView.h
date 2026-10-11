#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Contracts.h"
#include "FretLookAndFeel.h"
#include "UiSnapshot.h"

namespace fw::ui
{

/** Read-only 6-string fret diagram in a titled LCD well. Sounding frets come
    straight from the audio thread's UiSnapshot; -1 on a string means it is idle. */
class FretboardView final : public juce::Component
{
public:
    FretboardView() = default;

    void setSnapshot (const fw::UiSnapshot* source) noexcept { ui = source; }

    void paint (juce::Graphics&) override;

private:
    static constexpr int kNumFrets = 15;
    const fw::UiSnapshot* ui = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FretboardView)
};

} // namespace fw::ui

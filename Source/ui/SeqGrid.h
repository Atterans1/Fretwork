#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Contracts.h"
#include "FretLookAndFeel.h"
#include "PluginProcessor.h"
#include "SeqStore.h"
#include "UiSnapshot.h"

namespace fw::ui
{

/** 12-row step sequencer. All reads and writes go through SeqStore for the
    slot currently bound to seqSlot; pattern contents are never cached. */
class SeqGrid final : public juce::Component
{
public:
    SeqGrid (FretworkAudioProcessor& processor, juce::AudioProcessorValueTreeState& state);
    ~SeqGrid() override;

    /** Called from the editor timer: relayouts if quantize/signature changed. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    int getStepsPerBar() const noexcept { return stepsPerBar; }

private:
    class LayoutListener;
    friend class LayoutListener;

    void recomputeLayout();
    int currentSlot() const noexcept;
    int colAt (float x) const noexcept;
    int rowAt (float y) const noexcept;
    juce::Rectangle<float> cellRect (int row, int col) const noexcept;
    void resetDrag() noexcept;

    FretworkAudioProcessor& processor;
    juce::AudioProcessorValueTreeState* apvts = nullptr;
    std::unique_ptr<LayoutListener> layoutListener;
    juce::AudioParameterInt* slotParam = nullptr;
    juce::AudioParameterChoice* quantizeParam = nullptr;
    juce::AudioParameterChoice* signatureParam = nullptr;

    int stepsPerBar = 16;
    int stepsPerQuarter_ = 4;
    int cellW = 20;
    int rowH = 18;

    enum class DragMode { none, resize, erase };
    DragMode dragMode = DragMode::none;
    int dragRow = 0, dragStart = 0, dragLen = 1, dragVel = 100;

    // Cell under the pointer, for hover feedback only (-1 = none).
    int hoverRow = -1, hoverCol = -1;

    // What the last paint() showed, so tick() repaints only on change.
    int paintedStep = -1;
    DragMode paintedDrag = DragMode::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqGrid)
};

} // namespace fw::ui

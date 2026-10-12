#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <vector>

namespace fw
{

/** Presets, undo history and A/B for the sound parameters.

    Everything here works on "sound" parameters: all of them except the
    transport switches (Seq Play, Seq Slot, Metronome, Tempo, Power), so undoing
    a tone tweak never stops playback and loading a preset never changes tempo.

    Threading: the gesture callbacks may arrive on any thread and only set
    atomics. Everything else - poll(), undo/redo, A/B, preset loads - runs on
    the message thread. The editor calls poll() from its timer; with no editor
    open there is nothing to record. */
class PatchManager final : private juce::AudioProcessorParameter::Listener
{
public:
    struct Override { const char* id; float value; };   // value in the parameter's own units
    struct Factory  { const char* name; int body; std::vector<Override> overrides; };

    PatchManager (juce::AudioProcessorValueTreeState& state, std::function<void (int)> applyBodyDefaults);
    ~PatchManager() override;

    //==========================================================================
    /** Message thread: commits a history step once a UI gesture has finished
        and the parameters have settled. Call regularly (the editor's timer). */
    void poll();

    /** Forgets undo history and A/B, starting fresh from the current sound.
        Called after a host restores state. */
    void resetHistory();

    bool canUndo() const noexcept { return index > 0; }
    bool canRedo() const noexcept { return index + 1 < (int) history.size(); }
    void undo();
    void redo();

    /** Swaps to the other comparison slot. The first switch copies the current
        sound into it, so A and B start equal. */
    void toggleAB();
    bool isB() const noexcept { return onB; }

    //==========================================================================
    static const std::vector<Factory>& factoryPresets();

    /** Loads a factory preset by index. Returns false for an invalid index. */
    bool loadFactory (int index);
    int currentFactory() const noexcept { return factoryIndex; }
    void setCurrentFactoryIndexOnly (int i) noexcept { factoryIndex = i; }

    /** User presets live as XML files in this directory. */
    static juce::File userPresetDirectory();
    juce::Array<juce::File> userPresets() const;
    bool saveUserPreset (const juce::String& name, juce::File dir = {});
    bool loadUserPreset (const juce::File& file);

    /** Factory presets first, then user presets, for prev/next browsing. */
    int numPresets() const;
    juce::String presetName (int index) const;
    bool loadPreset (int index);
    int currentPreset() const noexcept { return presetIndex; }
    juce::String currentPresetName() const;

    /** True if the given parameter id takes part in snapshots and presets. */
    static bool isSoundParameter (const juce::String& paramId);

private:
    using Snapshot = std::vector<float>;   // normalised values, in sound-parameter order

    Snapshot capture() const;
    void apply (const Snapshot& s);
    void resetToDefaults();
    void commit();
    void setReal (const char* id, float value);

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override;

    juce::AudioProcessorValueTreeState& vts;
    std::function<void (int)> bodyDefaults;
    std::vector<juce::RangedAudioParameter*> params;

    std::vector<Snapshot> history;
    int index = 0;
    static constexpr int kMaxHistory = 100;

    Snapshot slotA, slotB;
    bool onB = false;

    int factoryIndex = 0;
    int presetIndex = 0;
    juce::String userPresetName;

    std::atomic<int>  gesturesActive { 0 };
    std::atomic<bool> pending { false };
    bool applying = false;
};

} // namespace fw

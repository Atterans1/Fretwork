#pragma once

#include "Contracts.h"
#include "Params.h"
#include "SeqStore.h"
#include "UiSnapshot.h"
#include "dsp/GuitarEngine.h"
#include "engine/PerformanceEngine.h"

#include <array>
#include <vector>

//==============================================================================
class FretworkAudioProcessor : public juce::AudioProcessor
{
public:
    FretworkAudioProcessor();
    ~FretworkAudioProcessor() override;

    // ------------------------------------------------------------ AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return { "Fretwork" }; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ----------------------------------------------------------------- Fretwork
    juce::AudioProcessorValueTreeState& getState() noexcept { return vts; }
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }
    fw::SeqStore& getSeqStore() noexcept { return seqStore; }
    const fw::UiSnapshot& getUi() const noexcept { return ui; }

    // Normalised 0..1 value; useful for UI bindings.
    float getParam (const char* paramId) const;

    // The parameter's real value in its own units (Hz, %, dB, ms). Anything
    // feeding the DSP engine must use this, not getParam.
    float getScaled (const char* paramId) const;

    int   getChoiceParam (const char* paramId) const;

    // Message thread only: called from the body ComboBox onChange.
    void applyBodyDefaults (int bodyIndex);

    // Message thread only: the editor calls this once it has written a keyswitch
    // change into the matching choice parameter, so the audio thread stops
    // reporting that group as pending. The processor owns the snapshot, so the
    // editor never has to mutate it through a const reference.
    void clearKeyswitchDirty (int group) noexcept;

private:
    void buildPerfParams (fw::PerfParams& p);
    void buildEngineParams (fw::dsp::EngineParams& p);

    juce::AudioProcessorValueTreeState vts;
    juce::MidiKeyboardState keyboardState;
    fw::SeqStore seqStore;
    fw::UiSnapshot ui;

    fw::PerformanceEngine perf;
    fw::dsp::GuitarEngine engine;

    double currentSampleRate = 44100.0;
    int    maxBlockSize = 512;

    // Preallocated, because processBlock runs on the audio thread.
    juce::MidiBuffer combinedMidi { 128 };
    std::vector<fw::MidiMsg> midiIn;
    std::array<fw::StringEvent, 64> events { };
    std::array<fw::ClickEvent, 32> clicks { };
    std::array<fw::SeqPattern, fw::kNumSeqSlots> seqSlots;

    std::vector<float> renderL_;
    std::vector<float> renderR_;
};

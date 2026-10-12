#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace fw
{

/** MIDI CC to parameter links ("MIDI Learn").

    The message thread arms a parameter with startLearning(); the next
    controller that arrives on the audio thread is linked to it. From then on
    that controller moves the parameter. One controller drives one parameter,
    and a parameter answers to one controller. The map is atomics only, so the
    audio thread never locks. */
class MidiLearn
{
public:
    explicit MidiLearn (juce::AudioProcessorValueTreeState& state);

    // ------------------------------------------------------------- message thread
    void startLearning (const juce::String& paramId);
    void cancelLearning() noexcept { learning = -1; }
    bool isLearning (const juce::String& paramId) const;
    int  ccFor (const juce::String& paramId) const;    // -1 when not linked
    void forget (const juce::String& paramId);

    juce::String serialise() const;                    // "cc=paramId;..."
    void deserialise (const juce::String& text);

    // --------------------------------------------------------------- audio thread
    /** Links the controller if a parameter is waiting to learn, then applies
        the value to whatever parameter the controller is linked to. */
    void handleController (int cc, int value);

private:
    int indexOf (const juce::String& paramId) const;

    std::vector<juce::RangedAudioParameter*> params;
    std::array<std::atomic<int>, 128> map;             // cc -> parameter index, -1 none
    std::atomic<int> learning { -1 };
};

} // namespace fw

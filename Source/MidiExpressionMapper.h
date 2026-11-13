#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <vector>
#include <optional>
#include <cmath>

namespace fw
{

// MIDI message types that can be mapped
enum class MidiMessageType : uint8_t
{
    CC              = 0,  // Control Change (0-127)
    ChannelPressure = 1,  // Channel Aftertouch
    PolyPressure    = 2,  // Polyphonic Aftertouch (per note)
    NoteVelocity    = 3,  // Note On Velocity
    PitchBend       = 4,  // Pitch Bend (0-16383)
    NoteNumber      = 5   // Note Number (0-127)
};

// Mapping curve types
enum class MidiCurveType : uint8_t
{
    Linear        = 0,
    Exponential   = 1,
    Logarithmic   = 2,
    SCurve        = 3,
    Custom        = 5  // User-defined lookup table
};

/** MIDI Expression Mapping - maps any MIDI message to any parameter. */
class MidiExpressionMapper
{
public:
    // A single MIDI-to-parameter mapping
    struct Mapping
    {
        MidiMessageType msgType = MidiMessageType::CC;
        int controllerNumber = 0;        // CC number, note number, etc.
        int midiChannel = 0;             // 0 = omni, 1-16 = specific channel
        juce::String parameterId;        // Target parameter ID
        
        // Input range (MIDI values)
        int inputMin = 0;
        int inputMax = 127;
        
        // Output range (parameter values 0-1)
        float outputMin = 0.0f;
        float outputMax = 1.0f;
        
        // Curve
        MidiCurveType curve = MidiCurveType::Linear;
        std::vector<float> customCurve;  // For custom curve (64 points)
        
        // Options
        bool enabled = true;
        bool bidirectional = false;      // Send MIDI out when parameter changes
        int outputCC = -1;               // CC to send when parameter changes (bidirectional)
        int outputChannel = 0;           // MIDI channel for output (0=same as input)
        
        // MIDI Learn
        bool learning = false;
    };

    explicit MidiExpressionMapper (juce::AudioProcessorValueTreeState& state);

    // ------------------------------------------------------------- message thread
    /** Start MIDI Learn for a parameter - next matching MIDI message creates mapping */
    void startLearning (const juce::String& paramId, MidiMessageType type = MidiMessageType::CC, int channel = 0);
    void cancelLearning() noexcept;
    bool isLearning (const juce::String& paramId) const;
    int ccFor (const juce::String& paramId) const;    // Get CC number for parameter, -1 if not mapped
    void forget (const juce::String& paramId);        // Remove mapping for parameter
    int handleController (int cc, int value);         // Legacy compatibility

    // Manual mapping creation
    Mapping createMapping (const juce::String& paramId, MidiMessageType type, int controllerNumber,
                          int channel = 0, int inMin = 0, int inMax = 127,
                          float outMin = 0.0f, float outMax = 1.0f,
                          MidiCurveType curve = MidiCurveType::Linear);

    void addMapping (const Mapping& mapping);
    void removeMapping (const juce::String& paramId, MidiMessageType type, int controllerNumber, int channel = 0);
    void removeAllMappingsForParameter (const juce::String& paramId);
    void clearAllMappings() noexcept;

    const std::vector<Mapping>& getMappings() const noexcept { return mappings; }
    const Mapping* findMapping (const juce::String& paramId) const;
    std::vector<Mapping> getMappingsForParameter (const juce::String& paramId) const;

    // Serialization
    juce::String serialise() const;
    void deserialise (const juce::String& text);

    // --------------------------------------------------------------- audio thread
    /** Process incoming MIDI message and apply to mapped parameters */
    void handleMidiMessage (const juce::MidiMessage& msg);

    /** Called from parameter change callback to send MIDI out (bidirectional) */
    void onParameterChanged (const juce::String& paramId, float newValue);

    // MIDI output callback - user must implement
    std::function<void(const juce::MidiMessage&)> onMidiOutput;

private:
    struct ParameterRef
    {
        juce::RangedAudioParameter* param = nullptr;
        juce::String paramId;
    };

    int findParameterIndex (const juce::String& paramId) const;
    float applyCurve (float normalizedInput, MidiCurveType curve, const std::vector<float>& customCurve) const;
    float scaleToParameterRange (float normalizedValue, const juce::RangedAudioParameter* param) const;
    int scaleToMidiRange (float normalizedValue, int midiMin, int midiMax) const;

    juce::AudioProcessorValueTreeState& state;
    std::vector<ParameterRef> parameters;
    std::vector<Mapping> mappings;
    mutable juce::CriticalSection mappingLock;   // audio thread only ever try-locks
    
    // For MIDI Learn
    struct LearnState
    {
        std::atomic<int> targetParamIndex { -1 };
        std::atomic<int> expectedType { 0 };
        std::atomic<int> expectedChannel { 0 };
    } learnState;
};

} // namespace fw

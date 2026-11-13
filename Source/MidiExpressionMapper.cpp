#include "MidiExpressionMapper.h"
#include "Params.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fw
{

MidiExpressionMapper::MidiExpressionMapper (juce::AudioProcessorValueTreeState& state_)
    : state (state_)
{
    // Cache all parameters using known parameter IDs from Params.h
    static const char* const allParamIds[] = {
        fw::id::strumTime, fw::id::release, fw::id::strokeVol,
        fw::id::timeOffset, fw::id::separation, fw::id::velOffset,
        fw::id::humanize, fw::id::quantize, fw::id::signature,
        fw::id::bpm, fw::id::transpose, fw::id::hold, fw::id::power,
        fw::id::metronome, fw::id::seqPlay, fw::id::seqSlot,
        fw::id::excitation, fw::id::damping, fw::id::transition,
        fw::id::harmonic, fw::id::tremolo, fw::id::mode, fw::id::voicing,
        fw::id::glideTime, fw::id::bendAmount, fw::id::bendTime,
        fw::id::wheelRange, fw::id::vibRate, fw::id::vibDepth,
        fw::id::tremRate, fw::id::arpStrings, fw::id::arpMask0,
        fw::id::arpMask1, fw::id::arpMask2, fw::id::arpMask3,
        fw::id::arpMask4, fw::id::arpMask5, fw::id::arpOrder,
        fw::id::arpRate, fw::id::arpGate, fw::id::arpSwing,
        fw::id::body, fw::id::tuning, fw::id::capo, fw::id::tune0,
        fw::id::tune1, fw::id::tune2, fw::id::tune3, fw::id::tune4,
        fw::id::tune5, fw::id::bodySize, fw::id::bodyRes, fw::id::topDamp,
        fw::id::micBlend, fw::id::material, fw::id::stringSet,
        fw::id::guitarType, fw::id::courseLevel, fw::id::stringAge,
        fw::id::stiffness, fw::id::pickPos, fw::id::pickHard,
        fw::id::sympathetic, fw::id::fretNoise, fw::id::sustain,
        fw::id::pickupType, fw::id::pickupSel, fw::id::pickupHeight,
        fw::id::pickupTone, fw::id::pickupOut, fw::id::tone,
        fw::id::drive, fw::id::room, fw::id::roomType, fw::id::width,
        fw::id::output, fw::id::ampModel, fw::id::ampGain, fw::id::ampBass,
        fw::id::ampMid, fw::id::ampTreble, fw::id::ampLevel, fw::id::cab,
        fw::id::chorusMix, fw::id::chorusRate, fw::id::delayMix,
        fw::id::delayTime, fw::id::delayFeedback,
        nullptr  // terminator
    };
    
    for (const char* paramId : allParamIds)
    {
        if (paramId == nullptr) break;
        
        auto* p = state.getParameter(paramId);
        if (p == nullptr) continue;
        
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter(paramId)))
        {
            parameters.push_back ({ rp, paramId });
        }
    }
    
    // Learning appends from the audio thread; reserve so that never allocates.
    mappings.reserve (256);

    // Initialize learn state
    learnState.targetParamIndex = -1;
    learnState.expectedType = 0;
    learnState.expectedChannel = 0;
}

int MidiExpressionMapper::findParameterIndex (const juce::String& paramId) const
{
    for (size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i].paramId == paramId)
            return (int) i;
    return -1;
}

// ------------------------------------------------------------- message thread

void MidiExpressionMapper::startLearning (const juce::String& paramId, MidiMessageType type, int channel)
{
    const int idx = findParameterIndex (paramId);
    if (idx >= 0)
    {
        learnState.targetParamIndex = idx;
        learnState.expectedType = (int) type;
        learnState.expectedChannel = channel;
    }
}

void MidiExpressionMapper::cancelLearning() noexcept
{
    learnState.targetParamIndex = -1;
}

bool MidiExpressionMapper::isLearning (const juce::String& paramId) const
{
    const int idx = findParameterIndex (paramId);
    return idx >= 0 && idx == learnState.targetParamIndex.load();
}


int MidiExpressionMapper::ccFor (const juce::String& paramId) const
{
    const juce::ScopedLock sl (mappingLock);
    for (const auto& m : mappings)
        if (m.enabled && m.msgType == MidiMessageType::CC && m.parameterId == paramId)
            return m.controllerNumber;
    return -1;
}

void MidiExpressionMapper::forget (const juce::String& paramId)
{
    removeAllMappingsForParameter (paramId);
}

int MidiExpressionMapper::handleController (int cc, int value)
{
    if (cc < 0 || cc > 127)
        return -1;

    handleMidiMessage (juce::MidiMessage::controllerEvent (1, cc, juce::jlimit (0, 127, value)));

    const juce::ScopedLock sl (mappingLock);
    for (const auto& m : mappings)
        if (m.enabled && m.msgType == MidiMessageType::CC && m.controllerNumber == cc)
            return 1;
    return 0;
}


MidiExpressionMapper::Mapping MidiExpressionMapper::createMapping (
    const juce::String& paramId,
    MidiMessageType type,
    int controllerNumber,
    int channel,
    int inMin,
    int inMax,
    float outMin,
    float outMax,
    MidiCurveType curve)
{
    Mapping m;
    m.parameterId = paramId;
    m.msgType = type;
    m.controllerNumber = controllerNumber;
    m.midiChannel = channel;
    m.inputMin = inMin;
    m.inputMax = inMax;
    m.outputMin = outMin;
    m.outputMax = outMax;
    m.curve = curve;
    return m;
}

void MidiExpressionMapper::addMapping (const Mapping& mapping)
{
    const juce::ScopedLock sl (mappingLock);
    // Remove any existing mapping for same param+type+controller+channel
    removeMapping (mapping.parameterId, mapping.msgType, mapping.controllerNumber, mapping.midiChannel);
    
    const int paramIdx = findParameterIndex (mapping.parameterId);
    if (paramIdx < 0)
        return;
    
    mappings.push_back (mapping);
}

void MidiExpressionMapper::removeMapping (const juce::String& paramId, MidiMessageType type, int controllerNumber, int channel)
{
    const juce::ScopedLock sl (mappingLock);
    mappings.erase (
        std::remove_if (mappings.begin(), mappings.end(),
            [&] (const Mapping& m)
            {
                return m.parameterId == paramId
                    && m.msgType == type
                    && m.controllerNumber == controllerNumber
                    && m.midiChannel == channel;
            }),
        mappings.end());
}

void MidiExpressionMapper::removeAllMappingsForParameter (const juce::String& paramId)
{
    const juce::ScopedLock sl (mappingLock);
    mappings.erase (
        std::remove_if (mappings.begin(), mappings.end(),
            [&] (const Mapping& m) { return m.parameterId == paramId; }),
        mappings.end());
}

void MidiExpressionMapper::clearAllMappings() noexcept
{
    const juce::ScopedLock sl (mappingLock);
    mappings.clear();
}

const MidiExpressionMapper::Mapping* MidiExpressionMapper::findMapping (const juce::String& paramId) const
{
    for (const auto& m : mappings)
        if (m.parameterId == paramId && m.enabled)
            return &m;
    return nullptr;
}

std::vector<MidiExpressionMapper::Mapping> MidiExpressionMapper::getMappingsForParameter (const juce::String& paramId) const
{
    const juce::ScopedLock sl (mappingLock);
    std::vector<Mapping> result;
    for (const auto& m : mappings)
        if (m.parameterId == paramId && m.enabled)
            result.push_back (m);
    return result;
}

// Serialization. Current format: one mapping per "|" entry, 13 comma fields.
// Projects saved by older builds used "cc=paramId;cc=paramId"; those still load.
juce::String MidiExpressionMapper::serialise() const
{
    const juce::ScopedLock sl (mappingLock);
    juce::StringArray links;
    for (const auto& m : mappings)
    {
        if (! m.enabled) continue;

        juce::String s;
        s << (int) m.msgType << ","
          << m.controllerNumber << ","
          << m.midiChannel << ","
          << m.parameterId << ","
          << m.inputMin << ","
          << m.inputMax << ","
          << m.outputMin << ","
          << m.outputMax << ","
          << (int) m.curve << ","
          << (m.enabled ? "1" : "0") << ","
          << (m.bidirectional ? "1" : "0") << ","
          << m.outputCC << ","
          << m.outputChannel;
        links.add (s);
    }
    return links.joinIntoString ("|");
}

void MidiExpressionMapper::deserialise (const juce::String& text)
{
    const juce::ScopedLock sl (mappingLock);
    mappings.clear();

    for (const auto& link : juce::StringArray::fromTokens (text, "|;", ""))
    {
        if (link.containsChar (','))
        {
            const auto parts = juce::StringArray::fromTokens (link, ",", "");
            if (parts.size() < 13) continue;

            Mapping m;
            m.msgType = (MidiMessageType) parts[0].getIntValue();
            m.controllerNumber = parts[1].getIntValue();
            m.midiChannel = parts[2].getIntValue();
            m.parameterId = parts[3];
            m.inputMin = parts[4].getIntValue();
            m.inputMax = parts[5].getIntValue();
            m.outputMin = parts[6].getFloatValue();
            m.outputMax = parts[7].getFloatValue();
            m.curve = (MidiCurveType) parts[8].getIntValue();
            m.enabled = parts[9].getIntValue() != 0;
            m.bidirectional = parts[10].getIntValue() != 0;
            m.outputCC = parts[11].getIntValue();
            m.outputChannel = parts[12].getIntValue();

            if (findParameterIndex (m.parameterId) >= 0)
                mappings.push_back (m);
        }
        else if (link.containsChar ('='))
        {
            const int cc = link.upToFirstOccurrenceOf ("=", false, false).getIntValue();
            const auto id = link.fromFirstOccurrenceOf ("=", false, false);
            if (cc >= 0 && cc < 128 && findParameterIndex (id) >= 0)
            {
                Mapping m;
                m.parameterId = id;
                m.controllerNumber = cc;
                mappings.push_back (m);
            }
        }
    }
}


// --------------------------------------------------------------- audio thread

void MidiExpressionMapper::handleMidiMessage (const juce::MidiMessage& msg)
{
    if (!msg.isController() && !msg.isChannelPressure() && !msg.isAftertouch() 
        && !msg.isPitchWheel() && !msg.isNoteOn())
        return;
    
    const juce::ScopedTryLock tryLock (mappingLock);
    if (! tryLock.isLocked())
        return;

    MidiMessageType type = MidiMessageType::CC;
    int controllerNumber = 0;
    int value = 0;
    int channel = msg.getChannel();
    
    if (msg.isController())
    {
        type = MidiMessageType::CC;
        controllerNumber = msg.getControllerNumber();
        value = msg.getControllerValue();
    }
    else if (msg.isChannelPressure())
    {
        type = MidiMessageType::ChannelPressure;
        controllerNumber = 0;
        value = msg.getChannelPressureValue();
    }
    else if (msg.isAftertouch())
    {
        type = MidiMessageType::PolyPressure;
        controllerNumber = msg.getNoteNumber();
        value = msg.getAfterTouchValue();
    }
    else if (msg.isPitchWheel())
    {
        type = MidiMessageType::PitchBend;
        controllerNumber = 0;
        value = msg.getPitchWheelValue(); // 0-16383
    }
    else if (msg.isNoteOn())
    {
        type = MidiMessageType::NoteNumber;
        controllerNumber = msg.getNoteNumber();
        value = msg.getVelocity();
    }
    else
    {
        return;
    }
    
    // MIDI Learn: if we're learning, capture this message
    const int targetIdx = learnState.targetParamIndex.load();
    if (targetIdx >= 0)
    {
        const int expectedType = learnState.expectedType.load();
        const int expectedChannel = learnState.expectedChannel.load();
        
        if (expectedType == (int) type
            && (expectedChannel == 0 || expectedChannel == channel))
        {
            const auto targetId = parameters[(size_t) targetIdx].paramId;
            mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                [&] (const Mapping& old)
                                {
                                    const bool sameKnob = old.parameterId == targetId && old.msgType == type;
                                    const bool sameSource = old.msgType == type && old.controllerNumber == controllerNumber
                                                         && (old.midiChannel == 0 || expectedChannel == 0 || old.midiChannel == expectedChannel);
                                    return sameKnob || sameSource;
                                }),
                            mappings.end());

            Mapping m;
            m.parameterId = targetId;
            m.msgType = type;
            m.controllerNumber = controllerNumber;
            m.midiChannel = expectedChannel;
            m.inputMin = 0;
            m.inputMax = (type == MidiMessageType::PitchBend) ? 16383 : 127;
            m.enabled = true;
            mappings.push_back (m);

            learnState.targetParamIndex = -1; // Cancel learn mode
        }
    }
    
    // Apply to existing mappings
    for (auto& m : mappings)
    {
        if (!m.enabled) continue;
        if (m.msgType != type) continue;
        if (m.controllerNumber != controllerNumber) continue;
        if (m.midiChannel != 0 && m.midiChannel != channel) continue;
        
        // Normalize input
        float normalized = 0.0f;
        if (m.inputMax != m.inputMin)
        {
            normalized = (float) (value - m.inputMin) / (float) (m.inputMax - m.inputMin);
            normalized = juce::jlimit (0.0f, 1.0f, normalized);
        }
        
        // Apply curve
        float curved = applyCurve (normalized, m.curve, m.customCurve);
        
        // Scale to output range
        float output = m.outputMin + curved * (m.outputMax - m.outputMin);
        
        // Apply to parameter
        const int paramIdx = findParameterIndex (m.parameterId);
        if (paramIdx >= 0 && paramIdx < (int) parameters.size())
        {
            auto* param = parameters[paramIdx].param;
            param->setValueNotifyingHost (output);
        }
    }
}

void MidiExpressionMapper::onParameterChanged (const juce::String& paramId, float newValue)
{
    // Find bidirectional mappings for this parameter
    for (const auto& m : mappings)
    {
        if (!m.enabled || !m.bidirectional) continue;
        if (m.parameterId != paramId) continue;
        if (m.outputCC < 0) continue;
        
        // Scale parameter value back to MIDI range
        float normalized = (newValue - m.outputMin) / (m.outputMax - m.outputMin);
        normalized = juce::jlimit (0.0f, 1.0f, normalized);
        
        int midiValue = m.inputMin + (int) (normalized * (m.inputMax - m.inputMin));
        midiValue = juce::jlimit (m.inputMin, m.inputMax, midiValue);
        
        // Create MIDI output message
        juce::MidiMessage msg;
        switch (m.msgType)
        {
            case MidiMessageType::CC:
                msg = juce::MidiMessage::controllerEvent (m.outputChannel > 0 ? m.outputChannel : m.midiChannel, m.outputCC, midiValue);
                break;
            case MidiMessageType::PitchBend:
                // Pitch bend needs special handling (14-bit)
                msg = juce::MidiMessage::pitchWheel (m.outputChannel > 0 ? m.outputChannel : m.midiChannel, midiValue << 7); // Scale to 14-bit
                break;
            default:
                continue; // Other types not supported for output yet
        }
        
        if (onMidiOutput)
            onMidiOutput (msg);
    }
}

float MidiExpressionMapper::applyCurve (float input, MidiCurveType curve, const std::vector<float>& customCurve) const
{
    input = juce::jlimit (0.0f, 1.0f, input);
    
    switch (curve)
    {
        case MidiCurveType::Linear:
            return input;
            
        case MidiCurveType::Exponential:
            return input == 0.0f ? 0.0f : std::pow (input, 2.0f);
            
        case MidiCurveType::Logarithmic:
            return input == 0.0f ? 0.0f : std::log (1.0f + input * (std::exp(1.0f) - 1.0f));
            
        case MidiCurveType::SCurve:
            return 3.0f * input * input - 2.0f * input * input * input;
            
        case MidiCurveType::Custom:
            if (!customCurve.empty())
            {
                const float index = input * (float) (customCurve.size() - 1);
                const int i0 = (int) std::floor (index);
                const int i1 = std::min (i0 + 1, (int) customCurve.size() - 1);
                const float t = index - i0;
                return customCurve[i0] * (1.0f - t) + customCurve[i1] * t;
            }
            return input;
            
        default:
            return input;
    }
}

float MidiExpressionMapper::scaleToParameterRange (float normalizedValue, const juce::RangedAudioParameter* param) const
{
    return param->convertFrom0to1 (normalizedValue);
}

int MidiExpressionMapper::scaleToMidiRange (float normalizedValue, int midiMin, int midiMax) const
{
    return midiMin + (int) (normalizedValue * (midiMax - midiMin));
}

} // namespace fw

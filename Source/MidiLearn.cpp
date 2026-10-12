#include "MidiLearn.h"

namespace fw
{

MidiLearn::MidiLearn (juce::AudioProcessorValueTreeState& state)
{
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            params.push_back (rp);

    for (auto& m : map)
        m = -1;
}

int MidiLearn::indexOf (const juce::String& paramId) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i]->getParameterID() == paramId)
            return (int) i;
    return -1;
}

void MidiLearn::startLearning (const juce::String& paramId)
{
    learning = indexOf (paramId);
}

bool MidiLearn::isLearning (const juce::String& paramId) const
{
    const int i = learning.load();
    return i >= 0 && i == indexOf (paramId);
}

int MidiLearn::ccFor (const juce::String& paramId) const
{
    const int i = indexOf (paramId);
    if (i < 0)
        return -1;
    for (int cc = 0; cc < 128; ++cc)
        if (map[(size_t) cc].load() == i)
            return cc;
    return -1;
}

void MidiLearn::forget (const juce::String& paramId)
{
    const int i = indexOf (paramId);
    for (auto& m : map)
        if (m.load() == i)
            m = -1;
}

void MidiLearn::handleController (int cc, int value)
{
    if (cc < 0 || cc > 127)
        return;

    if (const int target = learning.exchange (-1); target >= 0)
    {
        for (auto& m : map)            // a parameter answers to one controller
            if (m.load() == target)
                m = -1;
        map[(size_t) cc] = target;
    }

    const int i = map[(size_t) cc].load();
    if (i >= 0 && i < (int) params.size())
        params[(size_t) i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) value / 127.0f));
}

juce::String MidiLearn::serialise() const
{
    juce::StringArray links;
    for (int cc = 0; cc < 128; ++cc)
        if (const int i = map[(size_t) cc].load(); i >= 0 && i < (int) params.size())
            links.add (juce::String (cc) + "=" + params[(size_t) i]->getParameterID());
    return links.joinIntoString (";");
}

void MidiLearn::deserialise (const juce::String& text)
{
    for (auto& m : map)
        m = -1;

    for (const auto& link : juce::StringArray::fromTokens (text, ";", {}))
    {
        const int cc = link.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const int i = indexOf (link.fromFirstOccurrenceOf ("=", false, false));
        if (cc >= 0 && cc < 128 && i >= 0 && link.containsChar ('='))
            map[(size_t) cc] = i;
    }
}

} // namespace fw

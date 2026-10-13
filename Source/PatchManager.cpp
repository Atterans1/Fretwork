#include "PatchManager.h"

#include "Params.h"

namespace fw
{

namespace
{
    // Choice values below are indices into the parameter's choice list.
    const std::vector<PatchManager::Factory> kFactory =
    {
        // ---- Dreadnought Steel
        { "Campfire Strum",     0, { { id::mode, 1 }, { id::strumTime, 35 }, { id::humanize, 20 }, { id::room, 20 } } },
        { "Fingerstyle Folk",   0, { { id::excitation, 1 }, { id::pickHard, 30 }, { id::pickPos, 20 }, { id::sustain, 120 }, { id::room, 15 } } },
        { "Palm-Muted Pop",     0, { { id::mode, 1 }, { id::damping, 1 }, { id::strumTime, 18 }, { id::humanize, 10 } } },
        // ---- Classical Nylon
        { "Bossa Nova",         1, { { id::mode, 1 }, { id::excitation, 1 }, { id::strumTime, 45 }, { id::humanize, 25 }, { id::tone, legacyToneHz (7000) }, { id::room, 18 } } },
        { "Classical Arpeggio", 1, { { id::mode, 2 }, { id::excitation, 1 }, { id::arpOrder, 2 }, { id::arpRate, 1 }, { id::sustain, 130 }, { id::room, 25 } } },
        { "Nylon Lead",         1, { { id::transition, 2 }, { id::vibDepth, 30 }, { id::room, 22 } } },
        // ---- Solid Electric
        { "Clean Funk Rhythm",  2, { { id::mode, 1 }, { id::strumTime, 10 }, { id::humanize, 15 }, { id::tone, legacyToneHz (11000) } } },
        { "Crunch Rhythm",      2, { { id::mode, 1 }, { id::pickupType, 1 }, { id::pickupSel, 0 }, { id::drive, 45 }, { id::strumTime, 15 } } },
        { "Blues Lead",         2, { { id::pickupSel, 2 }, { id::drive, 25 }, { id::transition, 3 }, { id::bendAmount, 1 }, { id::vibDepth, 35 }, { id::sustain, 140 } } },
        // ---- Semi-Hollow
        { "Jazz Box Comping",   3, { { id::mode, 1 }, { id::excitation, 2 }, { id::strumTime, 25 }, { id::tone, legacyToneHz (5500) } } },
        { "Warm Semi Strum",    3, { { id::mode, 1 }, { id::strumTime, 30 }, { id::drive, 10 }, { id::room, 25 } } },
        { "Rockabilly Slap",    3, { { id::pickupSel, 3 }, { id::damping, 1 }, { id::drive, 20 }, { id::room, 35 } } },

        // ---- Amp presets. Appended, not grouped above: hosts save the program
        // number, so inserting would point old projects at a different name.
        // The menu groups by body, so these still list under their guitar.
        { "Crunch Amp",         2, { { id::mode, 1 }, { id::pickupType, 1 }, { id::strumTime, 15 },
                                     { id::ampModel, 2 }, { id::cab, 2 }, { id::ampGain, 45 } } },
        { "Lead Stack",         2, { { id::pickupType, 1 }, { id::pickupSel, 2 }, { id::transition, 3 }, { id::vibDepth, 35 }, { id::sustain, 140 },
                                     { id::ampModel, 3 }, { id::cab, 3 }, { id::ampGain, 65 }, { id::ampMid, 60 },
                                     { id::delayMix, 18 }, { id::delayTime, 420 }, { id::delayFeedback, 30 } } },
        { "Chorus Clean",       2, { { id::mode, 1 }, { id::strumTime, 12 }, { id::room, 20 },
                                     { id::ampModel, 1 }, { id::cab, 1 }, { id::ampGain, 30 },
                                     { id::chorusMix, 45 }, { id::chorusRate, 0.7f } } },
        { "Jazz Amp",           3, { { id::excitation, 2 }, { id::room, 15 },
                                     { id::ampModel, 1 }, { id::cab, 2 }, { id::ampGain, 25 }, { id::ampBass, 60 }, { id::ampTreble, 35 }, { id::ampLevel, -4 } } },
    };

    const char* const kTransportIds[] = { id::seqPlay, id::seqSlot, id::metronome, id::bpm, id::power };
}

//==============================================================================
PatchManager::PatchManager (juce::AudioProcessorValueTreeState& state, std::function<void (int)> applyBodyDefaults)
    : vts (state), bodyDefaults (std::move (applyBodyDefaults))
{
    for (auto* p : vts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isSoundParameter (rp->getParameterID()))
            {
                params.push_back (rp);
                rp->addListener (this);
            }

    history.push_back (capture());
}

PatchManager::~PatchManager()
{
    for (auto* p : params)
        p->removeListener (this);
}

bool PatchManager::isSoundParameter (const juce::String& paramId)
{
    for (const char* t : kTransportIds)
        if (paramId == t)
            return false;
    return true;
}

PatchManager::Snapshot PatchManager::capture() const
{
    Snapshot s;
    s.reserve (params.size());
    for (auto* p : params)
        s.push_back (p->getValue());
    return s;
}

void PatchManager::apply (const Snapshot& s)
{
    if (s.size() != params.size())
        return;

    const juce::ScopedValueSetter<bool> guard (applying, true);
    for (size_t i = 0; i < params.size(); ++i)
        if (std::abs (params[i]->getValue() - s[i]) > 1.0e-6f)
            params[i]->setValueNotifyingHost (s[i]);
}

void PatchManager::setReal (const char* paramId, float value)
{
    if (auto* p = vts.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PatchManager::resetToDefaults()
{
    for (auto* p : params)
        p->setValueNotifyingHost (p->getDefaultValue());
}

//==============================================================================
void PatchManager::parameterGestureChanged (int, bool starting)
{
    if (applying)
        return;

    if (starting)
        ++gesturesActive;
    else
    {
        gesturesActive = juce::jmax (0, gesturesActive.load() - 1);
        pending = true;
    }
}

void PatchManager::poll()
{
    // The body ComboBox applies its defaults right after its own gesture ends,
    // so a gesture end only marks the state dirty; the step is committed on a
    // later poll, once everything that gesture triggered has landed.
    if (pending.load() && gesturesActive.load() == 0)
    {
        pending = false;
        commit();
    }
}

void PatchManager::commit()
{
    auto now = capture();
    if (now == history[(size_t) index])
        return;

    history.erase (history.begin() + index + 1, history.end());
    history.push_back (std::move (now));

    if ((int) history.size() > kMaxHistory)
        history.erase (history.begin());

    index = (int) history.size() - 1;
}

void PatchManager::resetHistory()
{
    history.assign (1, capture());
    index = 0;
    slotA.clear();
    slotB.clear();
    onB = false;
    pending = false;
}

void PatchManager::undo()
{
    commit();   // an edit made since the last poll is not lost
    if (! canUndo())
        return;
    apply (history[(size_t) --index]);
}

void PatchManager::redo()
{
    if (! canRedo())
        return;
    apply (history[(size_t) ++index]);
}

void PatchManager::toggleAB()
{
    commit();
    auto& active = onB ? slotB : slotA;
    auto& other  = onB ? slotA : slotB;

    active = capture();
    if (other.empty())
        other = active;

    onB = ! onB;
    apply (other);
    commit();
}

//==============================================================================
const std::vector<PatchManager::Factory>& PatchManager::factoryPresets()
{
    return kFactory;
}

bool PatchManager::loadFactory (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) kFactory.size()))
        return false;

    commit();
    {
        const juce::ScopedValueSetter<bool> guard (applying, true);
        const auto& f = kFactory[(size_t) i];
        resetToDefaults();
        setReal (id::body, (float) f.body);
        if (bodyDefaults != nullptr)
            bodyDefaults (f.body);
        for (const auto& o : f.overrides)
            setReal (o.id, o.value);
    }

    factoryIndex = i;
    presetIndex = i;
    userPresetName.clear();
    commit();
    return true;
}

juce::File PatchManager::userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Fretwork").getChildFile ("Presets");
}

juce::Array<juce::File> PatchManager::userPresets() const
{
    auto files = userPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.fwpreset");
    files.sort();
    return files;
}

bool PatchManager::saveUserPreset (const juce::String& name, juce::File dir)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;

    if (dir == juce::File())
        dir = userPresetDirectory();
    if (! dir.createDirectory())
        return false;

    juce::XmlElement xml ("FretworkPreset");
    xml.setAttribute ("name", name.trim());
    xml.setAttribute ("toneMapping", kToneMappingVersion);
    for (auto* p : params)
    {
        auto* e = xml.createNewChildElement ("P");
        e->setAttribute ("id", p->getParameterID());
        e->setAttribute ("v", (double) p->convertFrom0to1 (p->getValue()));
    }

    if (! xml.writeTo (dir.getChildFile (clean + ".fwpreset")))
        return false;

    userPresetName = name.trim();
    return true;
}

bool PatchManager::loadUserPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("FretworkPreset"))
        return false;

    commit();
    {
        const juce::ScopedValueSetter<bool> guard (applying, true);
        resetToDefaults();   // parameters missing from an older file fall back to defaults
        const bool legacyTone = xml->getIntAttribute ("toneMapping", 1) < kToneMappingVersion;
        for (auto* e : xml->getChildWithTagNameIterator ("P"))
        {
            const auto pid = e->getStringAttribute ("id");
            if (! isSoundParameter (pid))
                continue;
            double v = e->getDoubleAttribute ("v");
            if (legacyTone && pid == id::tone)
                v = legacyToneHz (v);
            if (auto* p = vts.getParameter (pid))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
        }
    }

    userPresetName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    commit();
    return true;
}

int PatchManager::numPresets() const
{
    return (int) kFactory.size() + userPresets().size();
}

juce::String PatchManager::presetName (int i) const
{
    if (juce::isPositiveAndBelow (i, (int) kFactory.size()))
        return kFactory[(size_t) i].name;

    const auto users = userPresets();
    const int u = i - (int) kFactory.size();
    return juce::isPositiveAndBelow (u, users.size()) ? users[u].getFileNameWithoutExtension() : juce::String();
}

bool PatchManager::loadPreset (int i)
{
    if (juce::isPositiveAndBelow (i, (int) kFactory.size()))
        return loadFactory (i);

    const auto users = userPresets();
    const int u = i - (int) kFactory.size();
    if (! juce::isPositiveAndBelow (u, users.size()) || ! loadUserPreset (users[u]))
        return false;

    presetIndex = i;
    return true;
}

juce::String PatchManager::currentPresetName() const
{
    return userPresetName.isNotEmpty() ? userPresetName : juce::String (kFactory[(size_t) factoryIndex].name);
}

} // namespace fw

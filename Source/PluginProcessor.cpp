#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/ModelTables.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Base open-string pitch per tuning preset (low E first). Transpose, capo
    // and fine tune are folded in on top.
    constexpr float kTunings[12][6] =
    {
        { 40.f, 45.f, 50.f, 55.f, 59.f, 64.f },   // Standard
        { 38.f, 45.f, 50.f, 55.f, 59.f, 64.f },   // Drop D
        { 38.f, 45.f, 50.f, 55.f, 57.f, 62.f },   // DADGAD
        { 38.f, 43.f, 50.f, 55.f, 59.f, 62.f },   // Open G
        { 38.f, 50.f, 54.f, 57.f, 62.f, 66.f },   // Open D
        { 36.f, 43.f, 48.f, 53.f, 57.f, 62.f },   // Drop C: C2 G2 C3 F3 A3 D4
        { 35.f, 42.f, 47.f, 52.f, 56.f, 61.f },   // Drop B: B1 F#2 B2 E3 G#3 C#4
        { 39.f, 44.f, 49.f, 54.f, 58.f, 63.f },   // Eb Standard: half step below standard
        { 38.f, 43.f, 50.f, 55.f, 58.f, 62.f },   // Open Gm: D G D G Bb D
        { 38.f, 45.f, 50.f, 53.f, 57.f, 62.f },   // Open Dm: D A D F A D
        { 52.f, 57.f, 62.f, 55.f, 59.f, 64.f },   // Nashville: EAD courses up an octave, GBE standard
        { 36.f, 43.f, 48.f, 55.f, 57.f, 64.f }    // C6: C2 G2 C3 G3 A3 E4
    };

    // 7-string standard: low B1 prepended; the six-string courses keep their
    // pitches, so every existing tuning row stays valid for six-string use.
    constexpr float kTuning7[7] = { 35.f, 40.f, 45.f, 50.f, 55.f, 59.f, 64.f };

    constexpr const char* stringGainIds[6] =
    {
        fw::id::stringGain0, fw::id::stringGain1, fw::id::stringGain2,
        fw::id::stringGain3, fw::id::stringGain4, fw::id::stringGain5
    };

    constexpr const char* tuningIds[6] =
    {
        fw::id::tune0, fw::id::tune1, fw::id::tune2,
        fw::id::tune3, fw::id::tune4, fw::id::tune5
    };
}

//==============================================================================
FretworkAudioProcessor::FretworkAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output",
                                                    juce::AudioChannelSet::stereo(),
                                                    true))
    , vts (*this, nullptr, juce::Identifier ("Fretwork"), fw::createLayout())
{
    midiIn.resize (128);
    patches = std::make_unique<fw::PatchManager> (vts, [this] (int body) { applyBodyDefaults (body); });
    midiLearn = std::make_unique<fw::MidiExpressionMapper> (vts);
}

//==============================================================================
int FretworkAudioProcessor::getNumPrograms()
{
    return (int) fw::PatchManager::factoryPresets().size();
}

int FretworkAudioProcessor::getCurrentProgram()
{
    return patches->currentFactory();
}

void FretworkAudioProcessor::setCurrentProgram (int index)
{
    // Hosts re-send the current program on project load. Reloading it would
    // overwrite the restored sound with the factory preset, so a request for
    // the program that is already current does nothing.
    if (index != patches->currentFactory())
        patches->loadFactory (index);
}

const juce::String FretworkAudioProcessor::getProgramName (int index)
{
    const auto& f = fw::PatchManager::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) f.size()) ? juce::String (f[(size_t) index].name) : juce::String();
}

FretworkAudioProcessor::~FretworkAudioProcessor() = default;

//==============================================================================
void FretworkAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize = std::max (64, samplesPerBlock);

    perf.prepare (currentSampleRate);
    engine.prepare (currentSampleRate, maxBlockSize);

    renderL_.assign ((std::size_t) maxBlockSize, 0.f);
    renderR_.assign ((std::size_t) maxBlockSize, 0.f);
}

void FretworkAudioProcessor::releaseResources()
{
}

//==============================================================================
bool FretworkAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled();
}

//==============================================================================
void FretworkAudioProcessor::buildPerfParams (fw::PerfParams& p)
{
    p.strumTime  = getScaled (fw::id::strumTime);
    p.timeOffset = getScaled (fw::id::timeOffset);
    p.separation = getScaled (fw::id::separation) * 0.01f;
    p.velOffset  = getScaled (fw::id::velOffset);
    // strokeVol is already 0..1; scaling it by 0.01 made strum, arp and
    // sequencer notes ~1% velocity, i.e. inaudible.
    p.strokeVol  = getScaled (fw::id::strokeVol);
    p.humanize   = getScaled (fw::id::humanize) * 0.01f;
    p.release    = getScaled (fw::id::release);

    p.quantize  = getChoiceParam (fw::id::quantize);
    p.signature = getChoiceParam (fw::id::signature);
    p.bpm       = getScaled (fw::id::bpm);

    p.transpose = (int) std::lround (getScaled (fw::id::transpose));
    p.wheelRange = (int) std::lround (getScaled (fw::id::wheelRange));
    p.atBendRange = getScaled (fw::id::bendAmount);
    p.hold      = getParam (fw::id::hold) > 0.5f;
    p.mode      = (fw::PlayMode) getChoiceParam (fw::id::mode);

    p.art.exc   = (fw::Excitation) getChoiceParam (fw::id::excitation);
    p.art.damp  = (fw::Damping)    getChoiceParam (fw::id::damping);
    p.art.trans = (fw::Transition) getChoiceParam (fw::id::transition);
    p.art.harm  = (fw::Harmonic)   getChoiceParam (fw::id::harmonic);
    p.art.tremolo = getParam (fw::id::tremolo) > 0.5f;

    p.arpStrings = (int) std::lround (getScaled (fw::id::arpStrings));
    for (int s = 0; s < fw::kNumStrings; ++s)
    {
        static constexpr const char* maskIds[6] =
        {
            fw::id::arpMask0, fw::id::arpMask1, fw::id::arpMask2,
            fw::id::arpMask3, fw::id::arpMask4, fw::id::arpMask5
        };
        p.arpMask[s] = getParam (maskIds[s]) > 0.5f;
    }

    p.arpOrder = getChoiceParam (fw::id::arpOrder);
    p.voicingMode = getChoiceParam (fw::id::voicing);
    p.arpRate  = getChoiceParam (fw::id::arpRate);
    p.arpGate  = getScaled (fw::id::arpGate);
    p.arpSwing = getScaled (fw::id::arpSwing);

    p.tremRate = getScaled (fw::id::tremRate);

    const int tuning = std::clamp (getChoiceParam (fw::id::tuning), 0, 11);
    const float capo = getScaled (fw::id::capo);

    // A 7-string guitar prepends the low B to the chosen six-string tuning and
    // runs seven courses; every other type uses the tuning row as-is.
    const bool seven = getChoiceParam (fw::id::guitarType) == (int) fw::GuitarType::SevenString;
    p.activeStringCount = seven ? fw::kMaxStrings : fw::kNumStrings;

    for (int s = 0; s < fw::kMaxStrings; ++s)
    {
        const float base = seven ? kTuning7[s]
                                 : (s < fw::kNumStrings ? kTunings[tuning][s] : 0.f);
        p.open[s] = base + capo + (s < fw::kNumStrings ? getScaled (tuningIds[s]) * 0.01f : 0.f);
    }

    const int body = std::clamp (getChoiceParam (fw::id::body), 0, 3);
    p.maxFret = fw::dsp::kBodyMaxFret[body];

    p.seqPlay   = getParam (fw::id::seqPlay) > 0.5f;
    p.seqSlot   = getChoiceParam (fw::id::seqSlot);
    p.metronome = getParam (fw::id::metronome) > 0.5f;

    p.seqSlots = seqSlots.data();
}

void FretworkAudioProcessor::buildEngineParams (fw::dsp::EngineParams& p)
{
    const int body = std::clamp (getChoiceParam (fw::id::body), 0, 3);
    // Indices 4+ are the named string sets in kMaterialSets.
    const int material = std::clamp (getChoiceParam (fw::id::material), 0, fw::dsp::kNumMaterials - 1);

    for (int s = 0; s < fw::kNumStrings; ++s)
    {
        fw::dsp::StringModelParams& sp = p.strings[(std::size_t) s];
        sp.material      = material;
        sp.age           = getScaled (fw::id::stringAge);
        sp.stiffness     = getScaled (fw::id::stiffness);
        sp.pickPos       = getScaled (fw::id::pickPos);
        sp.pickHard      = getScaled (fw::id::pickHard);
        sp.sustain       = getScaled (fw::id::sustain);
        sp.fretNoise     = getScaled (fw::id::fretNoise);
        sp.bodyLossScale = fw::dsp::kBodyLossScale[body];
        sp.scaleLengthM  = fw::dsp::kScaleLengthM[s];
        p.stringGain[(std::size_t) s] = juce::Decibels::decibelsToGain (getScaled (stringGainIds[s]));

        // Gauge set: heavier strings sustain a little longer and attack darker.
        const int set = std::clamp (getChoiceParam (fw::id::stringSet), 0, fw::dsp::kNumStringSets - 1);
        const auto& gs = fw::dsp::kStringSets[(std::size_t) set];
        const float gauge = gs.gauges[(std::size_t) s];
        sp.sustain = juce::jlimit (25.f, 200.f,
            getScaled (fw::id::sustain) * (1.f + (float) gs.sustainPerGauge * (gauge - 0.010f) * 60.f));
        sp.stiffness = juce::jlimit (0.f, 200.f,
            getScaled (fw::id::stiffness) * (float) gs.brightness);
    }

    p.body         = body;
    p.sympathetic  = getScaled (fw::id::sympathetic);
    p.glideTime    = getScaled (fw::id::glideTime);
    p.bendAmount   = getScaled (fw::id::bendAmount);
    p.bendTime     = getScaled (fw::id::bendTime);
    p.vibRate      = getScaled (fw::id::vibRate);
    p.vibDepth     = getScaled (fw::id::vibDepth);

    p.guitarType   = getChoiceParam (fw::id::guitarType);
    p.courseLevel  = getScaled (fw::id::courseLevel);

    p.pickupType   = getChoiceParam (fw::id::pickupType);
    p.pickupSel    = getChoiceParam (fw::id::pickupSel);
    p.pickupHeight = getScaled (fw::id::pickupHeight);
    p.pickupTone   = getScaled (fw::id::pickupTone);
    p.pickupOut    = getScaled (fw::id::pickupOut);

    p.bodySize     = getScaled (fw::id::bodySize);
    p.bodyRes      = getScaled (fw::id::bodyRes);
    p.topDamp      = getScaled (fw::id::topDamp);
    p.micBlend     = getScaled (fw::id::micBlend);

    p.tone         = getScaled (fw::id::tone);
    p.drive        = getScaled (fw::id::drive);
    p.room         = getScaled (fw::id::room);
    p.roomType     = getChoiceParam (fw::id::roomType);
    p.width        = getScaled (fw::id::width);
    p.output       = getScaled (fw::id::output);
    p.power        = getParam (fw::id::power) > 0.5f ? 1 : 0;

    p.ampModel      = getChoiceParam (fw::id::ampModel);
    p.ampGain       = getScaled (fw::id::ampGain);
    p.ampBass       = getScaled (fw::id::ampBass);
    p.ampMid        = getScaled (fw::id::ampMid);
    p.ampTreble     = getScaled (fw::id::ampTreble);
    p.ampLevel      = getScaled (fw::id::ampLevel);
    p.cab           = getChoiceParam (fw::id::cab);
    p.chorusMix     = getScaled (fw::id::chorusMix);
    p.chorusRate    = getScaled (fw::id::chorusRate);
    p.delayMix      = getScaled (fw::id::delayMix);
    p.delayTime     = getScaled (fw::id::delayTime);
    p.delayFeedback = getScaled (fw::id::delayFeedback);
}

//==============================================================================
void FretworkAudioProcessor::processBlock (juce::AudioBuffer<float>& audio,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = audio.getNumSamples();
    const int numChannels = audio.getNumChannels();

    audio.clear();

    if (numSamples <= 0) return;

    // ---- merge host MIDI with the on-screen keyboard ---------------------
    // One pass over a copy of the host's MIDI does both jobs. Every host event
    // is recorded in keyboardState, so notes arriving from the DAW or a MIDI
    // port light their keys on the on-screen piano. With injectIndirectEvents
    // true, the notes queued by on-screen key clicks are drained into the same
    // buffer; without that flag a click was silently discarded. Host events
    // are only recorded, never queued, so nothing is played twice.
    combinedMidi.clear();
    combinedMidi.addEvents (midi, 0, numSamples, 0);
    keyboardState.processNextMidiBuffer (combinedMidi, 0, numSamples, true);
    midi.clear();

    midiIn.clear();

    for (auto it = combinedMidi.begin(); it != combinedMidi.end(); ++it)
    {
        const auto meta = *it;
        const juce::uint8* d = meta.data;
        const int size = meta.numBytes;

        // Channel messages only. Program change and channel pressure are two
        // bytes long, so a 3-byte minimum silently dropped aftertouch.
        if (size < 2 || d[0] < 0x80 || d[0] >= 0xf0) continue;
        const int kind = d[0] & 0xf0;
        const bool twoByte = kind == 0xc0 || kind == 0xd0;
        if (! twoByte && size < 3) continue;
        if (midiIn.size() >= midiIn.capacity()) break;

        // Learned links move their parameter; the message still reaches the
        // engine, so a linked mod wheel keeps its vibrato. Short channel
        // messages fit MidiMessage's inline storage, so this never allocates.
        if (kind != 0x80 && kind != 0xc0)
            midiLearn->handleMidiMessage (meta.getMessage());

        fw::MidiMsg msg {};
        msg.offset = meta.samplePosition;
        msg.status = d[0];
        msg.d1     = d[1];
        msg.d2     = twoByte ? 0 : d[2];
        midiIn.push_back (msg);
    }

    // ---- one block of parameters, reused across the chunk loop ----------
    fw::PerfParams perfParams {};
    buildPerfParams (perfParams);
    perf.setParams (perfParams);

    fw::dsp::EngineParams engineParams {};
    buildEngineParams (engineParams);
    engine.setParams (engineParams);

    seqStore.refresh();
    for (int s = 0; s < fw::kNumSeqSlots; ++s)
        seqSlots[(std::size_t) s] = seqStore.snapshot (s);

    fw::Transport transport {};
    transport.bpm = (double) perfParams.bpm;

    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            transport.hostValid   = true;
            transport.hostPlaying = pos->getIsPlaying();

            if (const auto hostBpm = pos->getBpm())
                if (*hostBpm > 0.0) transport.bpm = *hostBpm;

            if (const auto ppq = pos->getPpqPosition())
                transport.ppq = *ppq;
            else if (const auto secs = pos->getTimeInSeconds())
                transport.ppq = *secs * transport.bpm / 60.0;
        }
    }

    // ---- render in chunks the DSP engine was prepared for ----------------
    int pos = 0;
    std::size_t midiCursor = 0;

    while (pos < numSamples)
    {
        const int chunk = std::min (maxBlockSize, numSamples - pos);

        fw::StringEvent chunkEvents[64];
        int numChunkEvents = 0;
        for (auto& e : chunkEvents) e = fw::StringEvent {};

        // Only the MIDI that falls inside this chunk, rebased to it.
        fw::MidiMsg chunkMidi[64];
        int numChunkMidi = 0;
        while (midiCursor < midiIn.size() && numChunkMidi < 64)
        {
            const fw::MidiMsg& m = midiIn[midiCursor];
            if (m.offset >= pos + chunk) break;

            if (m.offset >= pos)
            {
                fw::MidiMsg c = m;
                c.offset = m.offset - pos;
                chunkMidi[numChunkMidi++] = c;
            }
            ++midiCursor;
        }

        fw::ClickEvent chunkClicks[32];
        for (auto& c : chunkClicks) c = fw::ClickEvent {};
        int numChunkClicks = 0;

        numChunkEvents = perf.process (chunkMidi, numChunkMidi, transport, chunk,
                                       chunkEvents, 64, chunkClicks, numChunkClicks);

        // The performance engine parses pitch wheel and mod wheel; the DSP
        // applies them. Nothing forwarded them before, so both were dead.
        {
            const fw::UiState u = perf.ui();
            engine.setPitchBend (u.pitchBendSemis);
            engine.setModWheel  (u.modWheel);

            // The engine reports each keyswitch change in exactly one chunk's
            // snapshot; collect it now, or a later chunk in the same host
            // block overwrites the mask before the end-of-block publish.
            ui.ksDirtyMask.fetch_or (u.ksDirtyMask);
        }

        // Events arrive in time order; render up to each one, then apply it.
        int i = 0;
        int ev = 0, cl = 0;

        while (i < chunk)
        {
            int next = chunk;

            if (ev < numChunkEvents && chunkEvents[ev].sampleOffset < next)
                next = std::max (i, chunkEvents[ev].sampleOffset);

            if (cl < numChunkClicks && chunkClicks[cl].sampleOffset < next)
                next = std::max (i, chunkClicks[cl].sampleOffset);

            if (next > i)
            {
                for (; cl < numChunkClicks && chunkClicks[cl].sampleOffset < next; ++cl)
                    engine.clickAt (chunkClicks[cl].sampleOffset - i,
                                    chunkClicks[cl].accent);

                engine.render (renderL_.data() + i, renderR_.data() + i, next - i);
                i = next;
                continue;
            }

            // Nothing to render first: whatever lowered next is due now. Consume
            // every click and event at (or, if out of order, before) i. Handling
            // only events here left a click landing exactly on i unconsumed, so
            // the loop never advanced and the metronome hung the audio thread.
            for (; cl < numChunkClicks && chunkClicks[cl].sampleOffset <= i; ++cl)
                engine.clickAt (0, chunkClicks[cl].accent);

            for (; ev < numChunkEvents && chunkEvents[ev].sampleOffset <= i; ++ev)
                engine.handle (chunkEvents[ev]);
        }

        // Publish for the editor and for the host.
        const auto* l = audio.getArrayOfWritePointers();
        if (l != nullptr)
        {
            if (numChannels > 0) std::copy (renderL_.begin(), renderL_.begin() + chunk, l[0] + pos);
            if (numChannels > 1) std::copy (renderR_.begin(), renderR_.begin() + chunk, l[1] + pos);
        }

        pos += chunk;
    }

    // ---- UI state --------------------------------------------------------
    const fw::UiState s = perf.ui();

    for (int i2 = 0; i2 < fw::kNumStrings; ++i2)
        ui.fret[i2].store (s.fret[i2]);

    ui.seqStep.store (s.seqStep);
    ui.chordName.store (s.chordName);
    ui.ksNotes.store (s.ksNotes);

    for (int g = 0; g < 6; ++g)
        ui.ks[g].store (s.ks[g]);

    // The engine clears its own dirty mask on read, so OR it in here and let
    // the editor clear its copy once it has written the matching parameter.
    ui.ksDirtyMask.fetch_or (s.ksDirtyMask);
}

//==============================================================================
juce::AudioProcessorEditor* FretworkAudioProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
float FretworkAudioProcessor::getParam (const char* paramId) const
{
    if (auto* p = vts.getParameter (paramId))
        return p->getValue();

    return 0.f;
}

float FretworkAudioProcessor::getScaled (const char* paramId) const
{
    if (auto* p = vts.getParameter (paramId))
        return p->convertFrom0to1 (p->getValue());

    return 0.f;
}

int FretworkAudioProcessor::getChoiceParam (const char* paramId) const
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (vts.getParameter (paramId)))
        return p->getIndex();

    return 0;
}

void FretworkAudioProcessor::clearKeyswitchDirty (int group) noexcept
{
    if (group < 0 || group >= 6) return;

    ui.ksDirtyMask.fetch_and (~(1u << group), std::memory_order_relaxed);
}

//==============================================================================
void FretworkAudioProcessor::applyBodyDefaults (int bodyIndex)
{
    auto setFloat = [this] (const char* paramId, float value)
    {
        if (auto* p = vts.getParameter (paramId))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    auto setChoice = [this] (const char* paramId, int index)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (vts.getParameter (paramId)))
        {
            const int last = std::max (1, (int) p->choices.size() - 1);
            p->setValueNotifyingHost ((float) index / (float) last);
        }
    };

    switch (std::clamp (bodyIndex, 0, 3))
    {
        case 0:  // Dreadnought steel
            setChoice (fw::id::material, 0);
            setFloat  (fw::id::pickPos,   15.f);
            setFloat  (fw::id::pickHard,  55.f);
            setFloat  (fw::id::micBlend,  70.f);
            break;

        case 1:  // Classical nylon
            setChoice (fw::id::material, 2);
            setFloat  (fw::id::pickPos,   18.f);
            setFloat  (fw::id::pickHard,  25.f);
            setFloat  (fw::id::micBlend,  75.f);
            break;

        case 2:  // Solid electric
            setChoice (fw::id::material,   1);
            setChoice (fw::id::pickupType, 0);
            setChoice (fw::id::pickupSel,  1);
            setFloat  (fw::id::pickPos,    12.f);
            setFloat  (fw::id::pickHard,   55.f);
            setFloat  (fw::id::drive,       0.f);
            setFloat  (fw::id::micBlend,    0.f);
            break;

        case 3:  // Semi-hollow
            setChoice (fw::id::material,   3);
            setChoice (fw::id::pickupType, 1);
            setChoice (fw::id::pickupSel,  2);
            setFloat  (fw::id::pickPos,    14.f);
            setFloat  (fw::id::pickHard,   50.f);
            setFloat  (fw::id::micBlend,   25.f);
            break;

        default:
            break;
    }
}

//==============================================================================
void FretworkAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state = vts.copyState();

    // APVTS only pushes parameter values into its tree on a timer, and its
    // flush method is private, so a host that saves between ticks would
    // capture stale values. Write each parameter's current value into the copy.
    // The children are type "PARAM", keyed by "id", holding "value".
    // Each "PARAM" child stores its parameter's RAW value, not the normalised
    // 0..1 one, and APVTS only writes them on its own timer. Flush them here so
    // a host that saves between ticks still captures the real settings.
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto child = state.getChild (i);
        if (! child.isValid())
            continue;

        const juce::String idString = child.getProperty (juce::Identifier ("id")).toString();
        if (idString.isEmpty())
            continue;

        if (auto* param = vts.getParameter (idString))
            child.setProperty (juce::Identifier ("value"),
                               juce::var (param->convertFrom0to1 (param->getValue())),
                               nullptr);
    }

    juce::ValueTree seq ("SEQ");

    for (int slot = 0; slot < fw::kNumSeqSlots; ++slot)
        seq.setProperty (juce::String (slot), seqStore.serialiseSlot (slot), nullptr);

    state.appendChild (seq, nullptr);
    state.setProperty ("program", patches->currentFactory(), nullptr);
    state.setProperty ("midiLearn", midiLearn->serialise(), nullptr);
    state.setProperty ("toneMapping", fw::kToneMappingVersion, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void FretworkAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto state = juce::ValueTree::readFromData (data, (std::size_t) sizeInBytes);

    if (! state.isValid())
        return;

    const auto seq = state.getChildWithName (juce::Identifier ("SEQ"));

    if (seq.isValid())
    {
        for (int slot = 0; slot < fw::kNumSeqSlots; ++slot)
            seqStore.deserialiseSlot (slot, seq.getProperty (juce::String (slot)).toString());

        // Remove only the sequencer node. removeAllChildren would also delete
        // every PARAM child, leaving replaceState with an empty tree and
        // silently reverting every parameter to its default.
        state.removeChild (seq, nullptr);
    }

    // The saved program index is restored as a number only: the parameters
    // in the state are the sound, and must not be overwritten by the preset.
    const int program = (int) state.getProperty ("program", 0);
    state.removeProperty ("program", nullptr);
    midiLearn->deserialise (state.getProperty ("midiLearn", juce::String()).toString());
    state.removeProperty ("midiLearn", nullptr);

    // Projects saved before the Tone fix store the inverted-mapping value.
    if ((int) state.getProperty ("toneMapping", 1) < fw::kToneMappingVersion)
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto child = state.getChild (i);
            if (child.getProperty ("id").toString() == fw::id::tone)
                child.setProperty ("value", fw::legacyToneHz ((double) child.getProperty ("value")), nullptr);
        }
    state.removeProperty ("toneMapping", nullptr);

    if (juce::isPositiveAndBelow (program, getNumPrograms()))
        patches->setCurrentFactoryIndexOnly (program);

    vts.replaceState (state);

    // Undo after loading a project should not step back to the defaults.
    patches->resetHistory();
}

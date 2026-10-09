#include "engine/PerformanceEngine.h"

#include <cmath>
#include <cstddef>

namespace fw {

StrokeKind PerformanceEngine::kindOf (ks::Stroke s) noexcept
{
    switch (s)
    {
        case ks::Stroke::MutedDown:
        case ks::Stroke::MutedUp:   return StrokeKind::Muted;
        case ks::Stroke::PalmDown:
        case ks::Stroke::PalmUp:   return StrokeKind::Palm;
        default:                   return StrokeKind::Normal;
    }
}

bool PerformanceEngine::isUpward (ks::Stroke s) noexcept
{
    return s == ks::Stroke::Up || s == ks::Stroke::MutedUp || s == ks::Stroke::PalmUp;
}

//==============================================================================
void PerformanceEngine::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    chord_.clear();
    voicing_.clear();
    for (bool& b : sounding_) b = false;

    strum_.prepare (sampleRate_);
    seq_.reset();
    clock_.prepare (sampleRate_);
    rng_.seed (0x5eed1234u);

    queueCount_  = 0;
    absCursor_   = 0;
    tremAccum_   = 0.0;
    metAccum_    = 0.0;
    metBeat_     = 0;
    prepared_    = true;

    lastQuantize_  = -1;
    lastSignature_ = -1;
    lastMode_      = -1;

    refreshUi();
}

void PerformanceEngine::reset()
{
    prepare (sampleRate_);
}

void PerformanceEngine::setParams (const PerfParams& p)
{
    p_ = p;

    clock_.setTempo (p_.bpm <= 0.f ? 100.0 : (double) p_.bpm);

    if (p_.quantize != lastQuantize_)
    {
        clock_.setQuantize (p_.quantize);
        lastQuantize_ = p_.quantize;
    }

    if (p_.signature != lastSignature_)
    {
        clock_.setSignature (p_.signature);
        lastSignature_ = p_.signature;
    }

    if ((int) p_.mode != lastMode_)
    {
        if (p_.mode == PlayMode::Arp) arp_.seed (rng_.next());
        if (p_.mode == PlayMode::Seq) seq_.reset();
        lastMode_ = (int) p_.mode;
    }

    // Sustain: the pedal or the hold switch latches the whole chord.
    if (pedalDown_ || p_.hold) chord_.setLatched (true);

    // Once any keyswitch has been pressed they are authoritative; the editor
    // mirrors them into the choice parameters, so both paths agree.
    if (keyswitchesUsed_)
        p_.art = ks::applyGroup (p_.art, ks_);
}

//==============================================================================
float PerformanceEngine::fretOn (int stringIndex, float pitch) const noexcept
{
    if (stringIndex < 0 || stringIndex >= kNumStrings) return 0.f;
    return pitch - p_.open[stringIndex];
}

int PerformanceEngine::voicingStringIndexOf (int s) const noexcept
{
    for (int vi = 0; vi < voicing_.count; ++vi)
        if (voicing_.stringIndex[vi] == s) return vi;

    return -1;
}

int PerformanceEngine::heldPitchOnString (int s) const
{
    int best = -1;
    for (int i = 0; i < chord_.size(); ++i)
    {
        const ChordNote& n = chord_.at (i);
        if (n.string == s && (best < 0 || n.pitch > best))
            best = n.pitch;
    }
    return best;
}

void PerformanceEngine::refreshVoicing()
{
    voicing_.clear();

    const int n = chord_.size() < ChordState::kCapacity ? chord_.size()
                                                        : ChordState::kCapacity;
    if (n <= 0) return;

    float pitches[ChordState::kCapacity];
    for (int i = 0; i < n; ++i)
        pitches[i] = (float) chord_.at (i).pitch;

    assignVoicing (pitches, n, p_.open, p_.maxFret, voicing_);
}

void PerformanceEngine::refreshUi()
{
    for (int i = 0; i < kNumStrings; ++i) ui_.fret[i] = -1;

    for (int vi = 0; vi < voicing_.count; ++vi)
    {
        const int s = voicing_.stringIndex[vi];
        if (s >= 0 && s < kNumStrings) ui_.fret[s] = voicing_.fret[vi];
    }

    ui_.seqStep = (p_.mode == PlayMode::Seq) ? seq_.currentStep() : -1;

    for (int g = 0; g < 6; ++g) ui_.ks[g] = ks_[g];
    ui_.ksNotes       = ksNotes_;
    ui_.ksDirtyMask   = ksDirty_;
    ui_.modWheel      = (float) modWheel_;
    ui_.pitchBendSemis = (float) bendSemis_;

    ksDirty_ = 0;   // every change is reported exactly once
}

//==============================================================================
void PerformanceEngine::toEvent (const StrumHit& hit, StringEvent& ev) const
{
    ev.string     = hit.stringIndex;
    ev.type       = hit.type;
    ev.pitch      = hit.pitch;
    ev.pitchFrom  = hit.pitchFrom;
    ev.velocity   = hit.velocity;
    ev.harmonicN  = hit.harmonicN;
    ev.releaseSec = hit.releaseSec;
    ev.art        = hit.art;
}

void PerformanceEngine::pushHit (const StrumHit& hit, long long at)
{
    if (queueCount_ >= kQueueCapacity)
    {
        for (int i = 0; i + 1 < queueCount_; ++i)
            queue_[(size_t) i] = queue_[(size_t) (i + 1)];
        --queueCount_;
    }

    QueuedEvent q {};
    q.abs = (std::int64_t) at;
    toEvent (hit, q.ev);

    // Stable insertion sort by absolute time.
    int j = queueCount_;
    while (j > 0 && queue_[(size_t) (j - 1)].abs > q.abs)
    {
        queue_[(size_t) j] = queue_[(size_t) (j - 1)];
        --j;
    }
    queue_[(size_t) j] = q;
    ++queueCount_;
}

int PerformanceEngine::drain (int numSamples, StringEvent* out, int outCapacity)
{
    if (out == nullptr || outCapacity <= 0 || numSamples <= 0) return 0;

    int written = 0;
    int i = 0;

    while (i < queueCount_)
    {
        const long long off = (long long) queue_[(size_t) i].abs - absCursor_;
        if (off >= numSamples) { ++i; continue; }

        if (written < outCapacity)
        {
            out[written] = queue_[(size_t) i].ev;
            out[written].sampleOffset = (int) (off < 0 ? 0 : off);
            ++written;
        }

        for (int k = i; k + 1 < queueCount_; ++k)
            queue_[(size_t) k] = queue_[(size_t) (k + 1)];
        --queueCount_;
    }

    return written;
}

//==============================================================================
void PerformanceEngine::noteOn (int pitch, float velocity, int offset)
{
    const bool alreadyHeld = chord_.contains (pitch);

    if (! alreadyHeld)
    {
        if (! chord_.add (pitch, velocity, (int) std::lround (p_.open[0])))
            return;

        chord_.setLatched (pedalDown_ || p_.hold);
    }

    refreshVoicing();

    // Only Poly sounds directly; the other modes go through their own paths.
    if (p_.mode != PlayMode::Poly) return;

    int stringIdx = chord_.stringFor (pitch);

    if (stringIdx < 0)
    {
        const float handPos = chord_.handPosition();
        float bestCost = 0.f;
        bool  haveBest = false;

        for (int vi = 0; vi < voicing_.count; ++vi)
        {
            if (! chord_.contains ((int) voicing_.pitch[vi])) continue;

            const int   s    = voicing_.stringIndex[vi];
            const float fret = voicing_.fretFloat[vi];

            float cost = std::fabs (fret - handPos);
            if (chord_.stringIsHeld (s)) cost += 100.f;
            if (sounding_[(size_t) s])    cost += 5.f;

            if (! haveBest || cost < bestCost)
            {
                bestCost  = cost;
                stringIdx = s;
                haveBest  = true;
            }
        }

        if (! haveBest || stringIdx < 0 || stringIdx >= kNumStrings) return;

        const int vi = voicingStringIndexOf (stringIdx);
        chord_.setString (pitch, stringIdx, vi >= 0 ? voicing_.fretFloat[vi] : 0.f);
    }

    const bool legato = p_.art.trans == Transition::Legato && alreadyHeld;

    StrumHit h {};
    h.type         = legato ? EventType::Legato : EventType::Pluck;
    h.stringIndex  = stringIdx;
    h.velocity     = velocity;
    h.art          = p_.art;
    h.releaseSec   = p_.release;
    h.pitchFrom    = -1.f;
    h.delaySamples = 0.0;
    h.pitch        = p_.open[stringIdx];
    h.fret         = 0;

    const int vi = voicingStringIndexOf (stringIdx);
    if (vi >= 0)
    {
        h.pitch = voicing_.pitch[vi];
        h.fret  = voicing_.fret[vi];

        if (p_.art.harm == Harmonic::Natural)
        {
            const int partial = harmonicPartialForFret (h.fret);
            if (partial > 0) { h.harmonicN = partial; h.pitch = p_.open[stringIdx]; }
        }
    }

    if (p_.art.trans == Transition::Slide && ! alreadyHeld)
    {
        const int heldPitch = heldPitchOnString (stringIdx);
        if (heldPitch >= 0) h.pitchFrom = (float) heldPitch;
    }

    sounding_[(size_t) stringIdx] = true;
    pushHit (h, absCursor_ + offset);
}

void PerformanceEngine::noteOff (int pitch, int offset)
{
    if (chord_.isLatched (pitch)) return;

    const bool pullOff = p_.mode == PlayMode::Poly
                      && p_.art.trans == Transition::Legato
                      && chord_.hasLowerNoteOnSameString (pitch);

    int  lower = -1;
    const bool haveLower = pullOff && chord_.nextLowerOnSameString (pitch, lower);
    const int  stringIdx = chord_.stringFor (pitch);

    chord_.remove (pitch);
    refreshVoicing();

    if (stringIdx >= 0 && stringIdx < kNumStrings && ! chord_.stringIsHeld (stringIdx))
        sounding_[(size_t) stringIdx] = false;

    if (p_.mode != PlayMode::Poly || stringIdx < 0 || stringIdx >= kNumStrings) return;

    StrumHit h {};
    h.stringIndex  = stringIdx;
    h.art          = p_.art;
    h.releaseSec   = p_.release;
    h.pitchFrom    = -1.f;
    h.delaySamples = 0.0;

    if (haveLower)
    {
        h.type     = EventType::Legato;
        h.pitch    = (float) lower;
        h.velocity = 0.7f;
    }
    else
    {
        h.type     = EventType::Release;
        h.pitch    = (float) pitch;
        h.velocity = 0.f;
    }

    pushHit (h, absCursor_ + offset);
}

void PerformanceEngine::strokeKey (ks::Stroke stroke, float velocity, int offset)
{
    refreshVoicing();
    if (voicing_.count <= 0) return;

    StrumEngine::StrokeParams sp {};
    sp.strumTimeMs  = p_.strumTime;
    sp.timeOffsetMs = p_.timeOffset;
    sp.separation01 = p_.separation;
    sp.velOffset    = p_.velOffset;
    sp.strokeVol    = p_.strokeVol;
    sp.humanize01   = p_.humanize;

    const int n = (stroke == ks::Stroke::PickLowest || stroke == ks::Stroke::PickHighest)
        ? StrumEngine::pluckExtreme (stroke == ks::Stroke::PickHighest,
                                     voicing_, p_.open, p_.art, velocity,
                                     scratch_, kMaxHits)
        : strum_.stroke (isUpward (stroke), kindOf (stroke),
                         (int) std::lround (velocity * 127.f), sp,
                         voicing_, p_.open, p_.art, rng_, scratch_, kMaxHits);

    for (int i = 0; i < n; ++i)
    {
        scratch_[i].releaseSec = p_.release;
        pushHit (scratch_[i], absCursor_ + offset + (long long) scratch_[i].delaySamples);
    }
}

//==============================================================================
void PerformanceEngine::handleMidi (const MidiMsg& m)
{
    const int offset = m.offset;
    const int type   = m.status & 0xf0;
    const int ch     = m.status & 0x0f;
    const int note   = (int) m.d1;

    if (type == 0x90 && m.d2 > 0)
    {
        if (ks::isKeyswitch (note))
        {
            if (ks::isLatched (note))
            {
                const int g = (int) ks::group (note);
                const int v = ks::groupValue (note);

                keyswitchesUsed_ = true;

                if (ks_[g] != v)
                {
                    ks_[g] = v;
                    // Only one key per group may read as latched. Without
                    // clearing first, latching a second key in the same group
                    // left the first one's bit set and the UI highlighted both.
                    for (int n = ks::kFirstNote; n <= ks::kLastNote; ++n)
                        if (ks::isLatched (n) && (int) ks::group (n) == g)
                            ksNotes_ &= ~(1u << (std::uint32_t) (n - ks::kFirstNote));

                    ksNotes_ |= (1u << (std::uint32_t) (note - ks::kFirstNote));
                    ksDirty_  |= (1u << g);
                }
            }
            else
            {
                strokeKey (ks::stroke (note), (float) m.d2 / 127.f, offset);
            }
            return;
        }

        if (note >= kLowestPlayable)
            noteOn (note + p_.transpose, (float) m.d2 / 127.f, offset);

        return;
    }

    if (type == 0x80 || (type == 0x90 && m.d2 == 0))
    {
        // Stroke keys are momentary, so their note-off is a no-op; latch
        // groups toggle on note-on and deliberately ignore note-off.
        if (ks::isKeyswitch (note)) return;
        if (note >= kLowestPlayable) noteOff (note + p_.transpose, offset);
        return;
    }

    if (type == 0xb0 && ch == 1) { modWheel_ = (double) m.d2 / 127.0; return; }

    if (type == 0xb0 && ch == 64)
    {
        pedalDown_ = m.d2 >= 64;
        if (pedalDown_) chord_.setLatched (true);
        else if (! p_.hold) chord_.releaseLatched();
        refreshVoicing();
        return;
    }

    if (type == 0xe0)
    {
        // 14-bit pitch bend centred at 8192, spanning +/- 2 semitones.
        const int raw = (int) m.d1 | ((int) m.d2 << 7);
        bendSemis_ = ((double) (raw - 8192) / 8192.0) * 2.0;
    }
}

//==============================================================================
void PerformanceEngine::emitArpAndSeq (double ppqStart, double ppqEnd)
{
    if (! (ppqEnd >= ppqStart)) return;

    const double bpm = clock_.bpm();

    if (p_.mode == PlayMode::Arp)
    {
        arp_.rebuild (voicing_, p_.arpMask, p_.arpStrings);

        float stringVel[kNumStrings] {};
        for (int i = 0; i < chord_.size(); ++i)
        {
            const ChordNote& n = chord_.at (i);
            if (n.string >= 0 && n.string < kNumStrings)
                stringVel[n.string] = n.velocity;
        }

        ArpEngine::Params ap {};
        ap.rate       = p_.arpRate;
        ap.order      = p_.arpOrder;
        ap.gate01     = p_.arpGate  * 0.01f;
        ap.swing01    = p_.arpSwing * 0.01f;
        ap.strokeVol  = p_.strokeVol;
        ap.humanize01 = p_.humanize;

        const int n = arp_.advance (ppqStart, ppqEnd, sampleRate_, bpm, ap,
                                    p_.open, stringVel, p_.art, rng_,
                                    scratch_, kMaxHits);
        for (int i = 0; i < n; ++i)
        {
            scratch_[i].releaseSec = p_.release;
            pushHit (scratch_[i], absCursor_ + (long long) scratch_[i].delaySamples);
        }
        return;
    }

    if (p_.mode != PlayMode::Seq || ! p_.seqPlay || p_.seqSlots == nullptr) return;

    const int slot = p_.seqSlot >= 0 && p_.seqSlot < kNumSeqSlots ? p_.seqSlot : 0;

    StrumEngine::StrokeParams sp {};
    sp.strumTimeMs  = p_.strumTime;
    sp.timeOffsetMs = p_.timeOffset;
    sp.separation01 = p_.separation;
    sp.velOffset    = p_.velOffset;
    sp.strokeVol    = p_.strokeVol;
    sp.humanize01   = p_.humanize;

    const int n = seq_.advance (ppqStart, ppqEnd, sampleRate_, bpm,
                                (double) clock_.stepsPerQuarterInt(),
                                clock_.stepsPerBar(),
                                &p_.seqSlots[slot], voicing_, sp, strum_, p_.open,
                                p_.release, p_.art, rng_, scratch_, kMaxHits);

    for (int i = 0; i < n; ++i)
        pushHit (scratch_[i], absCursor_ + (long long) scratch_[i].delaySamples);
}

void PerformanceEngine::pollTremolo (int numSamples)
{
    if (! (p_.art.tremolo && p_.mode == PlayMode::Poly) || chord_.size() == 0)
    {
        tremAccum_ = 0.0;
        return;
    }

    const double bpm = clock_.bpm() > 0.0 ? clock_.bpm() : 100.0;
    const double spq = sampleRate_ * 60.0 / bpm;
    const int    rate = p_.tremRate < 1.f ? 1 : (int) (p_.tremRate + 0.5f);
    const double period = spq / (double) rate;

    if (! (period > 0.0)) return;

    tremAccum_ += (double) numSamples;

    int guard = 0;
    while (tremAccum_ >= period && guard++ < 8)
    {
        tremAccum_ -= period;

        for (int i = 0; i < chord_.size(); ++i)
        {
            const ChordNote& n = chord_.at (i);
            if (n.string < 0 || n.string >= kNumStrings) continue;

            StrumHit h {};
            h.type         = EventType::Pluck;
            h.stringIndex  = n.string;
            h.pitch        = (float) n.pitch;
            h.pitchFrom    = -1.f;
            h.fret         = (int) (n.fret + 0.5f);
            h.velocity     = n.velocity * 0.8f;
            h.releaseSec   = p_.release;
            h.art          = p_.art;
            h.delaySamples = 0.0;
            pushHit (h, absCursor_);
        }
    }
}

void PerformanceEngine::pollMetronome (int numSamples, ClickEvent* clicks, int& numClicks)
{
    if (! p_.metronome || clicks == nullptr) return;

    const double bpm = clock_.bpm() > 0.0 ? clock_.bpm() : 100.0;
    const double spq = sampleRate_ * 60.0 / bpm;
    if (! (spq > 1.0)) return;

    const double start = metAccum_;
    metAccum_ += (double) numSamples;

    const int sig = lastSignature_ >= 0 && lastSignature_ < Clock::kNumSignature
                  ? lastSignature_ : 0;
    const int beatsPerBar = Clock::kSignatureNum[sig];

    for (double next = spq - start; next <= metAccum_ && numClicks < kMaxClicks; next += spq)
    {
        long long off = (long long) (next + 0.5);
        if (off < 0) off = 0;
        if (off > numSamples - 1) off = numSamples - 1;

        ClickEvent& c = clicks[numClicks++];
        c.sampleOffset = (int) off;
        c.accent       = (metBeat_ % beatsPerBar) == 0;
        ++metBeat_;
    }
}

//==============================================================================
int PerformanceEngine::process (const MidiMsg* in,
                                int             numIn,
                                const Transport& transport,
                                int             numSamples,
                                StringEvent*    out,
                                int             outCapacity,
                                ClickEvent*     clicks,
                                int&            numClicks)
{
    numClicks = 0;

    if (! prepared_) prepare (sampleRate_);
    if (numSamples <= 0) return 0;

    if (in != nullptr)
        for (int i = 0; i < numIn; ++i)
            handleMidi (in[i]);

    refreshVoicing();

    const bool chordActive = ! chord_.size() == 0;

    double ppqStart = clock_.ppq();
    if (transport.hostValid && transport.hostPlaying) ppqStart = transport.ppq;

    const bool running = clock_.advance (numSamples, transport, p_.mode, p_.seqPlay, chordActive);
    const double ppqEnd = clock_.ppq();

    if (running) emitArpAndSeq (ppqStart, ppqEnd);

    pollTremolo (numSamples);
    pollMetronome (numSamples, clicks, numClicks);

    refreshUi();

    const int written = drain (numSamples, out, outCapacity);

    absCursor_ += numSamples;
    return written;
}

} // namespace fw

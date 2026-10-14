#include "engine/PerformanceEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fw {

// Articulation auto-detection rules (Poly, live MIDI). Fixed, user-approved:
//   strum      3+ note-ons within 70 ms; rising pitch order = down-stroke;
//              all within 1 ms = block chord (short scrape)
//   hammer/pull new note on the string of a still-held note (held >= 30 ms),
//              1-4 semitones up (hammer-on) or down (pull-off)
//   slide      same overlap at 5-12 semitones, or any overlap at velocity >= 110
//   palm mute  velocity 20-45; dead mute velocity 1-19
//   harmonic   velocity 127 on a 12th/19th/24th-fret harmonic of an open string
//   squeak     on slides, and on picked notes 5+ frets from the last one < 0.4 s
//   vibrato    automatic after a note is held 0.6 s with one or two notes down
namespace {
    constexpr double kStrumWindowSec  = 0.070;
    constexpr int    kStrumMinNotes   = 3;
    constexpr double kBlockChordSec   = 0.001;
    constexpr double kLegatoMinAgeSec = 0.030;
    constexpr int    kHammerMaxSemis  = 4;
    constexpr int    kSlideMaxSemis   = 12;
    constexpr int    kSlideVel        = 110;
    constexpr int    kPalmMuteMaxVel  = 45;
    constexpr int    kDeadMuteMaxVel  = 19;
    constexpr int    kHarmonicVel     = 127;
    constexpr double kShiftWindowSec  = 0.4;
    constexpr int    kShiftMinFrets   = 5;
    constexpr double kAutoVibDelaySec = 0.6;
    constexpr double kAutoVibRiseSec  = 0.4;
    constexpr double kAutoVibDepth    = 0.5;
    // Strum mode: note-ons within kChordGatherSec are one chord, strummed down
    // once; skipped while stroke keys were used in the last kStrokeKeyHoldSec.
    constexpr double kChordGatherSec   = 0.020;
    constexpr double kStrokeKeyHoldSec = 4.0;
}

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
    lastShape_.clear();
    shapedChord_ = {};
    voicingKeyLen_ = -1;
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
    for (auto& p : soundingPitch_) p = -1;
    clusterCount_ = 0;
    clusterNoised_ = false;
    lastOnAbs_ = -1;
    lastFret_ = -1;
    autoVib_ = 0.0;
    autoStrumAt_ = -1;
    lastStrokeKeyAbs_ = -1;
    std::fill (std::begin (coveredBy_), std::end (coveredBy_), 0);
    std::fill (std::begin (legatoString_), std::end (legatoString_), 0);
    lastChordOffAbs_ = -1;
    shiftCheckAt_ = -1;
    lastHand_ = -1.f;
    frettedMask_ = 0;

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
        // Finger state belongs to the chord modes' voicing: start clean.
        std::fill (std::begin (coveredBy_), std::end (coveredBy_), 0);
        std::fill (std::begin (legatoString_), std::end (legatoString_), 0);
        frettedMask_ = 0;
        autoStrumAt_ = -1;
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
    // Drop chord-mode finger state for notes that are no longer held (pedal
    // release, all-notes-off, or a covered note whose cover left): a stale
    // entry would keep that pitch out of every later chord.
    for (int q = 0; q < 128; ++q)
        if ((coveredBy_[q] != 0 || legatoString_[q] != 0) && ! chord_.contains (q))
            coveredBy_[q] = legatoString_[q] = 0;
    for (int q = 0; q < 128; ++q)
        if (coveredBy_[q] != 0 && ! chord_.contains (coveredBy_[q] - 1)) coveredBy_[q] = 0;
    int n = chord_.size() < ChordState::kCapacity ? chord_.size()
                                                        : ChordState::kCapacity;

    // Runs every block: only rebuild when the held notes, the mode, the voicing
    // setting, the neck range or the tuning changed.
    int key[ChordState::kCapacity + 4] {};
    int len = 0;
    for (int i = 0; i < n; ++i)
    {
        const int q = chord_.at (i).pitch;
        key[len++] = coveredBy_[q & 127] != 0 && p_.mode != PlayMode::Poly ? -q : q;
    }
    key[len++] = (int) p_.mode;
    key[len++] = p_.voicingMode;
    key[len++] = p_.maxFret;
    int tuning = 0;
    for (int s = 0; s < kNumStrings; ++s) tuning = tuning * 31 + (int) std::lround (p_.open[s] * 8.f);
    key[len++] = tuning;
    if (len == voicingKeyLen_ && std::equal (key, key + len, voicingKey_)) return;
    std::copy (key, key + len, voicingKey_);
    voicingKeyLen_ = len;

    voicing_.clear();
    shapedChord_ = {};
    if (n <= 0) return;

    int   held[ChordState::kCapacity];
    float pitches[ChordState::kCapacity];
    int   kept = 0;
    for (int i = 0; i < n; ++i)
    {
        const int q = chord_.at (i).pitch;
        // Chord modes: a note under a hammered/slid finger no longer sounds.
        if (p_.mode != PlayMode::Poly && coveredBy_[q & 127] != 0) continue;
        held[kept] = q;
        pitches[kept] = (float) q;
        ++kept;
    }
    n = kept;
    if (n <= 0) return;

    // Poly plays each key on its own string, so it always voices as held.
    // Elsewhere: Guitar shapes any recognised chord; Auto only when it is a
    // real chord (3+ distinct notes), so single notes and intervals stay as
    // played and the switch follows what is being played.
    if (p_.mode != PlayMode::Poly && p_.voicingMode != 1)
    {
        const ChordName c = chord::detect (held, n);
        unsigned pcs = 0;
        for (int i = 0; i < n; ++i) pcs |= 1u << (((held[i] % 12) + 12) % 12);
        int distinct = 0;
        for (unsigned v = pcs; v != 0; v &= v - 1) ++distinct;

        const bool shape = c.valid() && (p_.voicingMode == 2 || distinct >= 3);
        if (shape && chord::guitarVoicing (c, p_.open, p_.maxFret, lastShape_.count > 0 ? &lastShape_ : nullptr, voicing_))
        {
            shapedChord_ = c;
            lastShape_ = voicing_;
            return;
        }
    }

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
    ui_.modWheel      = (float) std::max (modWheel_, autoVib_);
    ui_.pitchBendSemis = (float) (bendSemis_ + aftertouchSemis_);
    ui_.chordName = shapedChord_.pack();

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
    ev.dir        = hit.dir;
    ev.durSec     = hit.durSec;
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
// Strum / Arp / Seq: the held chord's own velocity carries the right-hand
// technique, the way it does for a single note in Poly. Manual controls win.
Articulation PerformanceEngine::chordArt() const noexcept
{
    Articulation a = p_.art;
    if (p_.mode == PlayMode::Poly || chord_.size() == 0) return a;

    float vmax = 0.f;
    for (int i = 0; i < chord_.size(); ++i) vmax = std::max (vmax, chord_.at (i).velocity);
    const int v127 = (int) std::lround (std::clamp (vmax, 0.f, 1.f) * 127.f);

    if (a.damp == Damping::Open)
    {
        if (v127 <= kDeadMuteMaxVel)      a.damp = Damping::Dead;
        else if (v127 <= kPalmMuteMaxVel) a.damp = Damping::PalmMute;
    }
    if (a.harm == Harmonic::Off && v127 >= kHarmonicVel) a.harm = Harmonic::Natural;
    return a;
}

// The voicing actually played. With natural harmonics on (chord modes), each
// note that is a 12th / 19th / 24th-fret harmonic of a free string moves there.
Voicing PerformanceEngine::playVoicing (const Articulation& art) const noexcept
{
    Voicing v = voicing_;
    if (p_.mode == PlayMode::Poly || art.harm != Harmonic::Natural) return v;

    bool used[kNumStrings] {};
    for (int vi = 0; vi < v.count; ++vi) used[v.stringIndex[vi]] = true;
    for (int vi = 0; vi < v.count; ++vi)
    {
        const int p = (int) std::lround (v.pitch[vi]);
        if (harmonicPartialForFret (v.fret[vi]) > 0) continue;
        for (int s = 0; s < kNumStrings; ++s)
        {
            const int rel = p - (int) std::lround (p_.open[s]);
            if (used[s] || harmonicPartialForFret (rel) <= 0 || rel > p_.maxFret) continue;
            used[v.stringIndex[vi]] = false;
            used[s] = true;
            v.stringIndex[vi] = s; v.fret[vi] = rel; v.fretFloat[vi] = (float) rel;
            break;
        }
    }
    // Keep strings ascending: stroke order follows the voicing order.
    for (int a = 1; a < v.count; ++a)
        for (int b = a; b > 0 && v.stringIndex[b] < v.stringIndex[b - 1]; --b)
        {
            std::swap (v.stringIndex[b], v.stringIndex[b - 1]);
            std::swap (v.fret[b], v.fret[b - 1]);
            std::swap (v.pitch[b], v.pitch[b - 1]);
            std::swap (v.fretFloat[b], v.fretFloat[b - 1]);
        }
    return v;
}

bool PerformanceEngine::chordModeLegato (int pitch, float velocity, long long at)
{
    if (p_.art.trans != Transition::Normal) return false;
    // Only over a single held note (a melodic line): notes added to a held
    // chord or interval are the player building a chord, not hammering.
    if (chord_.size() != 1) return false;
    if (lastOnAbs_ >= 0 && at - lastOnAbs_ < (long long) (kStrumWindowSec * sampleRate_)) return false;   // part of a chord

    // The newest held note voiced on a string the new note can be fretted on.
    int bestString = -1, fromPitch = -1;
    std::int64_t newest = -1;
    for (int vi = 0; vi < voicing_.count; ++vi)
    {
        const int q = (int) std::lround (voicing_.pitch[vi]);
        const int s = voicing_.stringIndex[vi];
        if (! chord_.contains (q) || q == pitch) continue;
        if (at - noteStart_[q & 127] < (long long) (kLegatoMinAgeSec * sampleRate_)) continue;
        const int d = pitch - q, fret = pitch - (int) std::lround (p_.open[s]);
        if (std::abs (d) < 1 || std::abs (d) > kSlideMaxSemis || fret < 0 || fret > p_.maxFret) continue;
        if (noteStart_[q & 127] > newest) { newest = noteStart_[q & 127]; bestString = s; fromPitch = q; }
    }
    if (bestString < 0) return false;

    if (! chord_.add (pitch, velocity, (int) std::lround (p_.open[0]))) return false;
    chord_.setLatched (pedalDown_ || p_.hold);
    coveredBy_[fromPitch & 127]  = pitch + 1;
    legatoString_[pitch & 127]   = bestString + 1;
    frettedMask_ |= 1u << bestString;

    const bool slide = std::abs (pitch - fromPitch) > kHammerMaxSemis || (int) std::lround (velocity * 127.f) >= kSlideVel;
    StrumHit h {};
    h.type         = EventType::Legato;
    h.stringIndex  = bestString;
    h.velocity     = velocity;
    h.art          = chordArt();
    h.art.trans    = slide ? Transition::Slide : Transition::Legato;
    h.releaseSec   = p_.release;
    h.pitch        = (float) pitch;
    h.pitchFrom    = slide ? (float) fromPitch : -1.f;
    h.fret         = pitch - (int) std::lround (p_.open[bestString]);
    pushHit (h, at);
    if (slide) emitSqueak (bestString, pitch > fromPitch ? 1 : -1, velocity, at, std::abs (pitch - fromPitch));

    refreshVoicing();
    return true;
}

void PerformanceEngine::noteOn (int pitch, float velocity, int offset)
{
    const bool alreadyHeld = chord_.contains (pitch);
    const long long nowAbs = absCursor_ + offset;

    // Chord modes: a lone note landing on the string of a held, ringing note
    // is a hammer-on / slide on that string, not a new chord tone.
    if (p_.mode != PlayMode::Poly && ! alreadyHeld)
    {
        // A new chord starts from no fretted strings (the let-go release reads
        // the mask after the last note leaves, so it is cleared here, not there).
        if (chord_.size() == 0) frettedMask_ = 0;
        refreshVoicing();
        if (chordModeLegato (pitch, velocity, nowAbs))
        {
            noteStart_[pitch & 127] = nowAbs;
            lastOnAbs_ = nowAbs;
            return;
        }

        // First note of a new chord: check for a position shift once it has gathered.
        const bool firstOfChord = lastOnAbs_ < 0 || nowAbs - lastOnAbs_ > (long long) (kStrumWindowSec * sampleRate_);
        if (firstOfChord)
        {
            shiftQuick_ = chord_.size() > 0
                       || (lastChordOffAbs_ >= 0 && nowAbs - lastChordOffAbs_ < (long long) (kShiftWindowSec * sampleRate_));
            shiftCheckAt_ = nowAbs + (long long) (kChordGatherSec * sampleRate_);
        }
        noteStart_[pitch & 127] = nowAbs;
        lastOnAbs_ = nowAbs;
    }

    if (! alreadyHeld)
    {
        if (! chord_.add (pitch, velocity, (int) std::lround (p_.open[0])))
            return;

        chord_.setLatched (pedalDown_ || p_.hold);
    }

    refreshVoicing();
    if (p_.mode != PlayMode::Poly)
        for (int vi = 0; vi < voicing_.count; ++vi) frettedMask_ |= 1u << voicing_.stringIndex[vi];

    // Strum mode: pressing a chord strums it, the way a guitarist's right hand
    // follows the left. Hand-played stroke keys take over when in use.
    if (p_.mode == PlayMode::Strum && ! alreadyHeld)
    {
        const long long at = absCursor_ + offset;
        const bool byHand = lastStrokeKeyAbs_ >= 0
                         && at - lastStrokeKeyAbs_ < (long long) (kStrokeKeyHoldSec * sampleRate_);
        if (! byHand)
        {
            if (autoStrumAt_ < 0) { autoStrumAt_ = at + (long long) (kChordGatherSec * sampleRate_); autoStrumVel_ = 0.f; }
            autoStrumVel_ = std::max (autoStrumVel_, velocity);
        }
    }

    // Only Poly sounds directly; the other modes go through their own paths.
    if (p_.mode != PlayMode::Poly) return;

    // ---- articulation auto-detection -------------------------------------
    // Rules (fixed): see the constants at the top of this file. Each rule only
    // applies while its manual control is left at its default, so the
    // Damping / Transition / Harmonic buttons and keyswitches still override.
    const long long at   = absCursor_ + offset;
    const int       v127 = (int) std::lround (std::clamp (velocity, 0.f, 1.f) * 127.f);
    Articulation    art  = p_.art;

    // Strum: 3+ note-ons within kStrumWindowSec. A cluster that started earlier
    // than the window closes and this note opens a new one.
    const long long window = (long long) (kStrumWindowSec * sampleRate_);
    if (! alreadyHeld)
    {
        if (clusterCount_ == 0 || at - clusterFirst_ > window)
        {
            clusterCount_  = 0;
            clusterFirst_  = at;
            clusterNoised_ = false;
        }
        if (clusterCount_ < kClusterMax) clusterPitch_[clusterCount_] = pitch;
        ++clusterCount_;
    }
    const bool inStrum = clusterCount_ >= kStrumMinNotes;
    if (inStrum && ! clusterNoised_)
    {
        // Direction from pitch order: rising arrivals are a down-stroke.
        int up = 0, down = 0;
        const int m = std::min (clusterCount_, kClusterMax);
        for (int i = 1; i < m; ++i) (clusterPitch_[i] > clusterPitch_[i - 1] ? down : up) += 1;
        const double span = (double) (at - clusterFirst_) / sampleRate_;
        // A block chord (all within kBlockChordSec) still gets a short, fast scrape.
        emitStrokeNoise (down >= up ? 1 : -1, span < kBlockChordSec ? 0.02 : span * 1.5, velocity, clusterFirst_);
        clusterNoised_ = true;
    }

    // Mutes by velocity.
    if (art.damp == Damping::Open)
    {
        if (v127 <= kDeadMuteMaxVel)      art.damp = Damping::Dead;
        else if (v127 <= kPalmMuteMaxVel) art.damp = Damping::PalmMute;
    }

    // Natural harmonic: full velocity on a 12th / 19th / 24th-fret harmonic pitch.
    int harmString = -1, harmN = 0;
    if (art.harm == Harmonic::Off && v127 >= kHarmonicVel && ! alreadyHeld && ! inStrum)
        for (int s = 0; s < kNumStrings && harmString < 0; ++s)
        {
            const int rel = pitch - (int) std::lround (p_.open[s]);
            const int n = rel == 12 ? 2 : rel == 19 ? 3 : rel == 24 ? 4 : 0;
            if (n > 0 && rel <= p_.maxFret && soundingPitch_[s] < 0) { harmString = s; harmN = n; }
        }

    // Hammer-on / pull-off / slide: the new note lands on the string of a note
    // that is still held (and started at least kLegatoMinAgeSec ago, so chord
    // tones played together never count).
    int legatoString = -1, fromPitch = -1;
    bool slide = false;
    if (art.trans == Transition::Normal && ! alreadyHeld && ! inStrum && harmString < 0)
    {
        std::int64_t newest = -1;
        for (int i = 0; i < chord_.size(); ++i)
        {
            const int q = chord_.at (i).pitch;
            const int s = chord_.stringFor (q);
            if (q == pitch || s < 0 || s >= kNumStrings || soundingPitch_[s] != q) continue;
            if (at - noteStart_[q] < (long long) (kLegatoMinAgeSec * sampleRate_)) continue;
            const int d = pitch - q, fret = pitch - (int) std::lround (p_.open[s]);
            if (std::abs (d) < 1 || std::abs (d) > kSlideMaxSemis || fret < 0 || fret > p_.maxFret) continue;
            if (noteStart_[q] > newest) { newest = noteStart_[q]; legatoString = s; fromPitch = q; }
        }
        if (legatoString >= 0)
            slide = std::abs (pitch - fromPitch) > kHammerMaxSemis || v127 >= kSlideVel;
    }

    int stringIdx = chord_.stringFor (pitch);
    const int forced = harmString >= 0 ? harmString : legatoString;
    if (forced >= 0)
    {
        stringIdx = forced;
        chord_.setString (pitch, stringIdx, (float) (pitch - (int) std::lround (p_.open[stringIdx])));
    }

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

    const bool manualLegato = p_.art.trans == Transition::Legato && alreadyHeld;

    StrumHit h {};
    h.type         = manualLegato ? EventType::Legato : EventType::Pluck;
    h.stringIndex  = stringIdx;
    h.velocity     = velocity;
    h.art          = art;
    h.releaseSec   = p_.release;
    h.pitchFrom    = -1.f;
    h.delaySamples = 0.0;
    h.pitch        = p_.open[stringIdx];
    h.fret         = 0;

    if (forced >= 0)
    {
        h.pitch = (float) pitch;
        h.fret  = pitch - (int) std::lround (p_.open[stringIdx]);
    }
    else if (const int vi = voicingStringIndexOf (stringIdx); vi >= 0)
    {
        h.pitch = voicing_.pitch[vi];
        h.fret  = voicing_.fret[vi];

        if (art.harm == Harmonic::Natural)
        {
            const int partial = harmonicPartialForFret (h.fret);
            if (partial > 0) { h.harmonicN = partial; h.pitch = p_.open[stringIdx]; }
        }
    }

    if (harmString >= 0)
    {
        h.art.harm  = Harmonic::Natural;
        h.harmonicN = harmN;
        h.pitch     = p_.open[stringIdx];
    }
    else if (legatoString >= 0)
    {
        h.type      = EventType::Legato;
        h.art.trans = slide ? Transition::Slide : Transition::Legato;
        if (slide)
        {
            h.pitchFrom = (float) fromPitch;
            emitSqueak (stringIdx, pitch > fromPitch ? 1 : -1, velocity, at, std::abs (pitch - fromPitch));
        }
    }
    else if (p_.art.trans == Transition::Slide && ! alreadyHeld)
    {
        const int heldPitch = heldPitchOnString (stringIdx);
        if (heldPitch >= 0) h.pitchFrom = (float) heldPitch;
    }

    // Position shift: a new picked note 5+ frets from the last one, quickly,
    // drags the hand along the strings.
    // Within kShiftWindowSec of the last note-on, or of letting a note go:
    // releasing one position and landing the next is the usual way to shift.
    const long long shiftWin = (long long) (kShiftWindowSec * sampleRate_);
    if (h.type == EventType::Pluck && ! inStrum && lastOnAbs_ >= 0 && lastFret_ >= 0
        && (at - lastOnAbs_ < shiftWin || (lastChordOffAbs_ >= 0 && at - lastChordOffAbs_ < shiftWin))
        && std::abs (h.fret - lastFret_) >= kShiftMinFrets)
        emitSqueak (stringIdx, h.fret > lastFret_ ? 1 : -1, velocity * 0.6f, at, std::abs (h.fret - lastFret_));

    noteStart_[pitch & 127]   = at;
    soundingPitch_[stringIdx] = pitch;
    lastOnAbs_ = at;
    lastFret_  = h.fret;

    sounding_[(size_t) stringIdx] = true;

    // A chord is never struck at one instant: notes after the first inside the
    // strum window land 0-6 ms apart (scaled by Humanization). A single note
    // keeps zero added latency.
    long long hitAt = at;
    if (h.type == EventType::Pluck && ! alreadyHeld && clusterCount_ >= 2 && p_.humanize > 0.f)
        hitAt += (long long) ((double) rng_.unit() * 0.006 * (double) std::clamp (p_.humanize * 0.01f, 0.f, 1.f) * sampleRate_);
    pushHit (h, hitAt);
}

void PerformanceEngine::emitStrokeNoise (int dir, double durSec, float velocity, long long at)
{
    StrumHit n {};
    n.type     = EventType::StrokeNoise;
    n.dir      = dir;
    n.durSec   = (float) durSec;
    n.velocity = velocity;
    pushHit (n, at);
}

void PerformanceEngine::emitSqueak (int string, int dir, float velocity, long long at, int frets)
{
    StrumHit n {};
    n.type        = EventType::Squeak;
    n.stringIndex = string;
    n.dir         = dir;
    n.durSec      = (float) (0.04 + 0.012 * std::clamp (frets, 1, 12));   // a longer shift drags longer
    n.velocity    = velocity;
    pushHit (n, at);
}

void PerformanceEngine::noteOff (int pitch, int offset)
{
    if (chord_.isLatched (pitch)) return;

    if (p_.mode != PlayMode::Poly)
    {
        const long long at = absCursor_ + offset;
        const Voicing before = voicing_;
        const int ls = legatoString_[pitch & 127] - 1;
        legatoString_[pitch & 127] = 0;
        coveredBy_[pitch & 127] = 0;                    // a covered note lifted from under the finger

        int back = -1;
        for (int q = 0; q < 128; ++q)
            if (coveredBy_[q] == pitch + 1) { coveredBy_[q] = 0; back = q; }

        chord_.remove (pitch);
        refreshVoicing();

        // Releasing a hammered / slid note with the note under it still held
        // pulls off to it on the same string.
        if (ls >= 0 && back >= 0 && chord_.contains (back))
        {
            StrumHit h {};
            h.type        = EventType::Legato;
            h.stringIndex = ls;
            h.velocity    = 0.7f;
            h.art         = chordArt();
            h.art.trans   = Transition::Legato;
            h.releaseSec  = p_.release;
            h.pitch       = (float) back;
            h.pitchFrom   = -1.f;
            h.fret        = back - (int) std::lround (p_.open[ls]);
            pushHit (h, at);
            return;
        }

        // The whole chord let go: the fretting hand lifts off every string it
        // held, damping them with the finger-lift noise.
        if (chord_.size() == 0)
        {
            lastChordOffAbs_ = at;
            bool released[kNumStrings] {};
            for (int vi = 0; vi < before.count; ++vi) released[before.stringIndex[vi]] = true;
            for (int st = 0; st < kNumStrings; ++st) if ((frettedMask_ >> st) & 1u) released[st] = true;
            frettedMask_ = 0;
            if (ls >= 0) released[ls] = true;
            for (int st = 0; st < kNumStrings; ++st)
            {
                if (! released[st]) continue;
                StrumHit h {};
                h.type        = EventType::Release;
                h.stringIndex = st;
                h.velocity    = 0.f;
                h.art         = p_.art;
                h.releaseSec  = p_.release;
                h.pitch       = p_.open[st];
                h.pitchFrom   = -1.f;
                pushHit (h, at);
            }
        }
        return;
    }

    const int stringIdx = chord_.stringFor (pitch);

    // Another held note on the same string: releasing the sounding one pulls
    // off (or hammers back) to it, as on a real string. Automatic unless the
    // Transition control is set to something other than Normal/Legato.
    int back = -1;
    if (p_.mode == PlayMode::Poly && stringIdx >= 0 && stringIdx < kNumStrings
        && (p_.art.trans == Transition::Legato || p_.art.trans == Transition::Normal))
    {
        std::int64_t newest = -1;
        for (int i = 0; i < chord_.size(); ++i)
        {
            const int q = chord_.at (i).pitch;
            if (q != pitch && chord_.stringFor (q) == stringIdx && noteStart_[q & 127] > newest)
            { newest = noteStart_[q & 127]; back = q; }
        }
    }
    const bool wasSounding = stringIdx >= 0 && stringIdx < kNumStrings && soundingPitch_[stringIdx] == pitch;

    chord_.remove (pitch);
    refreshVoicing();
    lastChordOffAbs_ = absCursor_ + offset;   // Poly: any let-go starts the shift window

    if (stringIdx >= 0 && stringIdx < kNumStrings && ! chord_.stringIsHeld (stringIdx))
        sounding_[(size_t) stringIdx] = false;

    if (p_.mode != PlayMode::Poly || stringIdx < 0 || stringIdx >= kNumStrings) return;

    // The string already moved on to a newer note: nothing to release.
    if (! wasSounding) return;

    StrumHit h {};
    h.stringIndex  = stringIdx;
    h.art          = p_.art;
    h.releaseSec   = p_.release;
    h.pitchFrom    = -1.f;
    h.delaySamples = 0.0;

    if (back >= 0)
    {
        h.type      = EventType::Legato;
        h.art.trans = Transition::Legato;
        h.pitch     = (float) back;
        h.velocity  = 0.7f;
        soundingPitch_[stringIdx] = back;
    }
    else
    {
        h.type     = EventType::Release;
        h.pitch    = (float) pitch;
        h.velocity = 0.f;
        soundingPitch_[stringIdx] = -1;
    }

    pushHit (h, absCursor_ + offset);
}

void PerformanceEngine::strokeKey (ks::Stroke stroke, float velocity, int offset)
{
    if (! autoStroke_) { lastStrokeKeyAbs_ = absCursor_ + offset; autoStrumAt_ = -1; }
    refreshVoicing();
    const Articulation art = chordArt();
    const Voicing      v   = playVoicing (art);
    if (v.count <= 0) return;

    StrumEngine::StrokeParams sp {};
    sp.strumTimeMs  = p_.strumTime;
    sp.timeOffsetMs = p_.timeOffset;
    sp.separation01 = p_.separation;
    sp.velOffset    = p_.velOffset;
    sp.strokeVol    = p_.strokeVol;
    sp.humanize01   = p_.humanize;
    sp.skipLowest   = bassHeld_;

    const int n = (stroke == ks::Stroke::PickLowest || stroke == ks::Stroke::PickHighest)
        ? StrumEngine::pluckExtreme (stroke == ks::Stroke::PickHighest,
                                     v, p_.open, art, velocity,
                                     scratch_, kMaxHits)
        : strum_.stroke (isUpward (stroke), kindOf (stroke),
                         (int) std::lround (velocity * 127.f), sp,
                         v, p_.open, art, rng_, scratch_, kMaxHits);

    double lastDelay = 0.0;
    for (int i = 0; i < n; ++i)
    {
        scratch_[i].releaseSec = p_.release;
        lastDelay = std::max (lastDelay, scratch_[i].delaySamples);
        pushHit (scratch_[i], absCursor_ + offset + (long long) scratch_[i].delaySamples);
    }

    // A stroke drags the pick across the strings: add the scrape.
    if (n >= 2 && stroke != ks::Stroke::PickLowest && stroke != ks::Stroke::PickHighest)
        emitStrokeNoise (isUpward (stroke) ? -1 : 1, std::max (0.02, lastDelay / sampleRate_ * 1.5), velocity,
                         absCursor_ + offset);
}

//==============================================================================
void PerformanceEngine::handleMidi (const MidiMsg& m)
{
    const int offset = m.offset;
    const int type   = m.status & 0xf0;
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
                if (ks::stroke (note) == ks::Stroke::PickLowest) bassHeld_ = true;
                strokeKey (ks::stroke (note), (float) m.d2 / 127.f, offset);
            }
            return;
        }

        if (note >= kLowestPlayable)
            noteOn (note + p_.transpose - kWrittenOctave, (float) m.d2 / 127.f, offset);

        return;
    }

    if (type == 0x80 || (type == 0x90 && m.d2 == 0))
    {
        // Stroke keys are momentary, so their note-off only ends a held bass
        // note for bass-and-strum; latch groups toggle on note-on and
        // deliberately ignore note-off.
        if (ks::isKeyswitch (note))
        {
            if (ks::stroke (note) == ks::Stroke::PickLowest) bassHeld_ = false;
            return;
        }
        if (note >= kLowestPlayable) noteOff (note + p_.transpose - kWrittenOctave, offset);
        return;
    }

    // Controllers dispatch on the controller number (d1), on any channel.
    // Comparing the channel here meant CC64 never latched and every CC on
    // channel 1 was taken for the mod wheel.
    if (type == 0xb0 && note == 1) { modWheel_ = (double) m.d2 / 127.0; return; }

    if (type == 0xb0 && note == 64)
    {
        pedalDown_ = m.d2 >= 64;
        if (pedalDown_) chord_.setLatched (true);
        else if (! p_.hold) chord_.releaseLatched();
        refreshVoicing();
        return;
    }

    if (type == 0xe0)
    {
        // 14-bit pitch bend centred at 8192, spanning +/- Wheel Range semitones.
        const int raw = (int) m.d1 | ((int) m.d2 << 7);
        bendSemis_ = ((double) (raw - 8192) / 8192.0) * (double) p_.wheelRange;
        return;
    }

    // Aftertouch bends up, like pressing a string sideways. Channel pressure
    // carries its value in d1, polyphonic pressure in d2 (the highest key
    // wins). Below a small threshold nothing happens, so the weight of a
    // resting finger never detunes a held chord.
    if (type == 0xd0 || type == 0xa0)
    {
        const int pressure = type == 0xd0 ? (int) m.d1 : (int) m.d2;
        constexpr int kThreshold = 24;
        const double frac = pressure <= kThreshold ? 0.0 : (double) (pressure - kThreshold) / (127.0 - kThreshold);
        aftertouchSemis_ = frac * (double) p_.atBendRange;
    }
}

//==============================================================================
void PerformanceEngine::emitArpAndSeq (double ppqStart, double ppqEnd)
{
    if (! (ppqEnd >= ppqStart)) return;

    const double bpm = clock_.bpm();
    const Articulation art = chordArt();
    const Voicing      v   = playVoicing (art);

    if (p_.mode == PlayMode::Arp)
    {
        arp_.rebuild (v, p_.arpMask, p_.arpStrings);

        // Velocity per voiced string. Held notes only carry a string index in
        // Poly mode, so map through the voicing's pitches instead; reading
        // ChordNote::string here left every arp note at velocity 0.
        float stringVel[kNumStrings] {};
        for (int vi = 0; vi < v.count; ++vi)
        {
            const int s = v.stringIndex[vi];
            if (s < 0 || s >= kNumStrings) continue;

            // A guitar shape sounds pitches nobody holds: match the same
            // pitch, then the same pitch class, then the loudest held note.
            float exact = -1.f, sameClass = -1.f, loudest = 0.f;
            const int pc = (((int) std::lround (v.pitch[vi]) % 12) + 12) % 12;
            for (int i = 0; i < chord_.size(); ++i)
            {
                const ChordNote& n = chord_.at (i);
                loudest = std::max (loudest, n.velocity);
                if (std::fabs ((float) n.pitch - v.pitch[vi]) < 0.5f) exact = n.velocity;
                else if (sameClass < 0.f && (n.pitch % 12 + 12) % 12 == pc) sameClass = n.velocity;
            }
            stringVel[s] = exact >= 0.f ? exact : sameClass >= 0.f ? sameClass : loudest;
        }

        ArpEngine::Params ap {};
        ap.rate       = p_.arpRate;
        ap.order      = p_.arpOrder;
        ap.gate01     = p_.arpGate  * 0.01f;
        ap.swing01    = p_.arpSwing * 0.01f;
        ap.strokeVol  = p_.strokeVol;
        ap.humanize01 = p_.humanize;

        const int n = arp_.advance (ppqStart, ppqEnd, sampleRate_, bpm, ap,
                                    p_.open, stringVel, art, rng_,
                                    scratch_, kMaxHits);
        for (int i = 0; i < n; ++i)
        {
            scratch_[i].releaseSec = p_.release;
            pushHit (scratch_[i], absCursor_ + (long long) scratch_[i].delaySamples);
        }
        return;
    }

    // Seq runs with Seq Play / the host transport, or while a chord is held.
    if (p_.mode != PlayMode::Seq || (! p_.seqPlay && chord_.size() == 0) || p_.seqSlots == nullptr) return;

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
                                &p_.seqSlots[slot], v, sp, strum_, p_.open,
                                p_.release, art, rng_, scratch_, kMaxHits,
                                std::clamp (p_.arpSwing, 0.f, 60.f) * 0.01f);

    for (int i = 0; i < n; ++i)
        pushHit (scratch_[i], absCursor_ + (long long) scratch_[i].delaySamples);

    // Sequenced strokes are strums too: one scrape per stroke group (hits that
    // start within the strum window), direction from string order.
    for (int i = 0; i < n; )
    {
        int j = i + 1;
        while (j < n && scratch_[j].delaySamples - scratch_[i].delaySamples < kStrumWindowSec * sampleRate_) ++j;
        if (j - i >= 2 && scratch_[i].type == EventType::Pluck)
        {
            const double span = (scratch_[j - 1].delaySamples - scratch_[i].delaySamples) / sampleRate_;
            emitStrokeNoise (scratch_[j - 1].stringIndex >= scratch_[i].stringIndex ? 1 : -1,
                             std::max (0.02, span * 1.5), scratch_[i].velocity,
                             absCursor_ + (long long) scratch_[i].delaySamples);
        }
        i = j;
    }
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

    // Beats sit at absolute sample positions k * spq (k >= 1) since the
    // metronome started. This block covers [blockStart, blockEnd); each beat
    // inside it becomes one click at its offset from blockStart. The old code
    // compared a block-relative position with the absolute counter, so after
    // the first beat it emitted kMaxClicks clicks at offset 0 every block.
    const double blockStart = metAccum_;
    const double blockEnd   = blockStart + (double) numSamples;
    metAccum_ = blockEnd;

    const int sig = lastSignature_ >= 0 && lastSignature_ < Clock::kNumSignature
                  ? lastSignature_ : 0;
    const int beatsPerBar = Clock::kSignatureNum[sig];

    // One click per beat of the signature: quarter notes in x/4, eighths in x/8
    // (6/8 clicks six times a bar, not six quarters).
    const double beatSamples = spq * 4.0 / (double) Clock::kSignatureDen[sig];
    const double firstK = std::max (1.0, std::ceil (blockStart / beatSamples));

    for (double beat = firstK * beatSamples; beat < blockEnd && numClicks < kMaxClicks; beat += beatSamples)
    {
        int off = (int) (beat - blockStart);
        if (off < 0) off = 0;
        if (off > numSamples - 1) off = numSamples - 1;

        ClickEvent& c = clicks[numClicks++];
        c.sampleOffset = off;
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

    // Automatic vibrato: one or two notes held in Poly for kAutoVibDelaySec.
    {
        const bool want = chord_.size() >= 1 && chord_.size() <= 2 && lastOnAbs_ >= 0
                       && (double) (absCursor_ - lastOnAbs_) > kAutoVibDelaySec * sampleRate_;
        const double step = kAutoVibDepth * (double) numSamples / (kAutoVibRiseSec * sampleRate_);
        autoVib_ = want ? std::min (kAutoVibDepth, autoVib_ + step) : 0.0;
    }

    const bool chordActive = ! chord_.size() == 0;

    double ppqStart = clock_.ppq();
    if (transport.hostValid && transport.hostPlaying) ppqStart = transport.ppq;

    // Position-shift squeak for chord modes, once the new chord has gathered.
    if (shiftCheckAt_ >= 0 && shiftCheckAt_ < absCursor_ + numSamples)
    {
        const long long at = shiftCheckAt_;
        shiftCheckAt_ = -1;
        if (p_.mode != PlayMode::Poly && voicing_.count > 0)
        {
            float sum = 0.f; int lowest = kNumStrings;
            for (int vi = 0; vi < voicing_.count; ++vi) { sum += (float) voicing_.fret[vi]; lowest = std::min (lowest, voicing_.stringIndex[vi]); }
            const float hand = sum / (float) voicing_.count;
            const int frets = (int) std::lround (std::fabs (hand - lastHand_));
            if (shiftQuick_ && lastHand_ >= 0.f && frets >= kShiftMinFrets)
                emitSqueak (std::min (lowest, 2), hand > lastHand_ ? 1 : -1, 0.6f, at, frets);
            lastHand_ = hand;
        }
    }

    // Pending Strum-mode auto-strum due in this block.
    if (autoStrumAt_ >= 0 && autoStrumAt_ < absCursor_ + numSamples)
    {
        const int off = (int) std::max<long long> (0, autoStrumAt_ - absCursor_);
        autoStrumAt_ = -1;
        if (p_.mode == PlayMode::Strum && chord_.size() > 0)
        {
            autoStroke_ = true;
            strokeKey (ks::Stroke::Down, autoStrumVel_, off);
            autoStroke_ = false;
        }
    }

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

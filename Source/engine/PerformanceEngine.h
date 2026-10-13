#pragma once
#include "Contracts.h"
#include "engine/ArpEngine.h"
#include "engine/ChordDetect.h"
#include "engine/ChordState.h"
#include "engine/Clock.h"
#include "engine/KeyswitchMap.h"
#include "engine/SeqEngine.h"
#include "engine/SeqPattern.h"
#include "engine/StrumEngine.h"
#include "engine/Voicing.h"

#include <array>
#include <cstdint>

namespace fw {

// Cross-slice UI contract. Field for field identical to the published contract.
struct UiState
{
    // -1 means "idle". Default it to that rather than leaving {} (which is 0,
    // i.e. the open string) so a path that forgets to write all six cannot make
    // the fretboard show an open string for a string that is not sounding.
    UiState() noexcept { for (auto& f : fret) f = -1; }

    int   fret[kNumStrings] {};
    int   seqStep = -1;
    int   ks[6] {};
    std::uint32_t ksNotes = 0;
    std::uint32_t ksDirtyMask = 0;
    float modWheel = 0.f;
    float pitchBendSemis = 0.f;
    int   chordName = -1;      // ChordName::pack() of the guitar-voiced chord, -1 when playing as held
};

struct ClickEvent { int sampleOffset = 0; bool accent = false; };

struct PerfParams
{
    float strumTime  = 0.f;
    float timeOffset = 0.f;
    float separation = 0.f;
    float velOffset  = 0.f;
    float strokeVol  = 1.f;
    float humanize   = 0.f;
    float release    = 8.f;

    int   quantize   = 1;
    int   signature  = 2;
    float bpm        = 100.f;

    int   transpose  = 0;
    int   wheelRange = 2;          // pitch-wheel span in semitones, each way
    float atBendRange = 2.f;       // full aftertouch bends up this many semitones (Bend Amount)

    bool  hold       = false;
    int   voicingMode = 0;     // 0 Auto, 1 Played, 2 Guitar (Params "Voicing")
    PlayMode mode    = PlayMode::Poly;

    Articulation art {};

    int   arpStrings = 6;
    bool  arpMask[6]  { true, true, true, true, true, true };
    int   arpOrder    = 0;
    int   arpRate     = 3;
    float arpGate     = 80.f;
    float arpSwing    = 0.f;

    float tremRate = 16.f;

    float open[kNumStrings] { 40.f, 45.f, 50.f, 55.f, 59.f, 64.f };

    int   maxFret  = 20;

    bool seqPlay    = false;
    int  seqSlot    = 0;
    bool metronome  = false;

    const SeqPattern* seqSlots = nullptr;
};

// Headless performance engine: MIDI in, StringEvent and ClickEvent out.
// Allocation free and deterministic; events are stamped with an absolute
// sample position and parked in a fixed queue so strum spreads, arp gates and
// sequencer releases survive block boundaries untouched.
class PerformanceEngine
{
public:
    static constexpr int kQueueCapacity = 512;

    void prepare (double sampleRate);
    void reset();

    void setParams (const PerfParams& p);
    const PerfParams& params() const noexcept { return p_; }

    int  process (const MidiMsg* in, int numIn, const Transport& transport,
                  int numSamples, StringEvent* out, int outCapacity,
                  ClickEvent* clicks, int& numClicks);

    UiState ui() const { return ui_; }

private:
    static constexpr int kMaxHits   = kNumStrings * 2;
    static constexpr int kMaxClicks = 64;

    struct QueuedEvent
    {
        std::int64_t abs = 0;
        StringEvent  ev  {};
    };

    static StrokeKind kindOf (ks::Stroke s) noexcept;
    static bool isUpward (ks::Stroke s) noexcept;

    void handleMidi (const MidiMsg& m);
    void noteOn     (int pitch, float velocity, int offset);
    void noteOff    (int pitch, int offset);
    void strokeKey  (ks::Stroke stroke, float velocity, int offset);

    int   heldPitchOnString (int s) const;
    int   voicingStringIndexOf (int s) const noexcept;
    float fretOn (int stringIndex, float pitch) const noexcept;

    void refreshVoicing();
    void refreshUi();
    void emitArpAndSeq (double ppqStart, double ppqEnd);
    void pollTremolo   (int numSamples);
    void pollMetronome (int numSamples, ClickEvent* clicks, int& numClicks);

    void toEvent (const StrumHit& hit, StringEvent& ev) const;
    void pushHit (const StrumHit& hit, long long at);
    int  drain (int numSamples, StringEvent* out, int outCapacity);

    PerfParams p_ {};
    double     sampleRate_ = 44100.0;
    bool       prepared_   = false;

    std::int64_t absCursor_ = 0;
    std::array<QueuedEvent, kQueueCapacity> queue_ {};
    int queueCount_ = 0;

    ChordState  chord_ {};
    Voicing     voicing_ {};
    Voicing     lastShape_ {};           // previous guitar shape, for voice leading
    ChordName   shapedChord_ {};         // chord currently voiced as a shape (invalid = as held)
    int         voicingKey_[ChordState::kCapacity + 4] {};   // cache key of the last refreshVoicing
    int         voicingKeyLen_ = -1;
    StrumEngine strum_ {};
    ArpEngine   arp_ {};
    SeqEngine   seq_ {};
    Clock       clock_ {};
    Rng         rng_ {};

    bool sounding_[kNumStrings] {};

    int             ks_[6] {};
    std::uint32_t   ksNotes_         = 0;
    std::uint32_t   ksDirty_         = 0;
    bool            keyswitchesUsed_ = false;
    bool            pedalDown_      = false;
    bool bassHeld_ = false;   // Pick Lowest key held: strokes leave the bass string out

    double   modWheel_  = 0.f;
    double   bendSemis_ = 0.f;
    double   aftertouchSemis_ = 0.0;   // upward bend from channel / poly pressure
    double   tremAccum_ = 0.0;
    double   metAccum_  = 0.0;
    long long metBeat_  = 0;

    int lastQuantize_  = -1;
    int lastSignature_ = -1;
    int lastMode_      = -1;

    StrumHit scratch_[kMaxHits] {};
    UiState  ui_ {};

    // ---- articulation auto-detection (Poly, live MIDI) -------------------
    static constexpr int kClusterMax = 8;
    std::int64_t clusterFirst_ = 0;          // first note-on of the current strum window
    int          clusterCount_ = 0;
    int          clusterPitch_[kClusterMax] {};
    bool         clusterNoised_ = false;     // stroke noise already emitted for this cluster
    std::int64_t noteStart_[128] {};         // absolute sample of each pitch's last note-on
    int          soundingPitch_[kNumStrings] { -1, -1, -1, -1, -1, -1 };   // pitch each string is playing
    std::int64_t lastOnAbs_ = -1;            // last Poly note-on
    int          lastFret_  = -1;
    double       autoVib_   = 0.0;           // automatic vibrato depth, 0..1 like the mod wheel

    void emitStrokeNoise (int dir, double durSec, float velocity, long long at);
    void emitSqueak (int string, int dir, float velocity, long long at);
};

} // namespace fw

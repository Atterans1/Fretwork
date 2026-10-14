
#pragma once
#include "Contracts.h"

namespace fw {

// Tempo / step clock. Host-locked when the host supplies a valid playing
// transport, otherwise an internal ppq counter advanced by the bpm parameter.
//
// The internal counter only advances while a consumer actually wants time to
// pass: Seq mode with Seq Play, or Arp mode with a non-empty chord. It resets
// to 0 when it starts.
class Clock
{
public:
    // Param indices, matching createLayout() exactly.
    enum Quantize  { Eighth = 0, Sixteenth, ThirtySecond, kNumQuantize };
    enum Signature { Sig2x4 = 0, Sig3x4, Sig4x4, Sig5x4, Sig6x8, Sig7x8, Sig12x8, Sig8x4, Sig9x8, kNumSignature };   // appended: saved indices keep their meaning

    static constexpr double kStepsPerQuarter[kNumQuantize]  = { 2.0, 4.0, 8.0 };
    static constexpr int    kSignatureNum[kNumSignature]     = { 2, 3, 4, 5, 6, 7, 12, 8, 9 };
    static constexpr int    kSignatureDen[kNumSignature]     = { 4, 4, 4, 4, 8, 8,  8, 4, 8 };
    static constexpr int    kMaxStepsPerBar                  = 32;

    void prepare (double sampleRate) noexcept;

    void setTempo (double bpmParam)      noexcept { bpmParam_ = bpmParam; }
    void setQuantize (int index)         noexcept;
    void setSignature (int index)        noexcept;

    // Samples per quarter note at the current tempo.
    double samplesPerQuarter() const noexcept;

    // stepsPerBar for the current quantize and signature, with the quantize
    // reduced one notch at a time until the bar fits in kMaxStepsPerBar.
    int    stepsPerBar() const noexcept { return stepsPerBar_; }
    int    stepsPerQuarterInt() const noexcept;

    // Feeds the host transport and the gating state, then advances the internal
    // counter by numSamples. Returns true when the clock is running.
    bool advance (int numSamples,
                  const Transport& transport,
                  PlayMode mode,
                  bool seqPlay,   // Seq also runs while a chord is held
                  bool chordActive) noexcept;

    // True when the host is driving.
    bool hostLocked() const noexcept { return hostLocked_; }

    double ppq() const noexcept { return ppq_; }
    double bpm() const noexcept { return hostLocked_ ? hostBpm_ : bpmParam_; }
    bool   running() const noexcept { return running_; }

    // Samples elapsed for one step of the current quantize.
    double samplesPerStep() const noexcept;

    // Current integer step index, floor(ppq / stepsPerQuarter).
    int    step() const noexcept;

    // Sample position of a given step index from the current ppq.
    double sampleAtStep (int stepIndex) const noexcept;

    // ppq the clock will hold at sample offset absOffset samples into the block
    // that advance() is about to consume.
    double ppqAt (int numSamples, int absOffset) const noexcept;

    // Force the internal counter back to zero and mark the clock stopped.
    void reset() noexcept;

private:
    void recomputeBar() noexcept;

    double sampleRate_   = 44100.0;
    double bpmParam_     = 100.0;
    double hostBpm_      = 100.0;
    double ppq_          = 0.0;
    double ppqBlockStart_ = 0.0;
    bool   hostLocked_   = false;
    bool   running_      = false;
    int    quantize_     = Sixteenth;
    int    signature_    = Sig4x4;
    int    stepsPerBar_  = 16;
};

} // namespace fw

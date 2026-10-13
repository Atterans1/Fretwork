
#include "engine/Clock.h"

#include <cmath>
#include <cstddef>

namespace fw {

void Clock::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    ppq_ = 0.0;
    ppqBlockStart_ = 0.0;
    hostLocked_ = false;
    running_ = false;
    recomputeBar();
}

void Clock::reset() noexcept
{
    ppq_ = 0.0;
    ppqBlockStart_ = 0.0;
    running_ = false;
}

void Clock::setQuantize (int index) noexcept
{
    quantize_ = (index < 0) ? 0 : (index >= kNumQuantize ? kNumQuantize - 1 : index);
    recomputeBar();
}

void Clock::setSignature (int index) noexcept
{
    signature_ = (index < 0) ? 0 : (index >= kNumSignature ? kNumSignature - 1 : index);
    recomputeBar();
}

int Clock::stepsPerQuarterInt() const noexcept
{
    return (int) kStepsPerQuarter[quantize_];
}

void Clock::recomputeBar() noexcept
{
    int q = quantize_;
    int spb = 0;

    for (;;)
    {
        const double quarters = (double) kSignatureNum[signature_] * 4.0
                              / (double) kSignatureDen[signature_];
        spb = (int) std::lround (quarters * kStepsPerQuarter[q]);
        if (spb <= kMaxStepsPerBar) break;
        if (q == 0) break;              // 1/8 is the coarsest notch available
        --q;                            // reduce quantize one notch and recompute
    }

    // The effective quantize follows whatever notch finally fit.
    quantize_ = q;
    stepsPerBar_ = spb > 0 ? spb : 1;
}

double Clock::samplesPerQuarter() const noexcept
{
    const double tempo = bpm();
    if (! (tempo > 0.0)) return sampleRate_ * 60.0 / 100.0;
    return sampleRate_ * 60.0 / tempo;
}

double Clock::samplesPerStep() const noexcept
{
    return samplesPerQuarter() / kStepsPerQuarter[quantize_];
}

int Clock::step() const noexcept
{
    if (! (ppq_ > 0.0)) return 0;
    return (int) std::floor (ppq_ * kStepsPerQuarter[quantize_]);
}

double Clock::sampleAtStep (int stepIndex) const noexcept
{
    return ((double) stepIndex / kStepsPerQuarter[quantize_] - ppq_) * samplesPerQuarter();
}

double Clock::ppqAt (int numSamples, int absOffset) const noexcept
{
    if (numSamples <= 0) return ppqBlockStart_;
    const double spq = samplesPerQuarter();
    return ppqBlockStart_ + (double) absOffset / spq;
}

bool Clock::advance (int numSamples,
                     const Transport& transport,
                     PlayMode mode,
                     bool seqPlay,
                     bool chordActive) noexcept
{
    hostLocked_ = transport.hostValid && transport.hostPlaying;
    if (hostLocked_)
        hostBpm_ = transport.bpm > 0.0 ? transport.bpm : 100.0;

    // The internal counter only runs for the two consumers that read it.
    const bool wants = hostLocked_
                    || (mode == PlayMode::Seq && seqPlay)
                    || (mode == PlayMode::Arp && chordActive);

    if (! wants)
    {
        running_ = false;
        ppq_ = 0.0;
        ppqBlockStart_ = 0.0;
        return false;
    }

    // Starting from a stop resets to zero.
    if (! running_) ppq_ = 0.0;
    running_ = true;

    ppqBlockStart_ = hostLocked_ ? transport.ppq : ppq_;
    ppq_ = ppqBlockStart_;

    if (numSamples > 0)
        ppq_ += (double) numSamples / samplesPerQuarter();

    return true;
}

} // namespace fw

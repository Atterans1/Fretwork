
#include "engine/ArpEngine.h"

#include <cmath>
#include <cstddef>

namespace fw {

void ArpEngine::rebuild (const Voicing& v, const bool* mask, int arpStrings)
{
    poolCount_ = 0;
    if (mask == nullptr) return;

    PoolEntry tmp[kNumStrings];
    int n = 0;
    for (int i = 0; i < v.count && n < kNumStrings; ++i)
    {
        const int s = v.stringIndex[i];
        if (s < 0 || s >= kNumStrings) continue;
        if (! mask[s]) continue;
        tmp[n].stringIndex = s;
        tmp[n].pitch       = v.pitch[i];
        tmp[n].fret        = v.fret[i];
        ++n;
    }

    // Insertion sort by pitch ascending; ties fall back to the voicing order.
    for (int i = 1; i < n; ++i)
    {
        const PoolEntry key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].pitch > key.pitch) { tmp[j + 1] = tmp[j]; --j; }
        tmp[j + 1] = key;
    }

    int keep = n;
    if (arpStrings < keep) keep = arpStrings > 0 ? arpStrings : 0;

    for (int i = 0; i < keep; ++i)
        pool_[(std::size_t) i] = tmp[i];
    poolCount_ = keep;

    if (cursor_ >= poolCount_) cursor_ = 0;
}

int ArpEngine::advance (double ppqStart,
                        double ppqEnd,
                        double sampleRate,
                        double bpm,
                        const Params& p,
                        const float* open,
                        const float* stringVel,
                        Articulation art,
                        Rng& jitterRng,
                        StrumHit* out,
                        int cap)
{
    if (poolCount_ <= 0 || out == nullptr || cap <= 0) return 0;
    if (! (bpm > 0.0) || ! (sampleRate > 0.0)) return 0;
    if (! (ppqEnd >= ppqStart)) return 0;

    const int    rate      = (p.rate < 0) ? 0 : (p.rate >= 6 ? 5 : p.rate);
    const double stepLen   = kStepLenQuarters[rate];
    const double spq       = sampleRate * 60.0 / bpm;
    const double stepSamples = stepLen * spq;

    // A restarted clock restarts step numbering from zero.
    if (ppqStart < lastPpq_ - 1e-9) lastStep_ = -1;
    lastPpq_ = ppqEnd;

    int written = 0;

    // Gate releases whose window falls inside this block, oldest first.
    for (int i = 0; i < gateCount_; ++i)
    {
        const PendingGate g = gates_[(std::size_t) i];
        if (g.endPpq >= ppqEnd) continue;

        if (written < cap)
        {
            StrumHit& r = out[written];
            r.type         = EventType::Release;
            r.stringIndex  = g.stringIndex;
            r.delaySamples = (g.endPpq - ppqStart) * spq;
            if (r.delaySamples < 0.0) r.delaySamples = 0.0;
            r.velocity     = 0.f;
            r.pitch        = g.pitch;
            r.pitchFrom    = -1.f;
            r.fret         = g.fret;
            r.harmonicN    = g.harmonicN;
            r.art          = g.art;
            ++written;
        }
        for (int k = i; k + 1 < gateCount_; ++k)
            gates_[(std::size_t) k] = gates_[(std::size_t) k + 1];
        --gateCount_;
        --i;
    }

    const long long firstStep  = (long long) std::floor (ppqStart / stepLen);
    const long long endStep   = (long long) std::floor (ppqEnd   / stepLen);
    long long       emittedTo = lastStep_;
    const int    n     = poolCount_;
    const float  h     = p.humanize01 < 0.f ? 0.f : (p.humanize01 > 1.f ? 1.f : p.humanize01);
    const double jitS  = 4.0 * (double) h * sampleRate * 0.001;

    for (long long step = firstStep; step <= endStep && written < cap; ++step)
    {
        if (step <= lastStep_) continue;   // already served in an earlier block

        const double stepStartPpq = (double) step * stepLen;

        int idx = 0;
        switch (p.order)
        {
            case Down:
                cursor_ = (cursor_ <= 0) ? (n - 1) : (cursor_ - 1);
                idx = cursor_;
                break;

            case UpDown:
                if (n <= 1) { idx = 0; }
                else
                {
                    // Turning at an end steps back one note, so no end repeats.
                    idx  = cursor_;
                    int nx = cursor_ + direction_;
                    if (nx >= n)     { nx = n - 2; direction_ = -1; }
                    else if (nx < 0) { nx = 1;     direction_ = 1;  }
                    cursor_ = nx;
                }
                break;

            case Random:
                idx = (int) (rng_.unit() * (float) n);
                if (idx >= n) idx = n - 1;
                if (idx < 0)  idx = 0;
                break;

            case Up:
            default:
                idx = cursor_;
                cursor_ = (cursor_ + 1 >= n) ? 0 : (cursor_ + 1);
                break;
        }

        const PoolEntry& pe = pool_[(std::size_t) idx];

        const double swing  = (step & 1LL) ? (double) p.swing01 * stepSamples : 0.0;
        const double jitter = (double) jitterRng.range (-1.f, 1.f) * jitS;

        double delay = (stepStartPpq - ppqStart) * spq + swing + jitter;
        if (delay < 0.0) delay = 0.0;

        const int partial = (art.harm == Harmonic::Natural && open != nullptr)
                              ? harmonicPartialForFret (pe.fret) : 0;

        StrumHit& h2 = out[written];
        h2.type         = EventType::Pluck;
        h2.stringIndex  = pe.stringIndex;
        h2.delaySamples = delay;
        h2.velocity     = (stringVel != nullptr ? stringVel[pe.stringIndex] : 0.f) * p.strokeVol;
        if (h2.velocity < 0.f) h2.velocity = 0.f;
        h2.pitch        = (partial > 0) ? open[pe.stringIndex] : pe.pitch;
        h2.pitchFrom    = -1.f;
        h2.fret         = pe.fret;
        h2.harmonicN    = partial;
        h2.art          = art;
        ++written;
        emittedTo = step;

        // Park the gate release for whichever block contains it.
        const double endPpq = stepStartPpq + (double) p.gate01 * stepLen;
        if (gateCount_ >= kMaxPendingGates)
        {
            // Overflow: drop the oldest, deterministically.
            for (int k = 0; k + 1 < gateCount_; ++k)
                gates_[(std::size_t) k] = gates_[(std::size_t) k + 1];
            --gateCount_;
        }
        PendingGate& g = gates_[(std::size_t) gateCount_++];
        g.endPpq      = endPpq;
        g.stringIndex = pe.stringIndex;
        g.pitch       = h2.pitch;
        g.fret        = pe.fret;
        g.harmonicN   = h2.harmonicN;
        g.art         = art;
    }

    // Only steps actually emitted count as served, so a capped block
    // still produces its remaining steps in the next one.
    if (emittedTo > lastStep_) lastStep_ = emittedTo;
    return written;
}

} // namespace fw


#include "engine/SeqEngine.h"

#include <cmath>
#include <cstddef>

namespace fw {

void SeqEngine::reset() noexcept
{
    pendingCount_ = 0;
    lastStep_     = -1;
    lastPpq_      = 0.0;
    currentStep_  = -1;
}

void SeqEngine::pushRelease (long long step, int stringIndex) noexcept
{
    if (pendingCount_ >= kMaxPendingReleases)
    {
        // Fixed capacity: drop the oldest so behaviour stays deterministic.
        for (int k = 0; k + 1 < pendingCount_; ++k)
            pending_[(std::size_t) k] = pending_[(std::size_t) k + 1];
        --pendingCount_;
    }
    PendingRelease& r = pending_[(std::size_t) pendingCount_++];
    r.step        = step;
    r.stringIndex = stringIndex;
}

int SeqEngine::advance (double ppqStart,
                        double ppqEnd,
                        double sampleRate,
                        double bpm,
                        double stepsPerQuarter,
                        int stepsPerBar,
                        const SeqPattern* pattern,
                        const Voicing& voicing,
                        const StrumEngine::StrokeParams& strokeParams,
                        const StrumEngine& strum,
                        const float* open,
                        float releaseSec,
                        Articulation art,
                        Rng& rng,
                        StrumHit* out,
                        int cap)
{
    if (out == nullptr || cap <= 0) return 0;
    if (pattern == nullptr || pattern->count <= 0) { currentStep_ = -1; return 0; }
    if (! (bpm > 0.0) || ! (sampleRate > 0.0) || ! (stepsPerQuarter > 0.0)) return 0;
    if (! (ppqEnd >= ppqStart)) return 0;
    if (stepsPerBar <= 0) stepsPerBar = 1;

    const double spq = sampleRate * 60.0 / bpm;

    // A restarted clock restarts step numbering from zero.
    if (ppqStart < lastPpq_ - 1e-9) { lastStep_ = -1; pendingCount_ = 0; }
    lastPpq_ = ppqEnd;

    const long long firstStep = (long long) std::floor (ppqStart / stepsPerQuarter);
    const long long endStep   = (long long) std::floor (ppqEnd   / stepsPerQuarter);

    int written = 0;

    // Releases due on a boundary crossed inside this block, oldest first.
    for (int i = 0; i < pendingCount_; ++i)
    {
        const PendingRelease r = pending_[(std::size_t) i];
        if (r.step > endStep) continue;

        if (written < cap)
        {
            StrumHit& h = out[written];
            h.type         = EventType::Release;
            h.stringIndex  = r.stringIndex;
            h.delaySamples = ((double) r.step * stepsPerQuarter - ppqStart) * spq;
            if (h.delaySamples < 0.0) h.delaySamples = 0.0;
            h.velocity     = 0.f;
            h.pitchFrom    = -1.f;
            h.releaseSec   = releaseSec;
            h.fret         = 0;
            h.harmonicN    = 0;
            h.art          = art;
            if (open != nullptr) h.pitch = open[r.stringIndex];
            ++written;
        }
        for (int k = i; k + 1 < pendingCount_; ++k)
            pending_[(std::size_t) k] = pending_[(std::size_t) k + 1];
        --pendingCount_;
        --i;
    }

    long long emittedTo = lastStep_;

    for (long long step = firstStep; step <= endStep && written < cap; ++step)
    {
        if (step <= lastStep_) continue;   // already served in an earlier block
        emittedTo = step;

        const int slot = (int) (((long long) stepsPerBar > 0) ? step % (long long) stepsPerBar : step);

        for (int n = 0; n < pattern->count; ++n)
        {
            const SeqNote& cell = pattern->notes[(std::size_t) n];
            if ((int) cell.start != slot) continue;

            const double delay = ((double) step * stepsPerQuarter - ppqStart) * spq;
            const int    vel127 = (int) cell.vel;

            const int strokeRow = rowToStroke ((int) cell.row);
            if (strokeRow >= 0)
            {
                const bool up  = (strokeRow & 1) != 0;
                StrokeKind kind = StrokeKind::Normal;
                if (strokeRow == 2 || strokeRow == 3) kind = StrokeKind::Muted;
                else if (strokeRow == 4 || strokeRow == 5) kind = StrokeKind::Palm;

                StrumEngine::StrokeParams sp = strokeParams;
                sp.timeOffsetMs = (float) delay * 1000.f / (float) sampleRate;

                const int before = written;
                const int got = strum.stroke (up, kind, vel127, sp, voicing, open,
                                               art, rng, out + written, cap - written);
                const int after = before + got;
                for (int k = before; k < after; ++k)
                {
                    out[k].releaseSec = releaseSec;
                    pushRelease (step + (long long) cell.len, out[k].stringIndex);
                }
                written = after;
            }
            else
            {
                const int s = rowToString ((int) cell.row);
                if (s < 0) continue;

                // The chord has a note on this string when the voicing does.
                int vi = -1;
                for (int i = 0; i < voicing.count; ++i)
                    if (voicing.stringIndex[i] == s) { vi = i; break; }
                if (vi < 0) continue;

                const float partial = (art.harm == Harmonic::Natural && open != nullptr)
                                          ? (float) harmonicPartialForFret (voicing.fret[vi]) : 0.f;

                StrumHit& h = out[written];
                h.type         = EventType::Pluck;
                h.stringIndex  = s;
                h.delaySamples = delay < 0.0 ? 0.0 : delay;
                h.velocity     = ((float) vel127 / 127.f) * strokeParams.strokeVol;
                h.pitch        = (partial > 0.f && open != nullptr) ? open[s] : voicing.pitch[vi];
                h.pitchFrom    = -1.f;
                h.fret         = voicing.fret[vi];
                h.harmonicN    = (int) partial;
                h.releaseSec   = releaseSec;
                h.art          = art;
                ++written;

                pushRelease (step + (long long) cell.len, s);
            }
        }
    }

    // Only boundaries actually served count, so a capped block still
    // produces its remaining steps in the next one.
    if (emittedTo > lastStep_) lastStep_ = emittedTo;

    long long cs = endStep % (long long) stepsPerBar;
    if (cs < 0) cs += stepsPerBar;
    currentStep_ = (int) cs;
    return written;
}

} // namespace fw

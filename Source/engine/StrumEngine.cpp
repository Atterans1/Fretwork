
#include "engine/StrumEngine.h"

#include <algorithm>

#include <cmath>
#include <cstddef>

namespace fw {

int harmonicPartialForFret (int fret) noexcept
{
    switch (fret)
    {
        case 12: return 2;
        case  7: return 3;
        case 19: return 3;
        case  5: return 4;
        case 24: return 4;
        case  4: return 5;
        case  9: return 5;
        case 16: return 5;
        default: return 0;
    }
}

namespace {

    // strumTime * (1 - (1-x)^(1 + 2*sep01)) milliseconds, plus timeOffset.
    double stepTimeMs (float strumTimeMs, float separation01, float x) noexcept
    {
        const double sep  = 2.0 * (double) separation01;
        const double base = (double) strumTimeMs;
        const double xd   = (double) x;
        return base * (1.0 - std::pow (1.0 - xd, 1.0 + sep));
    }

    void fillHit (StrumHit& h,
                  int   stringIndex,
                  int   fret,
                  float voicedPitch,
                  const float* open,
                  Articulation art,
                  float velocity) noexcept
    {
        // Every field is written: callers reuse scratch slots that may still
        // hold a Release from the previous block (sequenced strokes after the
        // first one used to come out as releases and never sounded).
        h.type          = EventType::Pluck;
        h.pitchFrom     = -1.f;
        h.stringIndex   = stringIndex;
        h.fret          = fret;
        h.velocity      = velocity;
        h.art           = art;
        h.harmonicN     = 0;
        h.pitch         = voicedPitch;

        // A latched natural harmonic sounds the open string at the partial.
        if (art.harm == Harmonic::Natural && open != nullptr)
        {
            const int n = harmonicPartialForFret (fret);
            if (n > 0)
            {
                h.harmonicN = n;
                h.pitch     = open[stringIndex];
            }
        }
    }

} // namespace

int StrumEngine::struckCount (int n, float v01) noexcept
{
    if (n <= 2) return n;
    const float v = v01 < 0.f ? 0.f : (v01 > 1.f ? 1.f : v01);
    const float frac = std::fmin (1.f, 0.5f + 0.625f * v);
    const int count = (int) std::ceil ((float) n * frac - 1.0e-4f);
    return count < 2 ? 2 : (count > n ? n : count);
}

int StrumEngine::stroke (bool up,
                         StrokeKind kind,
                         int vel127,
                         const StrokeParams& p,
                         const Voicing& v,
                         const float* open,
                         Articulation liveArt,
                         Rng& rng,
                         StrumHit* out,
                         int cap) const
{
    if (out == nullptr || cap <= 0) return 0;
    const int n = v.count;
    if (n <= 0) return 0;

    Articulation art = liveArt;
    if (kind == StrokeKind::Muted) art.damp = Damping::Dead;
    else if (kind == StrokeKind::Palm) art.damp = Damping::PalmMute;

    const float h  = p.humanize01 < 0.f ? 0.f : (p.humanize01 > 1.f ? 1.f : p.humanize01);
    const float base = (float) (((float) vel127 + p.velOffset) / 127.0f);
    const float bnorm = base < 0.f ? 0.f : (base > 1.f ? 1.f : base);

    // Candidate strings in sweep order. Bass-and-strum leaves out the lowest
    // voiced string, which the player is holding for the bass note.
    int order[kNumStrings];
    int m = 0;
    for (int k = 0; k < n; ++k)
    {
        const int vi = up ? (n - 1 - k) : k;
        if (p.skipLowest && vi == 0 && n > 1) continue;
        order[m++] = vi;
    }

    int struck = struckCount (m, bnorm);
    // A hand pushing back up catches only the top three or four strings: an
    // upstroke never sweeps all six the way a downstroke does.
    if (up && m >= 4)
    {
        const int maxUp = std::max (3, (int) std::floor ((float) m * 0.67f));
        struck = std::min (struck, maxUp);
    }
    const int limit = struck < cap ? struck : cap;
    int written = 0;

    // A harder stroke is a faster wrist, so the pick crosses the strings in
    // less time. Centred on a typical playing velocity of 100, where the sweep
    // takes exactly the set Strum Time: full force is about 0.89x, a
    // feather-light stroke up to about 1.39x.
    constexpr float kTypicalVel01 = 100.0f / 127.0f;
    const float sweepMs = p.strumTimeMs * (1.0f + 0.5f * (kTypicalVel01 - bnorm));

    // Humanized timing, the way a hand is late or early: the whole stroke
    // shifts by one shared offset, and each string adds a small jitter capped
    // at a fraction of its gap to the next string. Independent per-string
    // jitter could land a later string before an earlier one, reversing the
    // sweep, which no strum does.
    const double strokeShiftMs = (double) rng.range (-16.f * h, 16.f * h);
    constexpr double kJitterOfGap = 0.35;
    double prevMs = -1.0e9;

    for (int k = 0; k < limit; ++k)
    {
        // Order index k runs in sweep order: ascending strings for a down
        // stroke, descending for an up stroke.
        const int vi   = order[k];
        const int s    = v.stringIndex[vi];
        const int fret = v.fret[vi];

        // Positions stay those of the full chord, so a partial stroke keeps the
        // same string spacing and simply ends sooner.
        const float x = (m == 1) ? 0.f : (float) k / (float) (m - 1);
        const float xNext = (m == 1) ? 0.f : (float) (k + 1) / (float) (m - 1);
        const double sweepAt = (double) stepTimeMs (sweepMs, p.separation01, x);
        const double gapMs   = k + 1 < m ? (double) stepTimeMs (sweepMs, p.separation01, xNext) - sweepAt : 0.0;
        const double jitter  = std::fmin (8.0 * (double) h, kJitterOfGap * gapMs);

        double tMs = sweepAt + (double) p.timeOffsetMs + strokeShiftMs
                   + (double) rng.range ((float) -jitter, (float) jitter);
        if (tMs < prevMs) tMs = prevMs;     // never reverse the sweep
        prevMs = tMs;

        const float gain = up ? upGain (s) : downGain (s);
        const float vel  = bnorm * p.strokeVol * gain
                         * (1.f + rng.range (-0.2f * h, 0.2f * h));

        StrumHit& h = out[written];
        h.delaySamples = tMs * sampleRate_ * 0.001;
        if (h.delaySamples < 0.0) h.delaySamples = 0.0;
        fillHit (h, s, fret, v.pitch[vi], open, art, vel);
        ++written;
    }

    return written;
}

int StrumEngine::pluckExtreme (bool highest,
                               const Voicing& v,
                               const float* open,
                               Articulation art,
                               float velocity,
                               StrumHit* out,
                               int cap)
{
    if (out == nullptr || cap <= 0 || v.count <= 0) return 0;

    int best = -1;
    for (int i = 0; i < v.count; ++i)
    {
        if (best < 0) { best = i; continue; }
        if (highest ? (v.stringIndex[i] > v.stringIndex[best])
                    : (v.stringIndex[i] < v.stringIndex[best]))
            best = i;
    }
    if (best < 0) return 0;

    StrumHit& h = out[0];
    h.delaySamples = 0.0;
    fillHit (h, v.stringIndex[best], v.fret[best], v.pitch[best], open, art, velocity);
    return 1;
}

} // namespace fw


#include "engine/StrumEngine.h"

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

    const int limit = n < cap ? n : cap;
    int written = 0;

    for (int k = 0; k < limit; ++k)
    {
        // Order index k runs in sweep order: ascending strings for a down
        // stroke, descending for an up stroke.
        const int vi   = up ? (n - 1 - k) : k;
        const int s    = v.stringIndex[vi];
        const int fret = v.fret[vi];

        const float x = (n == 1) ? 0.f : (float) k / (float) (n - 1);

        const double tMs = (double) stepTimeMs (p.strumTimeMs, p.separation01, x)
                         + (double) p.timeOffsetMs
                         + (double) rng.range (-8.f * h, 8.f * h);

        const float gain = up ? upGain (s) : downGain (s);
        const float vel  = bnorm * p.strokeVol * gain
                         * (1.f + rng.range (-0.12f * h, 0.12f * h));

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

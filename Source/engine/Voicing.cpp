
#include "engine/Voicing.h"

#include <cmath>
#include <cstddef>

namespace fw {

namespace {

    // Frets are physical: a pitch that does not land on an integer fret above
    // an open string is not playable on that string.
    constexpr float kFretEpsilon = 1e-3f;
    constexpr float kCostEpsilon = 1e-9f;

    struct Search
    {
        const float* pitches    = nullptr;
        const float* open       = nullptr;
        int          n          = 0;
        int          maxFret    = 0;
        int          cur[kNumStrings] {};
        int          best[kNumStrings] {};
        float        bestCost   = 0.f;
        bool         haveBest   = false;
        Voicing*     out        = nullptr;
    };

    // 10 * max(0, span(nonzero frets) - 4)
    // + 0.1 * sum(nonzero frets)
    // + 0.05 * fret of the highest note
    float costOf (const int* frets, int n) noexcept
    {
        int   sum    = 0;
        int   lo     = 0;
        int   hi     = 0;
        int   nonzero = 0;

        for (int i = 0; i < n; ++i)
        {
            if (frets[i] == 0) continue;
            if (nonzero == 0) { lo = hi = frets[i]; }
            else
            {
                if (frets[i] < lo) lo = frets[i];
                if (frets[i] > hi) hi = frets[i];
            }
            sum += frets[i];
            ++nonzero;
        }

        const int   span = (nonzero >= 2) ? (hi - lo) : 0;
        const float a = 10.f * (float) ((span > 4) ? (span - 4) : 0);
        const float b = 0.1f * (float) sum;
        const float c = 0.05f * (float) frets[n - 1];
        return a + b + c;
    }

    void commit (Search& s) noexcept
    {
        Voicing& v = *s.out;
        v.count = s.n;
        for (int i = 0; i < s.n; ++i)
        {
            const int str = s.best[i];
            const float pf = s.pitches[i];
            const float f  = pf - s.open[str];
            v.stringIndex[i] = str;
            v.fret[i]        = (int) std::lround (f);
            v.pitch[i]       = pf;
            v.fretFloat[i]   = f;
        }
    }

    // Strictly increasing string indices, enumerated in lexicographic order, so
    // the first tuple holding the minimum cost also has the lowest indices.
    void recurse (Search& s, int note, int firstString) noexcept
    {
        if (note == s.n)
        {
            const float c = costOf (s.cur, s.n);
            if (s.haveBest && ! (c < s.bestCost - kCostEpsilon)) return;
            s.bestCost = c;
            s.haveBest = true;
            for (int i = 0; i < s.n; ++i) s.best[i] = s.cur[i];
            return;
        }

        const float pf = s.pitches[note];
        const int   last = (note == s.n - 1) ? kNumStrings - 1 : (kNumStrings - s.n + note);

        for (int str = firstString; str <= last; ++str)
        {
            float fretOut = 0.f;
            if (! voicing::fits (pf, s.open[str], s.maxFret, fretOut)) continue;
            s.cur[note] = str;
            recurse (s, note + 1, str + 1);
        }
    }

    void greedy (Search& s) noexcept
    {
        bool used[kNumStrings] = {};
        Voicing& v = *s.out;
        v.count = 0;

        for (int i = 0; i < s.n; ++i)
        {
            const float pf  = s.pitches[i];
            int         bestStr = -1;
            int         bestFret = 0;

            for (int str = 0; str < kNumStrings; ++str)
            {
                if (used[str]) continue;
                float fretOut = 0.f;
                if (! voicing::fits (pf, s.open[str], s.maxFret, fretOut)) continue;
                const int f = (int) std::lround (fretOut);
                if (bestStr < 0 || f < bestFret)   // lowest fret; lower index wins ties
                {
                    bestStr  = str;
                    bestFret = f;
                }
            }

            if (bestStr < 0) continue;            // no valid string: drop the note
            used[bestStr] = true;

            v.stringIndex[v.count] = bestStr;
            v.fret[v.count]        = bestFret;
            v.pitch[v.count]       = pf;
            v.fretFloat[v.count]   = pf - s.open[bestStr];
            ++v.count;
        }
    }

} // namespace

namespace voicing {

bool fits (float pitch, float openPitch, int maxFret, float& fretOut) noexcept
{
    const float f = pitch - openPitch;
    if (! (f >= 0.f) || ! (f <= (float) maxFret)) return false;
    const float r = std::round (f);
    if (std::fabs (f - r) > kFretEpsilon) return false;
    fretOut = f;
    return true;
}

} // namespace voicing

void assignVoicing (const float* pitches,
                    int          n,
                    const float* open,
                    int          maxFret,
                    Voicing&     out) noexcept
{
    out.clear();
    if (pitches == nullptr || open == nullptr || n <= 0) return;
    if (n > kNumStrings) n = kNumStrings;   // greedy fallback drops the surplus

    Search s;
    s.pitches = pitches;
    s.open    = open;
    s.n       = n;
    s.maxFret = maxFret < 0 ? 0 : maxFret;
    s.out     = &out;

    recurse (s, 0, 0);

    if (s.haveBest) { commit (s); return; }
    greedy (s);
}

} // namespace fw

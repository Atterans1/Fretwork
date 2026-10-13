#include "ChordDetect.h"

#include <cmath>
#include <cstdio>

namespace fw::chord {
namespace {

struct Quality
{
    const char* suffix;
    unsigned    full;       // pitch classes relative to the root, bit i = i semitones
    unsigned    required;   // must all be held / voiced
};

constexpr unsigned bits (std::initializer_list<int> iv)
{
    unsigned m = 0;
    for (int i : iv) m |= 1u << i;
    return m;
}

// Order matters only as a final tie-break (earlier = preferred).
const Quality kQualities[] =
{
    { "",      bits ({ 0, 4, 7 }),         bits ({ 0, 4 }) },
    { "m",     bits ({ 0, 3, 7 }),         bits ({ 0, 3 }) },
    { "5",     bits ({ 0, 7 }),            bits ({ 0, 7 }) },
    { "7",     bits ({ 0, 4, 7, 10 }),     bits ({ 0, 4, 10 }) },
    { "m7",    bits ({ 0, 3, 7, 10 }),     bits ({ 0, 3, 10 }) },
    { "maj7",  bits ({ 0, 4, 7, 11 }),     bits ({ 0, 4, 11 }) },
    { "sus4",  bits ({ 0, 5, 7 }),         bits ({ 0, 5, 7 }) },
    { "sus2",  bits ({ 0, 2, 7 }),         bits ({ 0, 2, 7 }) },
    { "6",     bits ({ 0, 4, 7, 9 }),      bits ({ 0, 4, 9 }) },
    { "m6",    bits ({ 0, 3, 7, 9 }),      bits ({ 0, 3, 9 }) },
    { "dim",   bits ({ 0, 3, 6 }),         bits ({ 0, 3, 6 }) },
    { "aug",   bits ({ 0, 4, 8 }),         bits ({ 0, 4, 8 }) },
    { "m7b5",  bits ({ 0, 3, 6, 10 }),     bits ({ 0, 3, 6, 10 }) },
    { "dim7",  bits ({ 0, 3, 6, 9 }),      bits ({ 0, 3, 6, 9 }) },
    { "7sus4", bits ({ 0, 5, 7, 10 }),     bits ({ 0, 5, 10 }) },
    { "add9",  bits ({ 0, 2, 4, 7 }),      bits ({ 0, 2, 4 }) },
    { "9",     bits ({ 0, 2, 4, 7, 10 }),  bits ({ 0, 2, 4, 10 }) },
    { "m9",    bits ({ 0, 2, 3, 7, 10 }),  bits ({ 0, 2, 3, 10 }) },
    { "maj9",  bits ({ 0, 2, 4, 7, 11 }),  bits ({ 0, 2, 4, 11 }) },
};
constexpr int kNumQualities = (int) (sizeof (kQualities) / sizeof (kQualities[0]));

int popcount (unsigned v) noexcept { int c = 0; while (v) { v &= v - 1; ++c; } return c; }

unsigned rotate (unsigned pcs, int root) noexcept
{
    // Relative to root: bit i set when (root + i) mod 12 is held.
    unsigned r = 0;
    for (int i = 0; i < 12; ++i)
        if (pcs & (1u << ((root + i) % 12))) r |= 1u << i;
    return r;
}

int pcOf (int pitch) noexcept { return ((pitch % 12) + 12) % 12; }

const char* const kNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

// Shape cost weights, fixed by the standard open shapes (C x32010, G 320003,
// D xx0232, Am x02210, E 022100, Dm xx0231, F 133211).
constexpr float kMutedString  = 2.0f;
constexpr float kPosition     = 0.5f;   // per fret of the hand position
constexpr float kStretch      = 1.0f;   // per fret of span between fretted notes
constexpr float kFinger       = 0.3f;   // per fretted note
constexpr float kMissingOpt   = 1.0f;   // per optional chord tone left out
constexpr float kMove         = 0.4f;   // per fret a string moves from the previous shape
constexpr float kSoundChange  = 1.0f;   // per string that starts or stops sounding
constexpr int   kHandSpan     = 4;      // frets under the hand: w .. w+3
constexpr int   kOpenPositionMax = 5;   // highest fretted note that still lets open strings ring

struct Search
{
    const float* open;
    unsigned allowed;        // absolute pitch classes
    unsigned required;       // absolute pitch classes
    unsigned optional;
    int bassPc;
    int window;              // lowest fretted fret allowed
    const Voicing* prev;
    int prevFret[kNumStrings];

    int  fret[kNumStrings];  // -1 muted
    int  bestFret[kNumStrings];
    float bestCost;
    bool found;

    void evaluate() noexcept
    {
        int lowest = -1, highest = -1;
        for (int s = 0; s < kNumStrings; ++s)
            if (fret[s] >= 0) { if (lowest < 0) lowest = s; highest = s; }
        if (lowest < 0) return;

        // Sounding strings must be adjacent.
        for (int s = lowest; s <= highest; ++s) if (fret[s] < 0) return;
        if (highest - lowest + 1 < 3) return;

        unsigned have = 0;
        int minF = 99, maxF = -1, fretted = 0;
        for (int s = lowest; s <= highest; ++s)
        {
            const int pitch = (int) std::lround (open[s]) + fret[s];
            have |= 1u << pcOf (pitch);
            if (fret[s] > 0) { ++fretted; if (fret[s] < minF) minF = fret[s]; if (fret[s] > maxF) maxF = fret[s]; }
        }
        if (pcOf ((int) std::lround (open[lowest]) + fret[lowest]) != bassPc) return;
        if ((have & required) != required) return;

        int opens = 0;
        for (int s = lowest; s <= highest; ++s) if (fret[s] == 0) ++opens;

        // Open strings only ring in open position: none once a finger is above
        // fret kOpenPositionMax.
        if (opens > 0 && maxF > kOpenPositionMax) return;

        if (fretted > 4)
        {
            // More than four fingers needs a barre across the lowest fret, and a
            // barre lies over every string, so nothing can ring open under it.
            int atMin = 0;
            for (int s = lowest; s <= highest; ++s) if (fret[s] == minF) ++atMin;
            if (atMin < 2 || fretted - atMin > 3 || opens > 0) return;
        }

        float cost = kMutedString * (float) (kNumStrings - (highest - lowest + 1))
                   + kPosition * (float) (fretted > 0 ? minF : 0)
                   + kStretch * (float) (fretted > 0 ? maxF - minF : 0)
                   + kFinger * (float) fretted
                   + kMissingOpt * (float) popcount (optional & ~have);

        if (prev != nullptr && prev->count > 0)
            for (int s = 0; s < kNumStrings; ++s)
            {
                const bool now = fret[s] >= 0, before = prevFret[s] >= 0;
                if (now != before) cost += kSoundChange;
                else if (now) cost += kMove * (float) std::abs (fret[s] - prevFret[s]);
            }

        if (! found || cost < bestCost)
        {
            found = true;
            bestCost = cost;
            for (int s = 0; s < kNumStrings; ++s) bestFret[s] = fret[s];
        }
    }

    void recurse (int s, int maxFret) noexcept
    {
        if (s == kNumStrings) { evaluate(); return; }

        const int base = (int) std::lround (open[s]);
        fret[s] = -1;
        recurse (s + 1, maxFret);

        if (allowed & (1u << pcOf (base)))
        {
            fret[s] = 0;
            recurse (s + 1, maxFret);
        }
        for (int f = window; f < window + kHandSpan && f <= maxFret; ++f)
        {
            if (f <= 0 || ! (allowed & (1u << pcOf (base + f)))) continue;
            fret[s] = f;
            recurse (s + 1, maxFret);
        }
        fret[s] = -1;
    }
};

} // namespace

int qualityCount() noexcept { return kNumQualities; }

ChordName detect (const int* pitches, int n) noexcept
{
    ChordName best;
    if (pitches == nullptr || n < 2) return best;

    unsigned pcs = 0;
    int lowest = pitches[0];
    for (int i = 0; i < n; ++i) { pcs |= 1u << pcOf (pitches[i]); if (pitches[i] < lowest) lowest = pitches[i]; }
    if (popcount (pcs) < 2) return best;
    const int bassPc = pcOf (lowest);

    int bestScore = 1 << 30;
    for (int q = 0; q < kNumQualities; ++q)
        for (int r = 0; r < 12; ++r)
        {
            const unsigned rel = rotate (pcs, r);
            if ((rel & ~kQualities[q].full) != 0) continue;
            if ((kQualities[q].required & ~rel) != 0) continue;
            const int score = (r == bassPc ? 0 : 1000) + popcount (kQualities[q].full) * 10 + q;
            if (score < bestScore) { bestScore = score; best.root = r; best.quality = q; best.bass = bassPc; }
        }
    return best;
}

void label (const ChordName& c, char* out, int outSize) noexcept
{
    if (out == nullptr || outSize <= 0) return;
    if (! c.valid() || c.quality >= kNumQualities) { out[0] = 0; return; }
    if (c.bass >= 0 && c.bass != c.root)
        std::snprintf (out, (size_t) outSize, "%s%s/%s", kNames[c.root], kQualities[c.quality].suffix, kNames[c.bass]);
    else
        std::snprintf (out, (size_t) outSize, "%s%s", kNames[c.root], kQualities[c.quality].suffix);
}

bool guitarVoicing (const ChordName& c, const float* open, int maxFret,
                    const Voicing* previous, Voicing& out) noexcept
{
    out.clear();
    if (! c.valid() || c.quality >= kNumQualities || open == nullptr) return false;

    const auto absolute = [&c] (unsigned rel)
    {
        unsigned a = 0;
        for (int i = 0; i < 12; ++i) if (rel & (1u << i)) a |= 1u << ((c.root + i) % 12);
        return a;
    };

    Search s {};
    s.open = open;
    s.allowed  = absolute (kQualities[c.quality].full);
    s.required = absolute (kQualities[c.quality].required) | (1u << c.bass);
    s.optional = s.allowed & ~s.required;
    s.bassPc = c.bass;
    s.prev = previous;
    for (int i = 0; i < kNumStrings; ++i) s.prevFret[i] = -1;
    if (previous != nullptr)
        for (int vi = 0; vi < previous->count; ++vi)
        {
            const int str = previous->stringIndex[vi];
            if (str >= 0 && str < kNumStrings) s.prevFret[str] = previous->fret[vi];
        }

    for (int w = 1; w + kHandSpan - 1 <= maxFret || w == 1; ++w)
    {
        s.window = w;
        for (int i = 0; i < kNumStrings; ++i) s.fret[i] = -1;
        s.recurse (0, maxFret);
    }
    if (! s.found) return false;

    for (int str = 0; str < kNumStrings; ++str)
    {
        if (s.bestFret[str] < 0) continue;
        const int vi = out.count++;
        out.stringIndex[vi] = str;
        out.fret[vi] = s.bestFret[str];
        out.fretFloat[vi] = (float) s.bestFret[str];
        out.pitch[vi] = open[str] + (float) s.bestFret[str];
    }
    return true;
}

} // namespace fw::chord

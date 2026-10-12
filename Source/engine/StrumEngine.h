
#pragma once
#include "Contracts.h"
#include "engine/Voicing.h"
#include <cstdint>

namespace fw {

// Deterministic xorshift32. An engine instance owns one; no globals, no
// static mutable state, so a given input sequence always draws the same stream.
struct Rng
{
    std::uint32_t state = 0x9e3779b9u;

    void seed (std::uint32_t s) noexcept { state = s != 0u ? s : 0x9e3779b9u; }

    std::uint32_t next() noexcept
    {
        std::uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    // Uniform in [0, 1).
    float unit() noexcept { return (float) (next() >> 8) / 16777216.0f; }

    // Uniform in [lo, hi).
    float range (float lo, float hi) noexcept { return lo + (hi - lo) * unit(); }
};

// Natural harmonic fret to partial. Returns 0 for a fret that plays plain.
int harmonicPartialForFret (int fret) noexcept;

// One scheduled string strike produced by a strum stroke or an extreme pluck.
// delaySamples is relative to the stroke trigger; the caller stamps it
// absolutely into its queue.
struct StrumHit
{
    EventType type    = EventType::Pluck;
    int   stringIndex  = 0;
    double delaySamples = 0.0;
    float velocity     = 0.f;
    float pitch        = 40.f;
    float pitchFrom    = -1.f;   // slide source; <0 = none
    int   fret         = 0;
    int   harmonicN    = 0;
    float releaseSec   = 8.f;   // used by Release events
    Articulation art {};
};

enum class StrokeKind { Normal, Muted, Palm };

// Strum timing and velocity shaping. Pure: all randomness arrives through the
// caller supplied Rng reference, so the engine stays reproducible.
class StrumEngine
{
public:
    struct StrokeParams
    {
        float strumTimeMs  = 0.f;
        float timeOffsetMs = 0.f;
        float separation01 = 0.f;
        float velOffset    = 0.f;
        float strokeVol    = 1.f;
        float humanize01   = 0.f;
        bool  skipLowest   = false;   // bass-and-strum: leave the lowest voiced string out
    };

    void prepare (double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0; }
    double sampleRate() const noexcept { return sampleRate_; }

    // Directional gain of string s. Down sweeps attenuate toward the treble
    // side; up sweeps do the opposite and carry an extra 0.85 for the pick.
    static float downGain (int s) noexcept { return 1.f - 0.3f * (float) s / 5.f; }
    static float upGain   (int s) noexcept { return (0.75f + 0.25f * (float) s / 5.f) * 0.85f; }

    // How many of n candidate strings a stroke of normalised velocity v01
    // reaches: all of them from about velocity 102 up, falling to half (never
    // fewer than two) for the lightest stroke.
    static int struckCount (int n, float v01) noexcept;

    // Schedules the struck strings. Down sweeps low to high (ascending string
    // index); up sweeps high to low. A soft stroke runs out before the far side
    // of the chord, so it strikes only the strings nearest where it starts.
    // Returns the number of hits written, which is capped at cap.
    int stroke (bool up,
                StrokeKind kind,
                int vel127,
                const StrokeParams& p,
                const Voicing& v,
                const float* open,
                Articulation liveArt,
                Rng& rng,
                StrumHit* out,
                int cap) const;

    // Keyswitch 34 / 35: pluck the lowest or highest voiced string immediately.
    static int pluckExtreme (bool highest,
                             const Voicing& v,
                             const float* open,
                             Articulation art,
                             float velocity,
                             StrumHit* out,
                             int cap);

private:
    double sampleRate_ = 44100.0;
};

} // namespace fw

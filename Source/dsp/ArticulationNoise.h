#pragma once
#include "Contracts.h"
#include "dsp/Biquad.h"

#include <array>
#include <cstdint>

namespace fw::dsp {

/** Procedural noises that ride on top of the strings. No samples: every sound
    is shaped white noise.

    Stroke noise (a pick dragged across the strings in a strum):
      - scrape: white noise through a band-pass whose centre glides from the
        first struck string's register to the last's (1.6 -> 3.4 kHz on a
        down-stroke, the reverse on an up-stroke), Q 1.2, under a 2 ms attack /
        hold-for-the-stroke / 18 ms decay envelope, panned across the strings in
        stroke order;
      - ticks: one 3 ms high-passed (2.5 kHz) noise click per string at the
        moment the pick crosses it, in stroke order, panned to that string.
      Level follows velocity.

    String squeak (a fingertip sliding along a wound string):
      band-pass noise, Q 7, centre swept 1.2 -> 3.8 kHz (shifting up the neck)
      or 3.8 -> 1.2 kHz (down) with a raised-cosine envelope over 60-150 ms.
      Wound strings (low E, A, D) squeak at full level, plain strings at 0.25.

    Allocation-free; a fixed pool of voices, the oldest is stolen. */
class ArticulationNoise
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** dir +1 down-stroke (low to high), -1 up-stroke; durSec is how long the
        pick takes across the strings. */
    void stroke (int dir, float durSec, float velocity, const float* panL, const float* panR) noexcept;

    /** string 0 = low E. dir +1 sliding up the neck (pitch rising), -1 down. */
    void squeak (int string, int dir, float durSec, float velocity, float panL, float panR) noexcept;

    /** Adds the active noises into left/right. */
    void render (float* left, float* right, int n) noexcept;

    bool active() const noexcept;

private:
    struct Voice
    {
        bool   on = false;
        int    pos = 0, len = 0, attack = 1, decay = 1;
        float  amp = 0.f;
        float  f0 = 1000.f, f1 = 1000.f, q = 1.f;
        float  pl0 = 0.7f, pr0 = 0.7f, pl1 = 0.7f, pr1 = 0.7f;
        bool   raisedCosine = false;
        Biquad bp;
        std::uint32_t age = 0;
    };

    struct Tick
    {
        bool  on = false;
        int   delay = 0, pos = 0, len = 1;
        float amp = 0.f, pl = 0.7f, pr = 0.7f;
        Biquad hp;
    };

    Voice& freeVoice() noexcept;
    Tick&  freeTick() noexcept;
    float  white() noexcept;

    double fs_ = 48000.0;
    std::array<Voice, 8> voices_ {};
    std::array<Tick, 24> ticks_ {};
    std::uint32_t seed_ = 0x9e3779b9u, clock_ = 0;
};

} // namespace fw::dsp

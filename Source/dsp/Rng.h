#pragma once
#include <cstdint>

namespace fw::dsp {

// Tiny header-only xorshift32 generator. No globals, no static state: every
// instance owns its own seed, so several can coexist on the audio thread.
struct Rng
{
    std::uint32_t state;

    explicit Rng (std::uint32_t seed) : state (seed != 0u ? seed : 0x9e3779b9u) {}

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
    float uniform() noexcept
    {
        return static_cast<float> (next() >> 8) * (1.0f / 16777216.0f);
    }

    float uniform (float lo, float hi) noexcept
    {
        return lo + (hi - lo) * uniform();
    }

    // Symmetric bipolar noise in [-1, 1).
    float bipolar() noexcept
    {
        return uniform() * 2.0f - 1.0f;
    }
};

} // namespace fw::dsp

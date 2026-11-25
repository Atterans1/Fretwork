// TEMPORARY (not committed): dumps reference PCM for the Rust parity harness.
// Renders the same note sequence the Rust port uses and writes interleaved
// L/R f32 little-endian, one file per configuration.
#include "Harness.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using fwtest::Plugin;

void play (Plugin& p)
{
    // The sounding pitches the Rust harness renders: MIDI 52 then a chord.
    // noteOn adds fw::kWrittenOctave, so write an octave low.
    p.noteOn (0.01, 40, 0.8f).noteOff (0.8, 40);
    p.noteOn (0.5, 52, 0.9f).noteOff (1.2, 52);
    p.noteOn (1.30, 47, 0.85f);
    p.noteOn (1.312, 52, 0.85f);
    p.noteOn (1.324, 56, 0.85f);
    p.noteOn (1.336, 59, 0.85f);
}

void dump (const char* name, double fs, int block, void (*setup) (Plugin&), double secs = 2.6)
{
    Plugin p (fs, block);
    setup (p);
    const auto take = p.render (secs);

    char path[512];
    std::snprintf (path, sizeof path, "C:/tmp/ref_%s.pcm", name);
    std::FILE* f = std::fopen (path, "wb");
    if (f == nullptr) { std::printf ("FAILED open %s\n", path); return; }

    // A short header line so the Rust side can confirm it read the right file:
    // sample rate, block, frame count, then the interleaved float pairs.
    std::fprintf (f, "FREF %s %d %.6f %zu\n", name, block, take.fs, take.left.size());
    for (size_t i = 0; i < take.left.size(); ++i)
    {
        const float l = take.left[i];
        const float r = take.right[i];
        std::fwrite (&l, sizeof l, 1, f);
        std::fwrite (&r, sizeof r, 1, f);
    }
    std::fclose (f);

    const bool fin = take.finite();
    std::printf ("DUMP %-22s fs=%7.1f block=%4d frames=%zu finite=%d rms=%.9f peak=%.9f\n",
                 name, take.fs, block, take.left.size(), fin ? 1 : 0, take.rms (0.0, 1e9), take.peak (0.0, 1e9));
}
} // namespace

FW_TEST ("zzdump: reference PCM for the Rust parity harness")
{
    dump ("default-48k-480",  48000.0, 480,  [] (Plugin& p) { play (p); });
    dump ("default-48k-64",   48000.0, 64,   [] (Plugin& p) { play (p); });
    dump ("default-44k1-441", 44100.0, 441,  [] (Plugin& p) { play (p); });
    dump ("default-96k-480",  96000.0, 480,  [] (Plugin& p) { play (p); });
    dump ("body1-48k-480",    48000.0, 480,  [] (Plugin& p) { p.set ("body", 1.f); play (p); });
    dump ("body2-48k-480",    48000.0, 480,  [] (Plugin& p) { p.set ("body", 2.f); play (p); });
}

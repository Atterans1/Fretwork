// TEMPORARY (not committed): dumps bit-exact hashes of guitar renders so the Bass work can prove
// the guitar path is unchanged. Only uses parameters that exist before the Bass change.
#include "Harness.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace {
using fwtest::Plugin;

uint64_t hashTake (const fwtest::Take& t)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h] (float f)
    {
        uint32_t u;
        std::memcpy (&u, &f, 4);
        for (int i = 0; i < 4; ++i) { h ^= (u >> (8 * i)) & 0xffu; h *= 1099511628211ull; }
    };
    for (size_t i = 0; i < t.left.size(); ++i) { mix (t.left[i]); mix (t.right[i]); }
    return h;
}

void play (Plugin& p)
{
    p.noteOn (0.01, 40, 0.8f).noteOff (0.8, 40);
    p.noteOn (0.5, 52, 0.9f).noteOff (1.2, 52);
    p.chord (1.3, { 47, 52, 56, 59 }, 0.85f, 0.012);
}

void run (const char* name, double fs, int block, void (*setup) (Plugin&), double secs = 2.6)
{
    Plugin p (fs, block);
    setup (p);
    const auto take = p.render (secs);
    std::printf ("HASH %-30s %016llx rms=%.9f peak=%.9f\n", name, (unsigned long long) hashTake (take), take.rms (0.0, 1e9), take.peak (0.0, 1e9));
}
}

FW_TEST ("zzbaseline: guitar render hashes")
{
    run ("default", 48000.0, 480, [] (Plugin& p) { play (p); });
    run ("default-96k", 96000.0, 480, [] (Plugin& p) { play (p); });
    run ("default-44k1", 44100.0, 441, [] (Plugin& p) { play (p); });
    run ("default-block64", 48000.0, 64, [] (Plugin& p) { play (p); });
    run ("default-block1024", 48000.0, 1024, [] (Plugin& p) { play (p); });
    run ("twelve", 48000.0, 480, [] (Plugin& p) { p.set ("guitarType", 1.f); play (p); });
    run ("baritone", 48000.0, 480, [] (Plugin& p) { p.set ("guitarType", 2.f); play (p); });
    run ("nylon12", 48000.0, 480, [] (Plugin& p) { p.set ("guitarType", 3.f); play (p); });
    run ("seven", 48000.0, 480, [] (Plugin& p) { p.set ("guitarType", 4.f); p.noteOn (0.01, 35, 0.85f); play (p); });
    run ("body0", 48000.0, 480, [] (Plugin& p) { p.set ("body", 0.f); play (p); });
    run ("body1", 48000.0, 480, [] (Plugin& p) { p.set ("body", 1.f); play (p); });
    run ("body2", 48000.0, 480, [] (Plugin& p) { p.set ("body", 2.f); play (p); });
    run ("body3", 48000.0, 480, [] (Plugin& p) { p.set ("body", 3.f); play (p); });
    run ("pickup-single", 48000.0, 480, [] (Plugin& p) { p.set ("body", 2.f).set ("pickupType", 0.f); play (p); });
    run ("pickup-humbucker", 48000.0, 480, [] (Plugin& p) { p.set ("body", 2.f).set ("pickupType", 1.f); play (p); });
    run ("pickup-p90", 48000.0, 480, [] (Plugin& p) { p.set ("body", 2.f).set ("pickupType", 2.f); play (p); });
    run ("pickup-sel3", 48000.0, 480, [] (Plugin& p) { p.set ("body", 3.f).set ("pickupSel", 3.f); play (p); });
    run ("gauge0", 48000.0, 480, [] (Plugin& p) { p.set ("stringSet", 0.f); play (p); });
    run ("gauge3", 48000.0, 480, [] (Plugin& p) { p.set ("stringSet", 3.f); play (p); });
    run ("gauge5", 48000.0, 480, [] (Plugin& p) { p.set ("stringSet", 5.f); play (p); });
    run ("material2", 48000.0, 480, [] (Plugin& p) { p.set ("material", 2.f); play (p); });
    run ("material9", 48000.0, 480, [] (Plugin& p) { p.set ("material", 9.f); play (p); });
    run ("tuning-dropD", 48000.0, 480, [] (Plugin& p) { p.set ("tuning", 1.f); play (p); });
    run ("tuning-nashville", 48000.0, 480, [] (Plugin& p) { p.set ("tuning", 10.f); play (p); });
    run ("capo3", 48000.0, 480, [] (Plugin& p) { p.set ("capo", 3.f); play (p); });
    run ("hammer-on", 48000.0, 480, [] (Plugin& p) { p.noteOn (0.01, 45, 0.7f); p.noteOn (0.6, 47, 0.7f); p.noteOff (1.4, 47).noteOff (1.4, 45); });
    run ("slide", 48000.0, 480, [] (Plugin& p) { p.noteOn (0.01, 45, 0.9f); p.noteOn (0.6, 50, 0.95f); p.noteOff (1.6, 50).noteOff (1.6, 45); });
    run ("harmonic", 48000.0, 480, [] (Plugin& p) { p.noteOn (0.01, 52, 1.0f); });
    run ("palm-dead", 48000.0, 480, [] (Plugin& p) { p.set ("damping", 1.f); p.noteOn (0.01, 40, 0.3f); p.noteOn (0.5, 45, 0.1f); });
    run ("strum", 48000.0, 480, [] (Plugin& p) { p.set ("mode", 1.f); p.chord (0.01, { 40, 47, 52, 56, 59, 64 }, 0.85f, 0.0); });
    run ("bend-wheel", 48000.0, 480, [] (Plugin& p) { p.noteOn (0.01, 45, 0.8f); p.bend (0.4, 12000); p.cc (0.8, 1, 100); });
    run ("fretnoise-max", 48000.0, 480, [] (Plugin& p) { p.set ("fretNoise", 100.f).set ("sympathetic", 80.f); play (p); });
    run ("parts-extreme", 48000.0, 480, [] (Plugin& p) { p.set ("scaleLength", 22.f).set ("neckMass", 100.f).set ("bridgeMass", 100.f).set ("magnetPull", 100.f).set ("airVolume", 1.3f); play (p); });
    run ("parts-long", 48000.0, 480, [] (Plugin& p) { p.set ("scaleLength", 30.f).set ("saddleHardness", 0.f).set ("pickupWindings", 100.f); play (p); });
    run ("amp-fx", 48000.0, 480, [] (Plugin& p) { p.set ("ampModel", 2.f).set ("cab", 3.f).set ("chorusMix", 40.f).set ("delayMix", 30.f); play (p); });
    run ("sustain-age", 48000.0, 480, [] (Plugin& p) { p.set ("sustain", 200.f).set ("stringAge", 100.f).set ("stiffness", 200.f); play (p); });
}

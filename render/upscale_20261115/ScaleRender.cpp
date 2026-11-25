// ScaleRender: chromatic scale (frets 0..24) on one forced string through GuitarEngine.
// Usage: ScaleRender <outdir> <string 0..5> <label> <velocity 0..1> [room%] [body 0..3] [micBlend%] [stiffness%] [age%]
// Writes <label>.wav (48 kHz, 24-bit, stereo) and <label>.csv (fret, pitch, onset sample).
#include "Contracts.h"
#include "dsp/GuitarEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
constexpr double kFs    = 48000.0;
constexpr int    kBlock = 480;
constexpr int    kFrets = 25;     // fret 0..24
constexpr double kSlot  = 0.9;    // seconds between onsets
constexpr double kTail  = 1.0;    // seconds after the last onset
// Standard tuning, string index 0 = low E ... 5 = high E (MIDI sounding pitch).
constexpr float  kOpenPitch[fw::kNumStrings] = { 40.f, 45.f, 50.f, 55.f, 59.f, 64.f };
} // namespace

int main (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "usage: ScaleRender <outdir> <string> <label> <velocity01> [room%%]\n"); return 2; }
    const juce::File dir (argv[1]);
    dir.createDirectory ();
    const int   string = std::atoi (argv[2]);
    const juce::String label (argv[3]);
    const float vel = (float) std::atof (argv[4]);
    const float room = argc > 5 ? (float) std::atof (argv[5]) : 12.f;
    const int   body = argc > 6 ? std::atoi (argv[6]) : 0;
    const float micBlend = argc > 7 ? (float) std::atof (argv[7]) : 70.f;
    const float stiffness = argc > 8 ? (float) std::atof (argv[8]) : 100.f;
    const float age = argc > 9 ? (float) std::atof (argv[9]) : 20.f;
    if (string < 0 || string >= fw::kNumStrings) { std::fprintf (stderr, "bad string\n"); return 2; }

    const int total = (int) ((kFrets * kSlot + kTail) * kFs);
    std::vector<int> onsets (kFrets);
    for (int f = 0; f < kFrets; ++f) onsets[(std::size_t) f] = (int) (f * kSlot * kFs);

    // GuitarEngine holds several MB of string delay lines inline: keep it off the stack.
    auto eng = std::make_unique<fw::dsp::GuitarEngine> ();
    eng->prepare (kFs, kBlock);
    fw::dsp::EngineParams ep {};
    ep.room = room;
    ep.body = body;
    ep.micBlend = micBlend;
    for (int k = 0; k < fw::kNumStrings; ++k) { ep.strings[(std::size_t) k].stiffness = stiffness; ep.strings[(std::size_t) k].age = age; }
    eng->setParams (ep);

    juce::AudioBuffer<float> all (2, total);
    all.clear ();
    std::vector<float> L (kBlock), R (kBlock);

    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min (kBlock, total - start);
        for (int f = 0; f < kFrets; ++f)
        {
            const int at = onsets[(std::size_t) f];
            if (at < start || at >= start + n) continue;
            fw::StringEvent e;
            e.sampleOffset = at - start;
            e.string   = string;
            e.type     = fw::EventType::Pluck;
            e.pitch    = kOpenPitch[string] + (float) f;
            e.velocity = vel;
            eng->handle (e);
        }
        eng->render (L.data (), R.data (), n);
        for (int i = 0; i < n; ++i)
        {
            all.setSample (0, start + i, L[(std::size_t) i]);
            all.setSample (1, start + i, R[(std::size_t) i]);
        }
    }

    const juce::File wavFile = dir.getChildFile (label + ".wav");
    wavFile.deleteFile ();
    std::unique_ptr<juce::FileOutputStream> fos (wavFile.createOutputStream ().release ());
    if (fos == nullptr) return 1;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (fos.get (), kFs, 2, 24, {}, 0));
    if (w == nullptr) return 1;
    fos.release ();
    if (! w->writeFromAudioSampleBuffer (all, 0, total)) return 1;
    w.reset ();

    juce::String csv = "fret,pitch,onset_sample\n";
    for (int f = 0; f < kFrets; ++f)
        csv += juce::String (f) + "," + juce::String ((int) (kOpenPitch[string] + f)) + ","
             + juce::String (onsets[(std::size_t) f]) + "\n";
    dir.getChildFile (label + ".csv").replaceWithText (csv);

    std::printf ("%s: string %d, room %.1f, body %d, micBlend %.1f, %d samples, %d notes, velocity %.3f\n",
                 label.toRawUTF8 (), string, (double) room, body, (double) micBlend, total, kFrets, (double) vel);
    return 0;
}

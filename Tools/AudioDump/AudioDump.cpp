// AudioDump: renders the same four SoundJudge passages through the real
// processor and writes them as 24-bit stereo WAV for read-only metrics.
// Separate from SoundJudge and the judge rubric. Usage: AudioDump <outdir>
// Writes <outdir>/<case>.wav, 48 kHz.
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <memory>
#include <vector>

namespace
{
struct Note { double t; int note; float vel; bool on; };

juce::AudioBuffer<float> render (const std::vector<Note>& evs, double secs,
                                 const std::vector<std::pair<const char*, float>>& params)
{
    auto proc = std::make_unique<FretworkAudioProcessor> ();
    proc->prepareToPlay (48000.0, 480);
    for (const auto& kv : params)
        if (auto* p = proc->getState ().getParameter (kv.first))
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));

    const int total = (int) (secs * 48000.0);
    juce::AudioBuffer<float> all (2, total);
    for (int start = 0; start < total; start += 480)
    {
        const int n = std::min (480, total - start);
        juce::AudioBuffer<float> buf (2, n);
        buf.clear ();
        juce::MidiBuffer midi;
        for (const auto& e : evs)
            if (e.t * 48000.0 >= start && e.t * 48000.0 < start + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note + fw::kWrittenOctave, e.vel)
                                    : juce::MidiMessage::noteOff (1, e.note + fw::kWrittenOctave),
                               (int) (e.t * 48000.0) - start);
        proc->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, start, buf, c, 0, n);
    }
    return all;
}

bool writeWav (const juce::File& out, const juce::AudioBuffer<float>& buf)
{
    out.deleteFile ();
    std::unique_ptr<juce::FileOutputStream> fos (out.createOutputStream ().release ());
    if (fos == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (fos.get (), 48000.0, 2, 24, {}, 0));
    if (w == nullptr) return false;
    fos.release ();
    return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples ());
}
} // namespace

int main (int argc, char** argv)
{
    const juce::String dir = argc > 1 ? juce::String (argv[1]) : juce::String (".");
    juce::File d (dir);
    d.createDirectory ();

    struct Case { const char* name; std::vector<Note> evs; double secs;
                  std::vector<std::pair<const char*, float>> params; };

    std::vector<Case> cases;
    cases.push_back ({ "standard_soft_low_E", { { 0.02, 40, 0.40f, true }, { 3.4, 40, 0.f, false } }, 3.6, {} });
    cases.push_back ({ "standard_hard_low_E", { { 0.02, 40, 126.f / 127.f, true }, { 3.4, 40, 0.f, false } }, 3.6, {} });
    cases.push_back ({ "pure_nickel_hard_low_E",
                       { { 0.02, 40, 126.f / 127.f, true }, { 3.4, 40, 0.f, false } }, 3.6,
                       { { "stringSet", 1.f }, { "material", 4.f } } });
    cases.push_back ({ "twelve_string_low_E",
                       { { 0.02, 40, 126.f / 127.f, true }, { 3.4, 40, 0.f, false } }, 3.6,
                       { { "guitarType", 1.f } } });

    for (const auto& c : cases)
    {
        const auto buf = render (c.evs, c.secs, c.params);
        const auto file = d.getChildFile (juce::String (c.name) + ".wav");
        const bool ok = writeWav (file, buf);
        std::printf ("%s: %s (%d samples)\n", c.name, ok ? "ok" : "FAILED", buf.getNumSamples ());
    }
    return 0;
}

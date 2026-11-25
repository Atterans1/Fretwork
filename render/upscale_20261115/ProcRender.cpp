// ProcRender: high E pitches 84..88 (frets 20..24, unambiguous: B string fret 24 = 83, high E only above) through FretworkAudioProcessor.
// Same MIDI convention as Tools/AudioDump: sent note = sounding pitch + kWrittenOctave. Usage: ProcRender <outdir> <label> <velocity01>
#include "PluginProcessor.h"
#include "Contracts.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "usage: ProcRender <outdir> <label> <velocity01>\n"); return 2; }
    const juce::File dir (argv[1]);
    dir.createDirectory ();
    const juce::String label (argv[2]);
    const float vel = (float) std::atof (argv[3]);
    const double fs = 48000.0;
    const int block = 480;
    const double slot = 0.9, tail = 1.0;
    const int pitches[5] = { 84, 85, 86, 87, 88 };
    const int frames = (int) ((5 * slot + tail) * fs);

    auto proc = std::make_unique<FretworkAudioProcessor> ();
    proc->prepareToPlay (fs, block);

    juce::AudioBuffer<float> all (2, frames);
    all.clear ();
    for (int start = 0; start < frames; start += block)
    {
        const int n = std::min (block, frames - start);
        juce::AudioBuffer<float> buf (2, n);
        buf.clear ();
        juce::MidiBuffer midi;
        for (int k = 0; k < 5; ++k)
        {
            const int onAt = (int) (k * slot * fs), offAt = (int) ((k * slot + 0.8) * fs);
            const int midiNote = pitches[k] + fw::kWrittenOctave;
            if (onAt >= start && onAt < start + n)
                midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, vel), onAt - start);
            if (offAt >= start && offAt < start + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, midiNote), offAt - start);
        }
        proc->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, start, buf, c, 0, n);
    }

    const juce::File wavFile = dir.getChildFile (label + ".wav");
    wavFile.deleteFile ();
    std::unique_ptr<juce::FileOutputStream> fos (wavFile.createOutputStream ().release ());
    if (fos == nullptr) return 1;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (fos.get (), fs, 2, 24, {}, 0));
    if (w == nullptr) return 1;
    fos.release ();
    if (! w->writeFromAudioSampleBuffer (all, 0, frames)) return 1;
    w.reset ();

    juce::String csv = "fret,pitch,onset_sample\n";
    for (int k = 0; k < 5; ++k)
        csv += juce::String (pitches[k] - 64) + "," + juce::String (pitches[k]) + "," + juce::String ((int) (k * slot * fs)) + "\n";
    dir.getChildFile (label + ".csv").replaceWithText (csv);
    std::printf ("%s: processor, %d samples, pitches 84..88, velocity %.3f\n", label.toRawUTF8 (), frames, (double) vel);
    return 0;
}

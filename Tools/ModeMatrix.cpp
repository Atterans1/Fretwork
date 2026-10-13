// ModeMatrix: plays every Fretwork mode and factory preset through a real
// build and scores the AUDIO, not parameters.
//
//   ModeMatrix list                         rows and their MIDI stimulus as JSON
//   ModeMatrix vst3 <Fretwork.vst3> <outdir> host the VST3, render every row, score
//   ModeMatrix analyze <row> <wav>           score a recording of the Standalone
//
// Pass criteria, fixed before any run:
//   - finite, peak below 0 dBFS, RMS over the note above -50 dBFS (not silent)
//   - Poly: fundamental within 30 cents of C4 (261.63 Hz)
//   - every other row holds C-E-G: C, E and G are all among the five strongest
//     pitch classes of the sustained sound
//   - Strum: at least 2 attacks (down and up stroke); Arp and Seq: at least 3

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <cstdio>

namespace
{
struct Ev { double t; int note; bool on; };

struct Row
{
    juce::String name;
    juce::String kind;                // "mode" or "preset"
    int program = -1;                 // factory program index for presets
    juce::StringArray params;         // "Name=value" applied before playing
    std::vector<Ev> events;
    juce::String expect;              // "C4" or "CEG"
    int minOnsets = 1;
};

const char* const kPresets[] = {
    "Campfire Strum", "Fingerstyle Folk", "Palm-Muted Pop", "Bossa Nova", "Classical Arpeggio", "Nylon Lead",
    "Clean Funk Rhythm", "Crunch Rhythm", "Blues Lead", "Jazz Box Comping", "Warm Semi Strum", "Rockabilly Slap",
    "Crunch Amp", "Lead Stack", "Chorus Clean", "Jazz Amp" };

constexpr double kLen = 2.5;

// C-E-G held 0.05..2.2 s, a down stroke (key 28) at 0.3 s and an up stroke
// (key 29) at 0.9 s. Stroke keys only act in Strum, so every mode gets the
// same input.
std::vector<Ev> chordWithStrokes()
{
    return { { 0.05, 60, true }, { 0.05, 64, true }, { 0.05, 67, true },
             { 0.30, 28, true }, { 0.40, 28, false }, { 0.90, 29, true }, { 1.00, 29, false },
             { 2.20, 60, false }, { 2.20, 64, false }, { 2.20, 67, false } };
}

std::vector<Row> rows()
{
    std::vector<Row> r;
    r.push_back ({ "Poly",  "mode", -1, { "Mode=Poly" },  { { 0.05, 60, true }, { 2.2, 60, false } }, "C4", 1 });
    r.push_back ({ "Strum", "mode", -1, { "Mode=Strum" }, chordWithStrokes(), "CEG", 2 });
    r.push_back ({ "Arp",   "mode", -1, { "Mode=Arp" },   chordWithStrokes(), "CEG", 3 });
    r.push_back ({ "Seq",   "mode", -1, { "Mode=Seq", "Seq Play=On" }, chordWithStrokes(), "CEG", 3 });
    for (int i = 0; i < (int) std::size (kPresets); ++i)
        r.push_back ({ kPresets[i], "preset", i, {}, chordWithStrokes(), "CEG", 1 });
    return r;
}

// ------------------------------------------------------------------ analysis
struct Score
{
    bool pass = false;
    double peakDb = -200, rmsDb = -200, f0 = 0, cents = 0;
    int onsets = 0;
    juce::String top5, why;
};

Score score (const Row& row, const std::vector<float>& x, double fs)
{
    Score s;
    bool finite = true;
    double pk = 0, ss = 0; size_t n = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        finite = finite && std::isfinite (x[i]);
        pk = std::max (pk, (double) std::abs (x[i]));
        const double t = (double) i / fs;
        if (t > 0.1 && t < 2.2) { ss += (double) x[i] * x[i]; ++n; }
    }
    s.peakDb = 20 * std::log10 (pk + 1e-12);
    s.rmsDb = 10 * std::log10 (ss / std::max<size_t> (1, n) + 1e-24);

    // Attacks: 10 ms RMS envelope rising 6 dB over 30 ms, above -45 dBFS, 80 ms apart.
    {
        const int hop = (int) (0.01 * fs);
        std::vector<double> env;
        for (size_t i = 0; i + (size_t) hop <= x.size(); i += (size_t) hop)
        {
            double e = 0; for (int k = 0; k < hop; ++k) e += (double) x[i + (size_t) k] * x[i + (size_t) k];
            env.push_back (10 * std::log10 (e / hop + 1e-24));
        }
        int last = -100;
        for (int i = 3; i < (int) env.size(); ++i)
            if (env[(size_t) i] > -45 && env[(size_t) i] - env[(size_t) i - 3] > 6 && i - last >= 8) { ++s.onsets; last = i; }
    }

    // Pitch-class energy over 0.5..2.0 s (16k FFT frames).
    const int N = 16384;
    juce::dsp::FFT fft (14);
    std::vector<float> buf ((size_t) (2 * N));
    double pcs[12] {}; double spec[N / 2] {};
    for (double t0 = 0.5; t0 + N / fs <= 2.1; t0 += 0.25)
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        const size_t a = (size_t) (t0 * fs);
        if (a + (size_t) N > x.size()) break;
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[a + (size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < N / 2; ++k) spec[k] += buf[(size_t) k];
    }
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * fs / N;
        if (f < 70 || f > 2000) continue;
        const int pc = ((int) std::lround (12 * std::log2 (f / 440.0)) % 12 + 12 + 9) % 12;   // 0 = C
        pcs[pc] += spec[k] * spec[k];
    }
    int order[12]; for (int i = 0; i < 12; ++i) order[i] = i;
    std::sort (order, order + 12, [&] (int a, int b) { return pcs[a] > pcs[b]; });
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    bool inTop[12] {};
    for (int i = 0; i < 5; ++i) { inTop[order[i]] = true; s.top5 << names[order[i]] << (i < 4 ? " " : ""); }

    // Fundamental near C4: strongest bin 200..330 Hz, interpolated.
    {
        int best = 0; double bv = 0;
        for (int k = (int) (200 * N / fs); k < (int) (330 * N / fs); ++k) if (spec[k] > bv) { bv = spec[k]; best = k; }
        if (best > 0)
        {
            const double l = std::log (spec[best - 1] + 1e-12), c = std::log (spec[best] + 1e-12), rr = std::log (spec[best + 1] + 1e-12);
            s.f0 = (best + 0.5 * (l - rr) / (l - 2 * c + rr)) * fs / N;
            s.cents = 1200 * std::log2 (s.f0 / 261.6256);
        }
    }

    juce::StringArray why;
    if (! finite) why.add ("non-finite samples");
    if (s.peakDb >= 0) why.add ("clipped");
    if (s.rmsDb <= -50) why.add ("silent");
    if (row.expect == "C4" && std::abs (s.cents) > 30) why.add ("pitch off C4");
    if (row.expect == "CEG" && ! (inTop[0] && inTop[4] && inTop[7])) why.add ("C/E/G not dominant");
    if (s.onsets < row.minOnsets) why.add ("too few attacks");
    s.pass = why.isEmpty();
    s.why = why.joinIntoString (", ");
    return s;
}

juce::String report (const Row& r, const Score& s)
{
    juce::String o;
    o << r.name << "\t" << (s.pass ? "PASS" : "FAIL") << "\tpeak " << juce::String (s.peakDb, 1) << " dBFS, rms "
      << juce::String (s.rmsDb, 1) << " dBFS, " << s.onsets << " attacks, top pitch classes [" << s.top5 << "]";
    if (r.expect == "C4") o << ", f0 " << juce::String (s.f0, 2) << " Hz (" << (s.cents >= 0 ? "+" : "") << juce::String (s.cents, 1) << " c)";
    if (! s.pass) o << " -- " << s.why;
    return o;
}

bool readMono (const juce::File& f, std::vector<float>& out, double& fs)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return false;
    juce::AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples);
    rd->read (&b, 0, b.getNumSamples(), 0, true, true);
    out.assign ((size_t) b.getNumSamples(), 0.f);
    for (int c = 0; c < b.getNumChannels(); ++c)
        juce::FloatVectorOperations::addWithMultiply (out.data(), b.getReadPointer (c), 1.f / b.getNumChannels(), b.getNumSamples());
    fs = rd->sampleRate;
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String cmd = argc > 1 ? argv[1] : "";
    const auto all = rows();

    if (cmd == "list")
    {
        juce::Array<juce::var> arr;
        for (const auto& r : all)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", r.name); o->setProperty ("kind", r.kind); o->setProperty ("program", r.program);
            o->setProperty ("params", r.params.joinIntoString (";"));
            juce::Array<juce::var> ev;
            for (const auto& e : r.events) ev.add (juce::String ((int) std::lround (e.t * 1000)) + (e.on ? " on " : " off ") + juce::String (e.note) + " 100");
            o->setProperty ("events", ev);
            arr.add (juce::var (o));
        }
        std::printf ("%s\n", juce::JSON::toString (arr, true).toRawUTF8());
        return 0;
    }

    if (cmd == "analyze" && argc >= 4)
    {
        const juce::String name = argv[2];
        for (const auto& r : all)
            if (r.name == name)
            {
                std::vector<float> x; double fs = 0;
                if (! readMono (juce::File (argv[3]), x, fs)) { std::printf ("cannot read %s\n", argv[3]); return 2; }
                const auto s = score (r, x, fs);
                std::printf ("%s\n", report (r, s).toRawUTF8());
                return s.pass ? 0 : 1;
            }
        std::printf ("no row %s\n", name.toRawUTF8());
        return 2;
    }

    if (cmd == "vst3" && argc >= 4)
    {
        const juce::File plugin (argv[2]), outDir (argv[3]);
        outDir.createDirectory();
        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
        juce::OwnedArray<juce::PluginDescription> types;
        fm.getFormat (0)->findAllTypesForFile (types, plugin.getFullPathName());
        if (types.isEmpty()) { std::printf ("no VST3 in %s\n", argv[2]); return 2; }
        std::printf ("plugin %s %s (%s)\n", types[0]->name.toRawUTF8(), types[0]->version.toRawUTF8(), plugin.getFullPathName().toRawUTF8());

        const double fs = 48000.0; const int blk = 512;
        int fails = 0;
        for (const auto& r : all)
        {
            juce::String err;
            auto p = fm.createPluginInstance (*types[0], fs, blk, err);   // fresh instance per row
            if (p == nullptr) { std::printf ("%s\tFAIL\tload: %s\n", r.name.toRawUTF8(), err.toRawUTF8()); ++fails; continue; }
            p->enableAllBuses();
            if (r.program >= 0)
            {
                if (p->getProgramName (r.program) != r.name) { std::printf ("%s\tFAIL\tprogram %d is '%s'\n", r.name.toRawUTF8(), r.program, p->getProgramName (r.program).toRawUTF8()); ++fails; continue; }
                p->setCurrentProgram (r.program);
            }
            bool ok = true;
            for (const auto& kv : r.params)
            {
                juce::AudioProcessorParameter* q = nullptr;
                for (auto* c : p->getParameters()) if (c->getName (64) == kv.upToFirstOccurrenceOf ("=", false, false)) q = c;
                if (q == nullptr) { std::printf ("%s\tFAIL\tno parameter %s\n", r.name.toRawUTF8(), kv.toRawUTF8()); ok = false; break; }
                q->setValueNotifyingHost (q->getValueForText (kv.fromFirstOccurrenceOf ("=", false, false)));
            }
            if (! ok) { ++fails; continue; }
            p->prepareToPlay (fs, blk);

            const int total = (int) (kLen * fs);
            const int chans = std::max (2, p->getTotalNumOutputChannels());
            juce::AudioBuffer<float> b (chans, blk);
            std::vector<float> L, R;
            for (int pos = 0; pos < total; pos += blk)
            {
                const int n = std::min (blk, total - pos);
                b.setSize (chans, n, false, false, true); b.clear();
                juce::MidiBuffer m;
                for (const auto& e : r.events)
                {
                    const int at = (int) std::lround (e.t * fs);
                    if (at >= pos && at < pos + n)
                        m.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.note), at - pos);
                }
                p->processBlock (b, m);
                L.insert (L.end(), b.getReadPointer (0), b.getReadPointer (0) + n);
                R.insert (R.end(), b.getReadPointer (1), b.getReadPointer (1) + n);
            }
            p->releaseResources();
            p.reset();

            const auto wav = outDir.getChildFile (r.name.replaceCharacter (' ', '_') + ".wav");
            wav.deleteFile();
            {
                juce::WavAudioFormat wf;
                std::unique_ptr<juce::AudioFormatWriter> w (wf.createWriterFor (new juce::FileOutputStream (wav), fs, 2, 24, {}, 0));
                const float* ch[] = { L.data(), R.data() };
                w->writeFromFloatArrays (ch, 2, (int) L.size());
            }
            std::vector<float> mono (L.size());
            for (size_t i = 0; i < L.size(); ++i) mono[i] = 0.5f * (L[i] + R[i]);
            const auto s = score (r, mono, fs);
            if (! s.pass) ++fails;
            std::printf ("%s\n", report (r, s).toRawUTF8());
        }
        std::printf ("%d rows, %d failed\n", (int) all.size(), fails);
        return fails == 0 ? 0 : 1;
    }

    std::printf ("usage: ModeMatrix list | vst3 <plugin.vst3> <outdir> | analyze <row> <wav>\n");
    return 2;
}

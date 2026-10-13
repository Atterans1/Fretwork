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

#include "ArticMatrix.h"

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
    juce::String seq;                 // sequencer slot 0, SeqStore text format (row,start,len,vel;...)
};

const char* const kPresets[] = {
    "Campfire Strum", "Fingerstyle Folk", "Palm-Muted Pop", "Bossa Nova", "Classical Arpeggio", "Nylon Lead",
    "Clean Funk Rhythm", "Crunch Rhythm", "Blues Lead", "Jazz Box Comping", "Warm Semi Strum", "Rockabilly Slap",
    "Crunch Amp", "Lead Stack", "Chorus Clean", "Jazz Amp", "Pop Strum", "Rock Rhythm" };

constexpr double kLen = 2.5;

// Rows name SOUNDING pitches (C4 = 60). Guitar MIDI is written an octave up, so
// pitches are sent +12; keyswitches (below MIDI 38) are keys and go unchanged.
int written (int note) { return note < 38 ? note : note + 12; }

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
    r.push_back ({ "Seq",   "mode", -1, { "Mode=Seq", "Seq Play=On", "Seq Slot=1" }, chordWithStrokes(), "CEG", 3 });
    // A fresh instance has empty slots, so the Seq row loads a one-bar pattern
    // through the plugin state, as a saved project does: down strokes (row 0)
    // on the beats, up strokes (row 1) on the off-beats.
    r.back().seq = "0,0,1,100;1,2,1,90;0,4,1,100;1,6,1,90;0,8,1,100;1,10,1,90;0,12,1,100;1,14,1,90";
    for (int i = 0; i < (int) std::size (kPresets); ++i)
        // Presets get a plain held chord, no stroke keys: each must sound out of the box.
        r.push_back ({ kPresets[i], "preset", i, {}, { { 0.05, 60, true }, { 0.05, 64, true }, { 0.05, 67, true },
                                                        { 2.20, 60, false }, { 2.20, 64, false }, { 2.20, 67, false } }, "CEG", 1 });
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
            for (const auto& e : r.events) ev.add (juce::String ((int) std::lround (e.t * 1000)) + (e.on ? " on " : " off ") + juce::String (written (e.note)) + " 100");
            o->setProperty ("events", ev);
            arr.add (juce::var (o));
        }
        std::printf ("%s\n", juce::JSON::toString (arr, true).toRawUTF8());
        return 0;
    }

    // ---------------------------------------------------------- articulations
    const auto phrases = artic::phrases();

    if (cmd == "artic-list")
    {
        juce::Array<juce::var> arr;
        for (const auto& p : phrases)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", p.name);
            juce::StringArray lines;
            auto ev = p.events;
            std::stable_sort (ev.begin(), ev.end(), [] (const artic::AEv& a, const artic::AEv& b) { return a.t < b.t; });
            for (const auto& e : ev)
                lines.add (juce::String ((int) std::lround (e.t * 1000)) + (e.kind == 1 ? " on " : e.kind == 0 ? " off " : " pb ")
                           + juce::String (e.kind == 3 ? e.a : written (e.a)) + " " + juce::String (e.kind == 1 ? e.b : 0));
            o->setProperty ("midisend", lines.joinIntoString ("\n"));
            o->setProperty ("first", ev.empty() ? 0.0 : ev.front().t);
            arr.add (juce::var (o));
        }
        std::printf ("%s\n", juce::JSON::toString (arr, true).toRawUTF8());
        return 0;
    }

    if (cmd == "artic-analyze" && argc >= 5)
    {
        const juce::String name = argv[2];
        for (const auto& p : phrases)
            if (p.name == name)
            {
                std::vector<float> x; double fs = 0;
                if (! readMono (juce::File (argv[3]), x, fs)) { std::printf ("cannot read %s\n", argv[3]); return 2; }
                // Align the recording: its first attack (5 ms RMS above -50 dBFS) is the phrase's first note-on.
                double first = 1e9; for (const auto& e : p.events) if (e.kind == 1) first = std::min (first, e.t);
                const int hop = (int) (0.005 * fs);
                long lead = -1;
                for (size_t i = 0; i + (size_t) hop < x.size(); i += (size_t) hop)
                {
                    double e = 0; for (int k = 0; k < hop; ++k) e += (double) x[i + (size_t) k] * x[i + (size_t) k];
                    if (10 * std::log10 (e / hop + 1e-24) > -50) { lead = (long) i; break; }
                }
                if (lead < 0) { std::printf ("%s\tFAIL\trecording is silent\n", name.toRawUTF8()); return 1; }
                const long shift = lead - (long) (first * fs);
                std::vector<float> y ((size_t) (artic::kLen * fs), 0.f);
                for (size_t i = 0; i < y.size(); ++i) { const long j = (long) i + shift; if (j >= 0 && j < (long) x.size()) y[i] = x[(size_t) j]; }
                const auto r = p.measure (y, fs);
                artic::spectrogramPng (y, fs, p, juce::File (argv[4]));
                std::printf ("%s\t%s\t%s (aligned: first attack at %.3f s in the capture)\n", name.toRawUTF8(), r.pass ? "PASS" : "FAIL",
                             r.evidence.toRawUTF8(), (double) lead / fs);
                return r.pass ? 0 : 1;
            }
        std::printf ("no phrase %s\n", name.toRawUTF8());
        return 2;
    }

    if (cmd == "artic-vst3" && argc >= 4)
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
        for (const auto& ph : phrases)
        {
            juce::String err;
            auto p = fm.createPluginInstance (*types[0], fs, blk, err);
            if (p == nullptr) { std::printf ("%s\tFAIL\tload: %s\n", ph.name.toRawUTF8(), err.toRawUTF8()); ++fails; continue; }
            p->enableAllBuses();
            p->prepareToPlay (fs, blk);
            const int total = (int) (artic::kLen * fs), chans = std::max (2, p->getTotalNumOutputChannels());
            juce::AudioBuffer<float> b (chans, blk);
            std::vector<float> L, R;
            for (int pos = 0; pos < total; pos += blk)
            {
                const int n = std::min (blk, total - pos);
                b.setSize (chans, n, false, false, true); b.clear();
                juce::MidiBuffer m;
                for (const auto& e : ph.events)
                {
                    const int at = (int) std::lround (e.t * fs);
                    if (at < pos || at >= pos + n) continue;
                    if (e.kind == 1) m.addEvent (juce::MidiMessage::noteOn (1, written (e.a), (juce::uint8) e.b), at - pos);
                    else if (e.kind == 0) m.addEvent (juce::MidiMessage::noteOff (1, written (e.a)), at - pos);
                    else m.addEvent (juce::MidiMessage::pitchWheel (1, e.a), at - pos);
                }
                p->processBlock (b, m);
                L.insert (L.end(), b.getReadPointer (0), b.getReadPointer (0) + n);
                R.insert (R.end(), b.getReadPointer (1), b.getReadPointer (1) + n);
            }
            p->releaseResources();
            p.reset();
            const auto base = outDir.getChildFile (ph.name.replaceCharacters (" ()", "___"));
            {
                const auto wav = base.withFileExtension ("wav");
                wav.deleteFile();
                juce::WavAudioFormat wf;
                std::unique_ptr<juce::AudioFormatWriter> w (wf.createWriterFor (new juce::FileOutputStream (wav), fs, 2, 24, {}, 0));
                const float* ch[] = { L.data(), R.data() };
                w->writeFromFloatArrays (ch, 2, (int) L.size());
            }
            std::vector<float> mono (L.size());
            for (size_t i = 0; i < L.size(); ++i) mono[i] = 0.5f * (L[i] + R[i]);
            const auto r = ph.measure (mono, fs);
            artic::spectrogramPng (mono, fs, ph, base.withFileExtension ("png"));
            if (! r.pass) ++fails;
            std::printf ("%s\t%s\t%s\n", ph.name.toRawUTF8(), r.pass ? "PASS" : "FAIL", r.evidence.toRawUTF8());
        }
        std::printf ("%d articulations, %d failed\n", (int) phrases.size(), fails);
        return fails == 0 ? 0 : 1;
    }

    if (cmd == "analyze" && argc >= 4)
    {
        const juce::String name = argv[2];
        for (const auto& r : all)
            if (r.name == name)
            {
                std::vector<float> x; double fs = 0;
                if (! readMono (juce::File (argv[3]), x, fs)) { std::printf ("cannot read %s\n", argv[3]); return 2; }
                // Align the loopback capture: its first sound (5 ms RMS above -50 dBFS)
                // is placed at the row's first note-on.
                double first = 1e9; for (const auto& e : r.events) if (e.on) first = std::min (first, e.t);
                const int hop = (int) (0.005 * fs);
                long lead = -1;
                for (size_t i = 0; i + (size_t) hop < x.size(); i += (size_t) hop)
                {
                    double e = 0; for (int k = 0; k < hop; ++k) e += (double) x[i + (size_t) k] * x[i + (size_t) k];
                    if (10 * std::log10 (e / hop + 1e-24) > -50) { lead = (long) i; break; }
                }
                if (lead < 0) { std::printf ("%s\tFAIL\trecording is silent\n", name.toRawUTF8()); return 1; }
                const long shift = lead - (long) (first * fs);
                std::vector<float> y ((size_t) (kLen * fs), 0.f);
                for (size_t i = 0; i < y.size(); ++i) { const long j = (long) i + shift; if (j >= 0 && j < (long) x.size()) y[i] = x[(size_t) j]; }
                const auto s = score (r, y, fs);
                std::printf ("%s\n", report (r, s).toRawUTF8());
                return s.pass ? 0 : 1;
            }
        std::printf ("no row %s\n", name.toRawUTF8());
        return 2;
    }

    // Applies a row's program, sequencer pattern and parameters to a fresh instance.
    const auto configure = [] (juce::AudioPluginInstance& p, const Row& r) -> juce::String
    {
        if (r.program >= 0)
        {
            if (p.getProgramName (r.program) != r.name) return "program " + juce::String (r.program) + " is '" + p.getProgramName (r.program) + "'";
            p.setCurrentProgram (r.program);
        }
        if (r.seq.isNotEmpty())
        {
            // A hosted VST3 returns JUCE binary XML (VST3PluginState); the plugin's own
            // ValueTree is base64 inside <IComponent>, possibly followed by wrapper data.
            juce::MemoryBlock mb; p.getStateInformation (mb);
            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            auto* comp = xml != nullptr ? xml->getChildByName ("IComponent") : nullptr;
            if (comp == nullptr) return "plugin state has no IComponent";
            juce::MemoryBlock inner;
            inner.fromBase64Encoding (comp->getAllSubText().trim());
            juce::MemoryInputStream in (inner, false);
            auto st = juce::ValueTree::readFromStream (in);
            const auto tail = (size_t) in.getPosition();
            auto seq = st.getChildWithName ("SEQ");
            if (! seq.isValid()) return "no SEQ node in the plugin state";
            seq.setProperty ("0", r.seq, nullptr);
            juce::MemoryOutputStream os;
            st.writeToStream (os);
            if (tail < inner.getSize()) os.write (static_cast<const char*> (inner.getData()) + tail, inner.getSize() - tail);
            comp->deleteAllTextElements();
            comp->addTextElement (os.getMemoryBlock().toBase64Encoding());
            juce::MemoryBlock out;
            juce::AudioProcessor::copyXmlToBinary (*xml, out);
            p.setStateInformation (out.getData(), (int) out.getSize());

            juce::MemoryBlock back; p.getStateInformation (back);
            auto bx = juce::AudioProcessor::getXmlFromBinary (back.getData(), (int) back.getSize());
            juce::MemoryBlock bi; bi.fromBase64Encoding (bx->getChildByName ("IComponent")->getAllSubText().trim());
            const auto got = juce::ValueTree::readFromData (bi.getData(), bi.getSize()).getChildWithName ("SEQ").getProperty ("0").toString();
            if (got != r.seq) return "sequencer pattern did not load (reads back '" + got + "')";
        }
        for (const auto& kv : r.params)
        {
            juce::AudioProcessorParameter* q = nullptr;
            for (auto* c : p.getParameters()) if (c->getName (64) == kv.upToFirstOccurrenceOf ("=", false, false)) q = c;
            if (q == nullptr) return "no parameter " + kv;
            q->setValueNotifyingHost (q->getValueForText (kv.fromFirstOccurrenceOf ("=", false, false)));
            const auto shown = q->getText (q->getValue(), 64);
            if (! shown.equalsIgnoreCase (kv.fromFirstOccurrenceOf ("=", false, false))) return kv + " reads back as " + shown;
        }
        return {};
    };

    // Writes a row's full plugin state as base64 (the Standalone's filterState value).
    if (cmd == "state" && argc >= 5)
    {
        juce::AudioPluginFormatManager fm;
        fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
        juce::OwnedArray<juce::PluginDescription> types;
        fm.getFormat (0)->findAllTypesForFile (types, argv[2]);
        if (types.isEmpty()) { std::printf ("no VST3 in %s\n", argv[2]); return 2; }
        for (const auto& r : all)
            if (r.name == juce::String (argv[3]))
            {
                juce::String err;
                auto p = fm.createPluginInstance (*types[0], 48000.0, 512, err);
                if (p == nullptr) { std::printf ("load: %s\n", err.toRawUTF8()); return 2; }
                const auto why = configure (*p, r);
                if (why.isNotEmpty()) { std::printf ("%s\n", why.toRawUTF8()); return 2; }
                // The Standalone stores the plugin's own state, not the VST3 host
                // container: unwrap <IComponent> and keep just the ValueTree.
                juce::MemoryBlock mb; p->getStateInformation (mb);
                auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
                auto* comp = xml != nullptr ? xml->getChildByName ("IComponent") : nullptr;
                if (comp == nullptr) { std::printf ("plugin state has no IComponent\n"); return 2; }
                juce::MemoryBlock inner; inner.fromBase64Encoding (comp->getAllSubText().trim());
                juce::MemoryOutputStream os;
                juce::ValueTree::readFromData (inner.getData(), inner.getSize()).writeToStream (os);
                juce::File (argv[4]).replaceWithText (os.getMemoryBlock().toBase64Encoding());
                return 0;
            }
        std::printf ("no row %s\n", argv[3]);
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
            if (const auto why = configure (*p, r); why.isNotEmpty())
            {
                std::printf ("%s\tFAIL\t%s\n", r.name.toRawUTF8(), why.toRawUTF8());
                ++fails;
                continue;
            }
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
                        m.addEvent (e.on ? juce::MidiMessage::noteOn (1, written (e.note), (juce::uint8) 100) : juce::MidiMessage::noteOff (1, written (e.note)), at - pos);
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

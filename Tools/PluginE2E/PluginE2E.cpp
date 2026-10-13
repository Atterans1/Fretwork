// PluginE2E: learn a real recording from its audio plus an independent
// reference (ASCII tab or MIDI), play the learned notes through any VST3
// instrument via JUCE's VST3 host, and verify the playback against the
// recording with independent measurements. See README.md.

#include "E2E.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace
{
constexpr double kRenderFs = 48000.0;
constexpr int    kBlock = 512;

struct Args
{
    juce::StringArray raw;
    juce::String get (const juce::String& key, const juce::String& def = {}) const
    {
        const int i = raw.indexOf (key);
        return i >= 0 && i + 1 < raw.size() ? raw[i + 1] : def;
    }
    juce::StringArray all (const juce::String& key) const
    {
        juce::StringArray out;
        for (int i = 0; i + 1 < raw.size(); ++i) if (raw[i] == key) out.add (raw[i + 1]);
        return out;
    }
    bool has (const juce::String& key) const { return raw.contains (key); }
};

void usage()
{
    std::printf (
        "PluginE2E --plugin X.vst3 --audio song.mp3 (--tab song.txt | --midi ref.mid) --out dir [options]\n"
        "  --tuning \"E2 A2 D3 G3 B3 E4\"   open strings low to high (default: from the tab's string count)\n"
        "  --param \"Name=value\"           set a plugin parameter by name, value in the plugin's own text (repeatable)\n"
        "  --program N                    select the plugin's program N first\n"
        "  --velocity 0..1                note-on velocity (default 0.8)\n"
        "  --pitch-tol SEMITONES          default 1\n"
        "  --onset-ms MS                  default 50\n"
        "  --min-rate 0..1                fraction that must pass pitch and onset (default 0.8)\n"
        "PluginE2E --plugin X.vst3 --list-params   print parameter names and current values\n");
}

std::vector<float> resampleTo (const std::vector<float>& x, double from, double to)
{
    std::vector<float> y (x);
    for (int pass = 0; pass < 2; ++pass)
    {
        juce::IIRFilter lp;
        lp.setCoefficients (juce::IIRCoefficients::makeLowPass (from, 0.45 * to));
        lp.processSamples (y.data(), (int) y.size());
    }
    const double ratio = from / to;
    std::vector<float> out ((size_t) std::floor ((double) (y.size() - 4) / ratio));
    juce::LagrangeInterpolator li;
    li.process (ratio, y.data(), out.data(), (int) out.size());
    return out;
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* q : p.getParameters())
        if (q->getName (256).equalsIgnoreCase (name.trim())) return q;
    return nullptr;
}

struct NoteEvent { int64_t sample; juce::MidiMessage msg; };

/** Renders the events through the hosted plugin, block by block, mono sum of all outputs. */
std::vector<float> renderPlugin (juce::AudioPluginInstance& p, std::vector<NoteEvent> ev, double seconds)
{
    std::stable_sort (ev.begin(), ev.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.sample < b.sample; });
    const int chans = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    const int outs = std::max (1, p.getTotalNumOutputChannels());
    const int64_t total = (int64_t) (seconds * kRenderFs);
    std::vector<float> out;
    out.reserve ((size_t) total);
    juce::AudioBuffer<float> buf (std::max (1, chans), kBlock);
    size_t next = 0;
    for (int64_t clock = 0; clock < total; clock += kBlock)
    {
        const int n = (int) std::min<int64_t> (kBlock, total - clock);
        buf.setSize (buf.getNumChannels(), n, false, false, true);
        buf.clear();
        juce::MidiBuffer midi;
        while (next < ev.size() && ev[next].sample < clock + n)
        {
            midi.addEvent (ev[next].msg, (int) std::max<int64_t> (0, ev[next].sample - clock));
            ++next;
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < n; ++i)
        {
            float s = 0;
            for (int c = 0; c < outs; ++c) s += buf.getSample (c, i);
            out.push_back (s / (float) outs);
        }
    }
    return out;
}

juce::String nn (double midi) { return midi > 0 ? juce::MidiMessage::getMidiNoteName ((int) std::lround (midi), true, true, 4) : juce::String ("-"); }
juce::String f1 (double v) { return juce::String (v, 1); }
} // namespace

int main (int argc, char* argv[])
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    Args args;
    for (int i = 1; i < argc; ++i) args.raw.add (juce::String::fromUTF8 (argv[i]));

    const juce::File pluginFile (args.get ("--plugin"));
    if (! pluginFile.exists()) { usage(); return 2; }

    // ---- host the plugin ----------------------------------------------------
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> types;
    fm.getFormat (0)->findAllTypesForFile (types, pluginFile.getFullPathName());
    if (types.isEmpty()) { std::printf ("no VST3 plugin in %s\n", pluginFile.getFullPathName().toRawUTF8()); return 2; }
    juce::String loadErr;
    auto plugin = fm.createPluginInstance (*types[0], kRenderFs, kBlock, loadErr);
    if (plugin == nullptr) { std::printf ("load failed: %s\n", loadErr.toRawUTF8()); return 2; }
    std::printf ("plugin: %s %s by %s, %s, %d params\n", types[0]->name.toRawUTF8(), types[0]->version.toRawUTF8(),
                 types[0]->manufacturerName.toRawUTF8(), types[0]->isInstrument ? "instrument" : "effect",
                 plugin->getParameters().size());

    if (args.has ("--list-params"))
    {
        for (auto* q : plugin->getParameters())
            std::printf ("  %-32s %s\n", q->getName (64).toRawUTF8(), q->getCurrentValueAsText().toRawUTF8());
        plugin.reset();
        return 0;
    }

    const juce::File audioFile (args.get ("--audio")), out (args.get ("--out"));
    const juce::File tabFile (args.get ("--tab")), midiFile (args.get ("--midi"));
    if (! audioFile.existsAsFile() || out == juce::File() || (! tabFile.existsAsFile() && ! midiFile.existsAsFile())) { usage(); return 2; }
    out.createDirectory();
    if (! types[0]->isInstrument)
        std::printf ("warning: plugin reports itself as an effect; MIDI notes may not sound\n");

    e2e::Gates gates;
    gates.pitchTolSemis = args.get ("--pitch-tol", "1").getDoubleValue();
    gates.onsetTolMs = args.get ("--onset-ms", "50").getDoubleValue();
    gates.durTolMs = gates.onsetTolMs;
    gates.minRate = args.get ("--min-rate", "0.8").getDoubleValue();
    const float velocity = (float) args.get ("--velocity", "0.8").getDoubleValue();

    // ---- 1. decode the recording ------------------------------------------
    juce::String fmt; double srcFs = 0; int srcCh = 0;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto audio = e2e::decodeToMono (audioFile, 22050.0, fmt, srcFs, srcCh);
    if (audio.x.empty()) { std::printf ("could not decode %s\n", audioFile.getFullPathName().toRawUTF8()); return 2; }
    std::printf ("decoded %s: %s, %.0f Hz, %d ch, %.1f s -> mono %.0f Hz\n", audioFile.getFileName().toRawUTF8(), fmt.toRawUTF8(),
                 srcFs, srcCh, audio.x.size() / audio.fs, audio.fs);

    // ---- 2. reference -------------------------------------------------------
    juce::String err, refName;
    std::vector<e2e::TabNote> ref;
    if (tabFile.existsAsFile())
    {
        std::vector<int> open;
        if (args.has ("--tuning"))
        {
            open = e2e::parseTuning (args.get ("--tuning"));
            if (open.empty()) { std::printf ("bad --tuning\n"); return 2; }
            std::reverse (open.begin(), open.end());   // tab lists the highest string first
        }
        ref = e2e::parseTab (tabFile.loadFileAsString(), open, err);
        refName = tabFile.getFullPathName();
    }
    else
    {
        ref = e2e::parseMidiReference (midiFile, err);
        refName = midiFile.getFullPathName();
    }
    if (ref.empty()) { std::printf ("reference: %s\n", err.toRawUTF8()); return 2; }
    std::printf ("reference: %d notes from %s\n", (int) ref.size(), refName.toRawUTF8());

    // ---- 3. learn from both together --------------------------------------
    const auto learned = e2e::learn (audio, ref);
    int used = 0, skipped = 0, corrected = 0;
    double first = 1e9, last = 0;
    for (const auto& n : learned.notes)
    {
        if (n.skipped) { ++skipped; continue; }
        ++used; corrected += n.corrected ? 1 : 0;
        first = std::min (first, n.onset); last = std::max (last, n.onset + n.duration);
    }
    if (used == 0) { std::printf ("learner found no notes\n"); return 1; }
    std::printf ("learned: transpose %+d (runner-up %+d, score %.1f vs %.1f), tuning %+.1f cents, %d notes, %d vetoed, %d corrected, %.2f-%.2f s (%.1f s)\n",
                 learned.transpose, learned.runnerUpTranspose, learned.score, learned.runnerUpScore, learned.tuningCents,
                 used, skipped, corrected, first, last, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);

    // ---- 4. learned.mid ---------------------------------------------------
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0);   // 120 bpm: 1 s = 1920 ticks
        for (const auto& n : learned.notes)
        {
            if (n.skipped) continue;
            seq.addEvent (juce::MidiMessage::noteOn (1, n.midi, velocity), (n.onset - first) * 1920.0);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.midi), (n.onset + n.duration - first) * 1920.0);
        }
        seq.updateMatchedPairs();
        juce::MidiFile mf; mf.setTicksPerQuarterNote (960); mf.addTrack (seq);
        out.getChildFile ("learned.mid").deleteFile();
        juce::FileOutputStream os (out.getChildFile ("learned.mid"));
        mf.writeTo (os);
    }

    // ---- 5. configure and play the plugin ----------------------------------
    plugin->enableAllBuses();
    if (args.has ("--program")) plugin->setCurrentProgram (args.get ("--program").getIntValue());
    juce::StringArray applied;
    for (const auto& kv : args.all ("--param"))
    {
        const auto name = kv.upToFirstOccurrenceOf ("=", false, false), value = kv.fromFirstOccurrenceOf ("=", false, false);
        auto* q = findParam (*plugin, name);
        if (q == nullptr) { std::printf ("no parameter named '%s' (see --list-params)\n", name.toRawUTF8()); return 2; }
        q->setValueNotifyingHost (q->getValueForText (value));
        applied.add (name + " = " + q->getCurrentValueAsText());
        std::printf ("param: %s -> %s\n", name.toRawUTF8(), q->getCurrentValueAsText().toRawUTF8());
    }
    plugin->setNonRealtime (true);
    plugin->prepareToPlay (kRenderFs, kBlock);

    const double lead = 1.0, offset = first - lead;   // render time 0 == source time 'offset'
    std::vector<NoteEvent> ev;
    for (const auto& n : learned.notes)
    {
        if (n.skipped) continue;
        ev.push_back ({ (int64_t) std::llround ((n.onset - offset) * kRenderFs), juce::MidiMessage::noteOn (1, n.midi, velocity) });
        ev.push_back ({ (int64_t) std::llround ((n.onset - offset + n.duration) * kRenderFs), juce::MidiMessage::noteOff (1, n.midi) });
    }
    const auto mono = renderPlugin (*plugin, ev, last - offset + 1.5);
    plugin->releaseResources();
    plugin.reset();

    float renderPeak = 0; for (float s : mono) renderPeak = std::max (renderPeak, std::abs (s));
    if (renderPeak < 1e-4f) { std::printf ("plugin produced silence (peak %.6f)\n", renderPeak); return 1; }

    // ---- 6. verify against the source -------------------------------------
    const auto v = e2e::verify (audio, mono, kRenderFs, offset, learned.notes, gates);

    // ---- 7. artefacts -----------------------------------------------------
    const auto render22 = resampleTo (mono, kRenderFs, audio.fs);
    {
        std::vector<float> L (render22.size()), R (render22.size());
        const int off = (int) (offset * audio.fs);
        for (size_t i = 0; i < render22.size(); ++i)
        {
            const int si = off + (int) i;
            L[i] = si >= 0 && si < (int) audio.x.size() ? 0.8f * audio.x[(size_t) si] : 0.f;
            R[i] = 0.8f * render22[i] / renderPeak;
        }
        e2e::writeWav (out.getChildFile ("learned_render.wav"), R, R, audio.fs);
        e2e::writeWav (out.getChildFile ("ab_source_left_render_right.wav"), L, R, audio.fs);
    }
    e2e::drawPianoRoll (out.getChildFile ("pianoroll.png"), audio, first - 0.3, last + 0.3, learned.notes, v);

    // ---- 8. report ----------------------------------------------------------
    const bool pass = v.pitchRate >= gates.minRate && v.onsetRate >= gates.minRate;
    juce::String md;
    md << "# PluginE2E: learn a real recording, play it through a plugin\n\n"
       << "- Plugin: " << types[0]->name << " " << types[0]->version << " (" << pluginFile.getFullPathName() << ")\n"
       << "- Parameters: " << (applied.isEmpty() ? juce::String ("plugin defaults") : applied.joinIntoString (", ")) << "\n"
       << "- Recording: " << audioFile.getFullPathName() << " (" << fmt << ", " << juce::String (srcFs, 0) << " Hz, " << srcCh << " ch)\n"
       << "- Reference: " << refName << " (" << (int) ref.size() << " notes)\n"
       << "- Learned: transpose " << (learned.transpose >= 0 ? "+" : "") << learned.transpose
       << " (runner-up " << learned.runnerUpTranspose << "), tuning " << juce::String (learned.tuningCents, 1) << " cents, "
       << used << " notes, " << skipped << " vetoed by the audio, " << corrected << " pitch-corrected, recording span "
       << juce::String (first, 2) << "-" << juce::String (last, 2) << " s\n"
       << "- Verdict: **" << (pass ? "PASS" : "FAIL") << "** (gates: pitch within " << gates.pitchTolSemis << " semitone, onset within "
       << gates.onsetTolMs << " ms, " << juce::String (gates.minRate * 100, 0) << " % of notes)\n\n"
       << "## Result\n\n"
       << "| Measure | Value |\n|---|---|\n"
       << "| Pitch within tolerance (render vs source) | " << juce::String (v.pitchRate * 100, 1) << " % |\n"
       << "| Onset within tolerance (render vs source) | " << juce::String (v.onsetRate * 100, 1) << " % |\n"
       << "| Duration within tolerance | " << juce::String (v.durRate * 100, 1) << " % |\n"
       << "| Mean signed onset error (render - source) | " << f1 (v.meanOnsetErrMs) << " ms |\n"
       << "| Mean / max abs onset error | " << f1 (v.meanAbsOnsetErrMs) << " / " << f1 (v.maxOnsetErrMs) << " ms |\n"
       << "| Mean / max abs duration error | " << f1 (v.meanDurErrMs) << " / " << f1 (v.maxDurErrMs) << " ms |\n"
       << "| Octave errors | " << v.octaveErrors << " |\n"
       << "| Source tuning, first / second half | " << f1 (v.sourceCentsFirstHalf) << " / " << f1 (v.sourceCentsSecondHalf) << " cents |\n\n"
       << "## Per note\n\n"
       << "| # | Reference | Learned | Src onset s | Onset err ms | Render pitch | Source pitch | Pitch err st | Dur render/src ms | Pitch | Onset | Dur |\n"
       << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& n : learned.notes)
    {
        const auto& t = ref[(size_t) n.tabIndex];
        juce::String refTxt = (t.stringNo > 0 ? juce::String (t.stringNo) + ":" + juce::String (t.fret) + (t.technique ? juce::String::charToString (t.technique) : juce::String()) + " " : juce::String())
                              + nn (n.tabMidi);
        if (n.skipped) { md << "| " << n.tabIndex + 1 << " | " << refTxt << " | vetoed | | | | | | | | | |\n"; continue; }
        const e2e::VerifyNote* q = nullptr;
        for (const auto& x : v.notes) if (x.tabIndex == n.tabIndex) q = &x;
        if (q == nullptr) continue;
        const double oe = q->renderOnset >= 0 && q->sourceOnset >= 0 ? (q->renderOnset - q->sourceOnset) * 1000 : 1e9;
        md << "| " << n.tabIndex + 1 << " | " << refTxt << " | " << nn (n.midi) << (n.corrected ? " (corr.)" : "")
           << " | " << (q->sourceOnset >= 0 ? juce::String (q->sourceOnset, 3) : juce::String ("none"))
           << " | " << (oe < 1e8 ? f1 (oe) : juce::String ("unmatched"))
           << " | " << nn (q->renderPitch) << " " << juce::String (q->renderPitch, 2)
           << " | " << nn (q->sourcePitch) << " " << juce::String (q->sourcePitch, 2)
           << " | " << juce::String (q->renderPitch - q->sourcePitch, 2)
           << " | " << juce::String (q->renderDur * 1000, 0) << "/" << juce::String (q->sourceDur * 1000, 0)
           << " | " << (q->pitchOk ? "ok" : "FAIL") << " | " << (q->onsetOk ? "ok" : "FAIL") << " | " << (q->durOk ? "ok" : "off") << " |\n";
    }
    out.getChildFile ("report.md").replaceWithText (md);

    std::printf ("verify: pitch %.1f %%, onset %.1f %%, duration %.1f %%; onset err mean %+.1f ms, mean abs %.1f, max %.1f; octave errors %d\n",
                 v.pitchRate * 100, v.onsetRate * 100, v.durRate * 100, v.meanOnsetErrMs, v.meanAbsOnsetErrMs, v.maxOnsetErrMs, v.octaveErrors);
    std::printf ("%s  (report: %s)\n", pass ? "PASS" : "FAIL", out.getChildFile ("report.md").getFullPathName().toRawUTF8());
    return pass ? 0 : 1;
}

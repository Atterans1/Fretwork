// CpuProbe: worst-case real-time CPU sweep for the Fretwork engine.
//
// Each row drives FretworkAudioProcessor::processBlock with one fixed load: a
// dense strum of all six strings every 125 ms (alternating down and up
// strokes, four rotating chord shapes, 3 ms between strings, each note held
// 110 ms). Strings keep ringing after note-off, so every row runs with all six
// strings sounding for the whole timed run.
//
// Per row: cpu_pct = time inside processBlock / audio time produced, percent of
// real time. Blocks run back to back, unpaced, so loop wall time is never the
// denominator. Also p50/p99/max block time as percent of that block's real-time
// budget, and the number of blocks over budget (overruns).
//
// Matrix (one CSV row each):
//   sr     44100, 48000, 96000
//   block  64, 128, 256, 512
//   body   Dreadnought Steel, Classical Nylon, Solid Electric, Semi-Hollow
//   mic    Mic Blend 0, 70 (default), 100 %
//   room   Room Level 20 % (default), 100 % (maximum), type fixed at Room
//   amp    Off, or Lead at gain 100 % through a 4x12 cab (AmpFx 4x oversampling)
// Fixed: guitar type 6-String, Poly mode, Standard tuning, default pickups,
// strings, tone, drive, width, output; chorus and delay off.
//
// Extras (not in the matrix median): one-variable changes applied to the
// worst matrix row: idle, the other guitar types, Strum and Arp mode, chorus
// and delay at max, room off, every room type at 100 %, the other amp models.
//
// Usage: CpuProbe [--quick | --full] [--csv path] [--seconds S] [--warm S]
//                 [--budget S] [--no-extras] [--axis name=v1,v2,...]...
//   --quick  reduced matrix, 1 s of audio per row, 28 s wall budget (default)
//   --full   complete matrix, 120 s of audio per row, no budget
//   --axis   override one axis: sr block body guitar mic room amp
// Exit: 0 ok; 1 a row over 100 % CPU or silent/non-finite; 2 bad arguments;
//       3 wall budget hit before the matrix finished.
#include "PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

const char* const kBodyNames[]   = { "Dreadnought Steel", "Classical Nylon", "Solid Electric", "Semi-Hollow" };
const char* const kGuitarNames[] = { "6-String", "12-String", "Baritone", "Nylon 12-String", "7-String" };

struct Cfg
{
    int sr = 48000;
    int block = 512;
    int body = 0;
    int guitar = 0;
    int mic = 70;
    int room = 20;          // Room Level, percent
    int amp = 0;            // 0 Off, 1 Lead at gain 100 %, 4x12
    bool idle = false;
    std::string tag = "matrix";
    std::vector<std::pair<const char*, double>> extra;   // applied last
};

struct Row
{
    int seq = 0;
    Cfg cfg;
    double secs = 0, cpu = 0, p50 = 0, p99 = 0, mx = 0, rms = 0, wall = 0;
    long long overruns = 0;
    bool audioOk = false;
};

struct Axes
{
    std::vector<int> sr, block, body, guitar, mic, room, amp;
};

void setParam (FretworkAudioProcessor& p, const char* id, double value)
{
    auto* prm = p.getState().getParameter (id);
    if (prm == nullptr)
    {
        std::fprintf (stderr, "CpuProbe: unknown parameter %s\n", id);
        std::exit (2);
    }
    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) value));
}

void apply (FretworkAudioProcessor& p, const Cfg& c)
{
    namespace id = fw::id;
    setParam (p, id::guitarType, c.guitar);
    setParam (p, id::body, c.body);
    setParam (p, id::micBlend, c.mic);
    setParam (p, id::roomType, 1);              // Room (default)
    setParam (p, id::room, c.room);
    if (c.amp)
    {
        setParam (p, id::ampModel, 3);          // Lead
        setParam (p, id::ampGain, 100);
        setParam (p, id::cab, 3);               // 4x12
    }
    for (const auto& kv : c.extra)
        setParam (p, kv.first, kv.second);
}

struct Ev { long long pos; int note; float vel; bool on; };

// Written pitches (the processor adds fw::kWrittenOctave on the MIDI side).
std::vector<Ev> pattern (const Cfg& c, double totalSecs)
{
    std::vector<Ev> e;
    if (c.idle) return e;

    static const int shapes[4][6] = {
        { 40, 47, 52, 55, 59, 64 },   // Em
        { 43, 47, 50, 55, 59, 67 },   // G
        { 40, 48, 52, 55, 60, 64 },   // C/E
        { 42, 45, 50, 57, 62, 66 },   // D/F#
    };
    const auto at = [&c] (double t) { return (long long) (t * c.sr); };

    int k = 0;
    for (double t = 0.02; t < totalSecs - 0.2; t += 0.125, ++k)
    {
        const bool down = (k % 2) == 0;
        const float vel = down ? 1.0f : 0.8f;
        for (int j = 0; j < 6; ++j)
        {
            const int note = shapes[k % 4][down ? j : 5 - j];
            const double on = t + 0.003 * j;
            e.push_back ({ at (on),        note, vel, true });
            e.push_back ({ at (on + 0.11), note, 0.f, false });
        }
    }
    std::stable_sort (e.begin(), e.end(), [] (const Ev& a, const Ev& b)
                      { return a.pos != b.pos ? a.pos < b.pos : (! a.on && b.on); });
    return e;
}

Row runCfg (const Cfg& c, double secs, double warmSecs)
{
    // Heap, not stack: each string holds ~1 MB of delay lines, and a stack
    // instance overflows the default 1 MB stack (SoundJudge does the same).
    auto proc = std::make_unique<FretworkAudioProcessor>();
    proc->prepareToPlay ((double) c.sr, c.block);
    apply (*proc, c);

    const long long warmBlocks = (long long) std::ceil (warmSecs * c.sr / c.block);
    const long long runBlocks  = (long long) std::ceil (secs * c.sr / c.block);
    const long long total      = warmBlocks + runBlocks;
    const auto evs = pattern (c, (double) total * c.block / c.sr);

    juce::AudioBuffer<float> buf (2, c.block);
    juce::MidiBuffer midi;
    std::vector<float> us;
    us.reserve ((size_t) runBlocks);

    const double budgetNs = (double) c.block * 1.0e9 / c.sr;
    double sumNs = 0.0, sumSq = 0.0;
    long long overruns = 0;
    size_t ei = 0;

    const auto w0 = Clock::now();
    for (long long b = 0; b < total; ++b)
    {
        const long long start = b * c.block, end = start + c.block;
        buf.clear();
        midi.clear();
        for (; ei < evs.size() && evs[ei].pos < end; ++ei)
        {
            const auto& e = evs[ei];
            midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note + fw::kWrittenOctave, e.vel)
                                : juce::MidiMessage::noteOff (1, e.note + fw::kWrittenOctave),
                           (int) (e.pos - start));
        }

        const auto p0 = Clock::now();
        proc->processBlock (buf, midi);
        const auto p1 = Clock::now();

        if (b < warmBlocks) continue;
        const double ns = (double) std::chrono::duration_cast<std::chrono::nanoseconds> (p1 - p0).count();
        sumNs += ns;
        us.push_back ((float) (ns * 1.0e-3));
        if (ns > budgetNs) ++overruns;

        const float* l = buf.getReadPointer (0);
        const float* r = buf.getReadPointer (1);
        for (int i = 0; i < c.block; ++i)
            sumSq += (double) l[i] * l[i] + (double) r[i] * r[i];
    }
    const auto w1 = Clock::now();

    std::sort (us.begin(), us.end());
    const auto q = [&us] (double f) { return (double) us[(size_t) (f * (double) (us.size() - 1))]; };
    const double audioNs  = (double) runBlocks * c.block * 1.0e9 / c.sr;
    const double budgetUs = budgetNs * 1.0e-3;

    Row r;
    r.cfg = c;
    r.secs = audioNs * 1.0e-9;
    r.cpu = 100.0 * sumNs / audioNs;
    r.p50 = 100.0 * q (0.50) / budgetUs;
    r.p99 = 100.0 * q (0.99) / budgetUs;
    r.mx  = 100.0 * us.back() / budgetUs;
    r.overruns = overruns;
    r.rms = std::sqrt (sumSq / (2.0 * (double) runBlocks * c.block));
    r.audioOk = std::isfinite (sumSq) && (c.idle || r.rms > 1.0e-4);
    r.wall = std::chrono::duration<double> (w1 - w0).count();
    return r;
}

std::string extraNote (const Cfg& c)
{
    std::string s = c.idle ? "idle" : "";
    for (const auto& kv : c.extra)
    {
        if (! s.empty()) s += ";";
        char t[64];
        std::snprintf (t, sizeof t, "%s=%g", kv.first, kv.second);
        s += t;
    }
    return s.empty() ? "-" : s;
}

const char* const kHeader = "seq,tag,sr,block,body,guitar,mic,room_pct,amp,os_factor,secs,"
                            "cpu_pct,p50_pct,p99_pct,max_pct,overruns,rms,audio_ok,wall_s,"
                            "body_name,guitar_name,note";

void printRow (std::FILE* f, const Row& r)
{
    const Cfg& c = r.cfg;
    std::fprintf (f, "%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.3f,%.3f,%.3f,%.3f,%lld,%.5f,%d,%.2f,%s,%s,%s\n",
                  r.seq, c.tag.c_str(), c.sr, c.block, c.body, c.guitar, c.mic, c.room, c.amp,
                  c.amp ? 4 : 1, r.secs, r.cpu, r.p50, r.p99, r.mx, r.overruns, r.rms, r.audioOk ? 1 : 0,
                  r.wall, kBodyNames[c.body], kGuitarNames[c.guitar], extraNote (c).c_str());
}

void describe (const char* label, const Row& r)
{
    const Cfg& c = r.cfg;
    std::printf ("%s %.3f%% CPU (seq %d, tag %s): sr %d, block %d, body %s, guitar %s, mic %d, "
                 "room %d%%, amp %s, note %s\n",
                 label, r.cpu, r.seq, c.tag.c_str(), c.sr, c.block, kBodyNames[c.body], kGuitarNames[c.guitar],
                 c.mic, c.room, c.amp ? "Lead 4x12 (4x OS)" : "Off", extraNote (c).c_str());
}

std::vector<int> parseList (const char* s)
{
    std::vector<int> v;
    for (const char* p = s; *p != 0;)
    {
        char* end = nullptr;
        v.push_back ((int) std::strtol (p, &end, 10));
        if (end == p) { v.clear(); break; }
        p = *end == ',' ? end + 1 : end;
    }
    return v;
}

std::vector<Cfg> extrasFor (const Cfg& base, const Axes& ax)
{
    std::vector<Cfg> v;
    const auto add = [&base, &v] (const char* tag, auto&& edit)
    {
        Cfg c = base;
        c.tag = tag;
        c.extra.clear();
        edit (c);
        v.push_back (c);
    };
    namespace id = fw::id;

    add ("x_idle", [] (Cfg& c) { c.idle = true; });
    for (int g = 0; g < 5; ++g)
        if (std::find (ax.guitar.begin(), ax.guitar.end(), g) == ax.guitar.end())
            add ("x_guitar", [g] (Cfg& c) { c.guitar = g; });
    add ("x_mode_strum", [] (Cfg& c) { c.extra = { { id::mode, 1 } }; });
    add ("x_mode_arp",   [] (Cfg& c) { c.extra = { { id::mode, 2 } }; });
    add ("x_fx_max", [] (Cfg& c)
         { c.extra = { { id::chorusMix, 100 }, { id::delayMix, 100 }, { id::delayFeedback, 90 } }; });
    add ("x_room_off", [] (Cfg& c) { c.extra = { { id::room, 0 } }; });
    for (int t = 0; t < 4; ++t)
        if (t != 1)   // Room is the matrix's type
            add ("x_room_type", [t] (Cfg& c) { c.extra = { { id::roomType, (double) t }, { id::room, 100 } }; });
    for (int m = 1; m <= 2; ++m)
        add ("x_amp_model", [m] (Cfg& c)
             { c.extra = { { id::ampModel, (double) m }, { id::ampGain, 100 }, { id::cab, 3 } }; });
    return v;
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    bool full = false, extras = true;
    double secs = -1, warm = 0.25, budget = -1;
    const char* csvPath = nullptr;
    std::vector<std::pair<std::string, std::vector<int>>> overrides;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const bool hasNext = i + 1 < argc;
        if (a == "--quick") full = false;
        else if (a == "--full") full = true;
        else if (a == "--no-extras") extras = false;
        else if (a == "--csv" && hasNext) csvPath = argv[++i];
        else if (a == "--seconds" && hasNext) secs = std::atof (argv[++i]);
        else if (a == "--warm" && hasNext) warm = std::atof (argv[++i]);
        else if (a == "--budget" && hasNext) budget = std::atof (argv[++i]);
        else if (a == "--axis" && hasNext)
        {
            const std::string kv = argv[++i];
            const auto eq = kv.find ('=');
            const auto vals = eq == std::string::npos ? std::vector<int>() : parseList (kv.c_str() + eq + 1);
            if (vals.empty()) { std::fprintf (stderr, "CpuProbe: bad --axis %s\n", kv.c_str()); return 2; }
            overrides.push_back ({ kv.substr (0, eq), vals });
        }
        else
        {
            std::fprintf (stderr, "Usage: CpuProbe [--quick | --full] [--csv path] [--seconds S] [--warm S] "
                                  "[--budget S] [--no-extras] [--axis name=v1,v2,...]...\n"
                                  "axes: sr block body guitar mic room amp\n");
            return 2;
        }
    }

    Axes ax;
    if (full)
        ax = { { 44100, 48000, 96000 }, { 64, 128, 256, 512 }, { 0, 1, 2, 3 }, { 0 }, { 0, 70, 100 }, { 20, 100 }, { 0, 1 } };
    else
        ax = { { 44100, 96000 }, { 64, 512 }, { 0, 1, 2, 3 }, { 0 }, { 0, 100 }, { 20, 100 }, { 0, 1 } };
    if (secs < 0) secs = full ? 120.0 : 1.0;
    if (budget < 0) budget = full ? 0.0 : 28.0;

    for (const auto& o : overrides)
    {
        std::vector<int>* t = o.first == "sr" ? &ax.sr : o.first == "block" ? &ax.block
                            : o.first == "body" ? &ax.body : o.first == "guitar" ? &ax.guitar
                            : o.first == "mic" ? &ax.mic : o.first == "room" ? &ax.room
                            : o.first == "amp" ? &ax.amp : nullptr;
        if (t == nullptr) { std::fprintf (stderr, "CpuProbe: unknown axis %s\n", o.first.c_str()); return 2; }
        *t = o.second;
    }
    for (int b : ax.body)   if (b < 0 || b > 3) { std::fprintf (stderr, "CpuProbe: body 0..3\n"); return 2; }
    for (int g : ax.guitar) if (g < 0 || g > 4) { std::fprintf (stderr, "CpuProbe: guitar 0..4\n"); return 2; }

    std::vector<Cfg> cfgs;
    for (int sr : ax.sr) for (int bl : ax.block) for (int bo : ax.body) for (int gt : ax.guitar)
    for (int mic : ax.mic) for (int rm : ax.room) for (int am : ax.amp)
    {
        Cfg c;
        c.sr = sr; c.block = bl; c.body = bo; c.guitar = gt; c.mic = mic; c.room = rm; c.amp = am;
        cfgs.push_back (c);
    }
    std::mt19937 rng (20261115u);
    std::shuffle (cfgs.begin(), cfgs.end(), rng);

    std::FILE* csv = nullptr;
    if (csvPath != nullptr)
    {
        csv = std::fopen (csvPath, "w");
        if (csv == nullptr) { std::fprintf (stderr, "CpuProbe: cannot write %s\n", csvPath); return 2; }
        std::fprintf (csv, "%s\n", kHeader);
    }
    std::printf ("%s\n", kHeader);
    std::printf ("# mode %s, %zu matrix rows, %.2f s audio per row (+%.2f s untimed warm-up)\n",
                 full ? "full" : "quick", cfgs.size(), secs, warm);

    std::vector<Row> rows;
    const auto t0 = Clock::now();
    bool truncated = false;
    int seq = 0;
    const auto emit = [&] (const Cfg& c)
    {
        Row r = runCfg (c, secs, warm);
        r.seq = ++seq;
        printRow (stdout, r);
        if (csv != nullptr) { printRow (csv, r); std::fflush (csv); }
        rows.push_back (r);
    };

    for (const auto& c : cfgs)
    {
        if (budget > 0 && std::chrono::duration<double> (Clock::now() - t0).count() > budget)
        {
            truncated = true;
            break;
        }
        emit (c);
    }

    if (extras && ! truncated && ! rows.empty())
    {
        const auto worst = std::max_element (rows.begin(), rows.end(),
                                             [] (const Row& a, const Row& b) { return a.cpu < b.cpu; });
        const Cfg base = worst->cfg;
        for (const auto& c : extrasFor (base, ax))
        {
            if (budget > 0 && std::chrono::duration<double> (Clock::now() - t0).count() > budget)
            {
                truncated = true;
                break;
            }
            emit (c);
        }
    }
    if (csv != nullptr) std::fclose (csv);

    std::vector<double> matrixCpu;
    const Row* worstMatrix = nullptr;
    const Row* worstAll = nullptr;
    bool bad = false;
    for (const auto& r : rows)
    {
        if (r.cfg.tag == "matrix")
        {
            matrixCpu.push_back (r.cpu);
            if (worstMatrix == nullptr || r.cpu > worstMatrix->cpu) worstMatrix = &r;
        }
        if (worstAll == nullptr || r.cpu > worstAll->cpu) worstAll = &r;
        bad = bad || r.cpu > 100.0 || ! r.audioOk;
    }
    if (worstMatrix != nullptr)
    {
        std::sort (matrixCpu.begin(), matrixCpu.end());
        const size_t n = matrixCpu.size();
        const double med = n % 2 ? matrixCpu[n / 2] : 0.5 * (matrixCpu[n / 2 - 1] + matrixCpu[n / 2]);
        describe ("WORST_MATRIX", *worstMatrix);
        describe ("WORST_ALL", *worstAll);
        std::printf ("MEDIAN_MATRIX %.3f%% CPU over %zu rows\n", med, n);
    }
    std::printf ("ROWS %zu, wall %.1f s%s%s\n", rows.size(),
                 std::chrono::duration<double> (Clock::now() - t0).count(),
                 truncated ? ", TRUNCATED by --budget" : "",
                 bad ? ", FAIL (row over 100% CPU or silent/non-finite)" : "");
    return truncated ? 3 : bad ? 1 : 0;
}

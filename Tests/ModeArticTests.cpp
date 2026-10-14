// Every articulation in every play mode, from the engine's own events.
//
// The synthesis of each articulation is mode-independent (and proven by audio
// in Poly by ModeMatrix artic-vst3). What differs per mode is whether the
// engine EMITS it when a player does the real-guitar thing. Expectations are
// fixed here; a cell prints PASS/FAIL so the run doubles as the matrix.
#include "Harness.h"
#include "engine/PerformanceEngine.h"
#include "SeqStore.h"

#include <functional>
#include <string>
#include <vector>

namespace
{
constexpr double kFs = 48000.0;
constexpr int    kBlock = 480;

struct Ev { double t; fw::StringEvent e; };
struct In { double t; unsigned char status, d1, d2; };

const char* modeName (fw::PlayMode m)
{
    return m == fw::PlayMode::Poly ? "Poly" : m == fw::PlayMode::Strum ? "Strum" : m == fw::PlayMode::Arp ? "Arp" : "Seq";
}

// Sounding pitch -> the written MIDI note a keyboard sends.
unsigned char w (int sounding) { return (unsigned char) (sounding + fw::kWrittenOctave); }

std::vector<Ev> run (fw::PlayMode mode, std::vector<In> script, double seconds)
{
    static fw::SeqPattern slots[fw::kNumSeqSlots];
    {
        fw::SeqStore store; store.refresh();
        for (int s = 0; s < fw::kNumSeqSlots; ++s) slots[s] = store.snapshot (s);
    }
    fw::PerformanceEngine perf;
    perf.prepare (kFs);
    fw::PerfParams p {};
    p.mode = mode; p.seqSlots = slots; p.seqSlot = 0; p.bpm = 100.f; p.strumTime = 20.f;
    perf.setParams (p);

    std::vector<Ev> out;
    fw::StringEvent ev[512]; fw::ClickEvent ck[8]; int nc = 0;
    const int blocks = (int) (seconds * kFs / kBlock);
    size_t k = 0;
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<fw::MidiMsg> in;
        while (k < script.size() && script[k].t * kFs < (b + 1) * kBlock)
        {
            const int off = std::max (0, (int) (script[k].t * kFs) - b * kBlock);
            in.push_back ({ off, script[k].status, script[k].d1, script[k].d2 });
            ++k;
        }
        const int n = perf.process (in.empty() ? nullptr : in.data(), (int) in.size(), fw::Transport {}, kBlock, ev, 512, ck, nc);
        for (int i = 0; i < n; ++i) out.push_back ({ (b * kBlock + ev[i].sampleOffset) / kFs, ev[i] });
    }
    return out;
}

std::vector<In> chord (double t, std::initializer_list<int> sounding, int vel)
{
    std::vector<In> v;
    double dt = 0;
    for (int n : sounding) { v.push_back ({ t + dt, 0x90, w (n), (unsigned char) vel }); dt += 0.004; }
    return v;
}
void add (std::vector<In>& a, const std::vector<In>& b) { a.insert (a.end(), b.begin(), b.end()); }
void sortIn (std::vector<In>& v) { std::stable_sort (v.begin(), v.end(), [] (const In& a, const In& b) { return a.t < b.t; }); }

std::vector<Ev> plucks (const std::vector<Ev>& e, double t0, double t1)
{
    std::vector<Ev> r;
    for (auto& x : e) if (x.e.type == fw::EventType::Pluck && x.t >= t0 && x.t < t1) r.push_back (x);
    return r;
}

const fw::PlayMode kModes[] = { fw::PlayMode::Poly, fw::PlayMode::Strum, fw::PlayMode::Arp, fw::PlayMode::Seq };

void cell (const char* artic, fw::PlayMode m, bool pass, const std::string& why)
{
    std::printf ("    %-14s %-6s %s  %s\n", artic, modeName (m), pass ? "PASS" : "FAIL", why.c_str());
    char msg[96];
    std::snprintf (msg, sizeof msg, "%s in %s: %s", artic, modeName (m), why.c_str());
    EXPECT_MSG (pass, msg);
}

// A sounding chord in every mode: C major, open shape.
const std::initializer_list<int> kC = { 48, 52, 55, 60, 64 };
} // namespace

FW_TEST ("mode matrix: strum down and up in every mode")
{
    for (auto m : kModes)
        for (int up = 0; up < 2; ++up)
        {
            std::vector<In> s = chord (0.05, kC, 100);
            s.push_back ({ 0.6, 0x90, (unsigned char) (up ? 29 : 28), 100 });
            s.push_back ({ 0.7, 0x80, (unsigned char) (up ? 29 : 28), 0 });
            const auto e = run (m, s, 1.2);
            // Arp and Seq keep playing their own notes: the stroke is what the
            // key adds, found by diffing against the same run without the key.
            const auto base = run (m, chord (0.05, kC, 100), 1.2);
            std::vector<Ev> h;
            for (auto& x : plucks (e, 0.6, 0.66))
            {
                bool inBase = false;
                for (auto& y : plucks (base, 0.6, 0.66))
                    inBase = inBase || (std::fabs (y.t - x.t) < 1e-6 && y.e.string == x.e.string);
                if (! inBase) h.push_back (x);
            }
            bool ordered = h.size() >= 3, noise = false;
            for (size_t i = 1; i < h.size(); ++i)
                ordered = ordered && (up ? h[i].e.string < h[i - 1].e.string : h[i].e.string > h[i - 1].e.string);
            for (auto& x : e) if (x.e.type == fw::EventType::StrokeNoise && x.t >= 0.59 && x.t < 0.65 && x.e.dir == (up ? -1 : 1)) noise = true;
            cell (up ? "Strum up" : "Strum down", m, ordered && noise,
                  std::to_string (h.size()) + " strings " + (ordered ? "in stroke order" : "NOT in order") + (noise ? ", pick scrape" : ", no scrape"));
        }
}

FW_TEST ("mode matrix: palm mute and dead notes by velocity in every mode")
{
    for (auto m : kModes)
        for (int dead = 0; dead < 2; ++dead)
        {
            const int vel = dead ? 12 : 35;
            const auto e = run (m, chord (0.05, kC, vel), 2.5);
            const auto h = plucks (e, 0.0, 2.5);
            int ok = 0;
            for (auto& x : h) ok += x.e.art.damp == (dead ? fw::Damping::Dead : fw::Damping::PalmMute);
            cell (dead ? "Dead note" : "Palm mute", m, ! h.empty() && ok == (int) h.size(),
                  std::to_string (ok) + "/" + std::to_string (h.size()) + " hits muted (velocity " + std::to_string (vel) + ")");
        }
}

FW_TEST ("mode matrix: hammer-on, pull-off and slide on a ringing string in every mode")
{
    for (auto m : kModes)
    {
        // Hold A3; after it rings, add B3 (+2: hammer-on), release B3 (pull-off back to A3).
        std::vector<In> s = { { 0.05, 0x90, w (57), 90 }, { 1.0, 0x90, w (59), 90 }, { 1.6, 0x80, w (59), 0 } };
        const auto e = run (m, s, 2.2);
        int hammer = -1, pull = -1;
        for (auto& x : e)
            if (x.e.type == fw::EventType::Legato)
            {
                if (x.t >= 1.0 && x.t < 1.05 && std::lround (x.e.pitch) == 59 && x.e.pitchFrom < 0) hammer = x.e.string;
                if (x.t >= 1.6 && x.t < 1.65 && std::lround (x.e.pitch) == 57) pull = x.e.string;
            }
        cell ("Hammer-on", m, hammer >= 0, hammer >= 0 ? "legato to B3 on string " + std::to_string (hammer) + ", no pick" : "no legato");
        cell ("Pull-off", m, pull >= 0 && pull == hammer, pull >= 0 ? "legato back to A3 on the same string" : "no pull-off");

        // Slide: hold E3, add A3 (+5) -> gliding legato from E3.
        std::vector<In> sl = { { 0.05, 0x90, w (52), 90 }, { 1.0, 0x90, w (57), 90 }, { 1.1, 0x80, w (52), 0 } };
        const auto f = run (m, sl, 1.6);
        bool glide = false, squeak = false;
        for (auto& x : f)
        {
            if (x.e.type == fw::EventType::Legato && x.t >= 1.0 && x.t < 1.05 && std::lround (x.e.pitch) == 57 && std::lround (x.e.pitchFrom) == 52) glide = true;
            if (x.e.type == fw::EventType::Squeak && x.t >= 1.0 && x.t < 1.05) squeak = true;
        }
        cell ("Slide", m, glide, glide ? std::string ("glide E3 -> A3") + (squeak ? " with squeak" : "") : "no glide");
    }
}

FW_TEST ("mode matrix: natural harmonics at full velocity in every mode")
{
    for (auto m : kModes)
    {
        // E3 at velocity 127: 12th-fret harmonic of the low E string.
        const auto e = run (m, { { 0.05, 0x90, w (52), 127 } }, 2.5);
        const auto h = plucks (e, 0.0, 2.5);
        int harm = 0;
        for (auto& x : h) harm += x.e.harmonicN == 2 && x.e.string == 0;
        cell ("Harmonic", m, ! h.empty() && harm == (int) h.size(),
              std::to_string (harm) + "/" + std::to_string (h.size()) + " hits are the 12th-fret harmonic on low E");
    }
}

FW_TEST ("mode matrix: automatic vibrato on a held note in every mode")
{
    for (auto m : kModes)
    {
        fw::SeqPattern none[fw::kNumSeqSlots];
        (void) none;
        fw::PerformanceEngine perf; perf.prepare (kFs);
        fw::SeqStore store; store.refresh();
        static fw::SeqPattern slots[fw::kNumSeqSlots];
        for (int s = 0; s < fw::kNumSeqSlots; ++s) slots[s] = store.snapshot (s);
        fw::PerfParams p {}; p.mode = m; p.seqSlots = slots; perf.setParams (p);
        fw::StringEvent ev[256]; fw::ClickEvent ck[8]; int nc = 0;
        float early = 0, late = 0;
        for (int b = 0; b < 200; ++b)
        {
            std::vector<fw::MidiMsg> in;
            if (b == 1) in.push_back ({ 0, 0x90, w (57), 90 });
            perf.process (in.empty() ? nullptr : in.data(), (int) in.size(), fw::Transport {}, kBlock, ev, 256, ck, nc);
            if (b == 40) early = perf.ui().modWheel;    // 0.4 s
            if (b == 150) late = perf.ui().modWheel;    // 1.5 s
        }
        char why[64]; std::snprintf (why, sizeof why, "depth %.2f at 0.4 s, %.2f at 1.5 s", early, late);
        cell ("Vibrato", m, early < 0.05f && late > 0.3f, why);
    }
}

FW_TEST ("mode matrix: fret noise when the chord is let go, in every mode")
{
    for (auto m : kModes)
    {
        std::vector<In> s = chord (0.05, kC, 100);
        for (int n : kC) s.push_back ({ 1.2, 0x80, w (n), 0 });
        sortIn (s);
        const auto e = run (m, s, 1.8);
        int rel = 0;
        for (auto& x : e) if (x.e.type == fw::EventType::Release && x.t >= 1.2 && x.t < 1.25) ++rel;
        cell ("Fret noise", m, rel >= 3, std::to_string (rel) + " strings released (finger lift) at let-go");
    }
}

FW_TEST ("mode matrix: string squeak on a fast position shift, in every mode")
{
    for (auto m : kModes)
    {
        // Open C (mean fret 1.2), then 0.25 s after letting go, the double-stop
        // E5 + B5. Auto voicing plays intervals as held, and these two notes can
        // only be fretted around the 17th-19th frets: a forced jump. (Three-note
        // chords are re-voiced by name with voice leading, so they rarely jump.)
        std::vector<In> s = chord (0.05, kC, 100);
        for (int n : kC) s.push_back ({ 1.0, 0x80, w (n), 0 });
        add (s, chord (1.25, { 76, 83 }, 100));
        sortIn (s);
        const auto e = run (m, s, 2.0);
        bool squeak = false;
        for (auto& x : e) if (x.e.type == fw::EventType::Squeak && x.t >= 1.2 && x.t < 1.4) squeak = true;
        cell ("String squeak", m, squeak, squeak ? "squeak on the shift" : "no squeak");
    }
}

FW_TEST ("mode matrix: pick attack follows velocity in every mode")
{
    for (auto m : kModes)
    {
        const auto soft = plucks (run (m, chord (0.05, kC, 60), 2.5), 0, 2.5);
        const auto hard = plucks (run (m, chord (0.05, kC, 120), 2.5), 0, 2.5);
        double vs = 0, vh = 0;
        for (auto& x : soft) vs += x.e.velocity;
        for (auto& x : hard) vh += x.e.velocity;
        if (! soft.empty()) vs /= (double) soft.size();
        if (! hard.empty()) vh /= (double) hard.size();
        char why[80]; std::snprintf (why, sizeof why, "mean hit velocity %.2f (key 60) vs %.2f (key 120)", vs, vh);
        // Seq takes its dynamics from the pattern (accents), as a drum machine does.
        const bool pass = m == fw::PlayMode::Seq ? (! soft.empty() && ! hard.empty()) : vh > vs + 0.15;
        cell ("Pick attack", m, pass, why);
    }
}

FW_TEST ("mode matrix: bend follows the wheel in every mode")
{
    for (auto m : kModes)
    {
        fw::PerformanceEngine perf; perf.prepare (kFs);
        fw::PerfParams p {}; p.mode = m; perf.setParams (p);
        fw::StringEvent ev[64]; fw::ClickEvent ck[8]; int nc = 0;
        std::vector<fw::MidiMsg> in { { 0, 0x90, w (57), 90 }, { 10, 0xE0, 0x7F, 0x7F } };
        perf.process (in.data(), 2, fw::Transport {}, kBlock, ev, 64, ck, nc);
        const float b = perf.ui().pitchBendSemis;
        char why[48]; std::snprintf (why, sizeof why, "wheel up bends %+.2f semitones", b);
        cell ("Bend", m, std::fabs (b - 2.f) < 0.05f, why);
    }
}

FW_TEST ("mode matrix: a hammered-over note is not lost after the pedal lets go")
{
    for (auto m : { fw::PlayMode::Strum, fw::PlayMode::Arp })
    {
        // Hold A3, hammer B3 (A3 is now covered), pedal down, lift both keys,
        // pedal up (drops both), then play an A minor chord containing A3.
        std::vector<In> s = { { 0.05, 0x90, w (57), 90 }, { 1.0, 0x90, w (59), 90 }, { 1.2, 0xB0, 64, 127 },
                              { 1.4, 0x80, w (57), 0 }, { 1.4, 0x80, w (59), 0 }, { 1.6, 0xB0, 64, 0 } };
        add (s, chord (2.0, { 57, 60, 64 }, 100));
        sortIn (s);
        const auto e = run (m, s, 3.0);
        bool a3 = false;
        for (auto& x : e) if (x.t >= 2.0 && x.e.type == fw::EventType::Pluck && std::lround (x.e.pitch) == 57) a3 = true;
        cell ("Pedal clean-up", m, a3, a3 ? "A3 sounds in the next chord" : "A3 stayed covered and is silent");
    }
}

// Sends a timed note sequence to a MIDI output whose name contains argv[1].
// usage: midisend <portSubstring> then lines on stdin: <ms> on|off|cc <a> <b>
//   on/off: a = note, b = velocity; cc: a = controller number, b = value
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#pragma comment(lib, "winmm.lib")
struct Ev { DWORD ms; int kind; int note; int vel; };   // kind: 0 off, 1 on, 2 cc
int main (int argc, char** argv)
{
    const UINT n = midiOutGetNumDevs();
    int pick = -1;
    for (UINT i = 0; i < n; ++i)
    {
        MIDIOUTCAPSA c {}; midiOutGetDevCapsA (i, &c, sizeof c);
        std::printf ("out %u: %s\n", i, c.szPname);
        if (argc > 1 && std::strstr (c.szPname, argv[1])) pick = (int) i;
    }
    if (argc < 2) return 0;
    if (pick < 0) { std::printf ("NOPORT\n"); return 3; }
    std::vector<Ev> evs; char kind[8]; Ev e {};
    while (std::scanf ("%lu %7s %d %d", &e.ms, kind, &e.note, &e.vel) == 4)
    {
        e.kind = std::strcmp (kind, "on") == 0 ? 1 : (std::strcmp (kind, "cc") == 0 ? 2 : 0);
        evs.push_back (e);
    }
    HMIDIOUT h {}; if (midiOutOpen (&h, (UINT) pick, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { std::printf ("OPENFAIL\n"); return 4; }
    timeBeginPeriod (1);
    const DWORD t0 = timeGetTime();
    for (const auto& ev : evs)
    {
        while (timeGetTime() - t0 < ev.ms) Sleep (1);
        const DWORD status = ev.kind == 2 ? 0xB0u : (ev.kind == 1 ? 0x90u : 0x80u);
        const DWORD msg = status | ((DWORD) ev.note << 8) | ((DWORD) (ev.kind == 0 ? 0 : ev.vel) << 16);
        midiOutShortMsg (h, msg);
    }
    timeEndPeriod (1);
    midiOutClose (h);
    std::printf ("SENT %zu\n", evs.size());
    return 0;
}

// Sends a timed note sequence to a MIDI output whose name contains argv[1].
// usage: midisend <portSubstring> then lines on stdin: <ms> on|off <note> <vel>
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#pragma comment(lib, "winmm.lib")
struct Ev { DWORD ms; bool on; int note; int vel; };
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
    while (std::scanf ("%lu %7s %d %d", &e.ms, kind, &e.note, &e.vel) == 4) { e.on = std::strcmp (kind, "on") == 0; evs.push_back (e); }
    HMIDIOUT h {}; if (midiOutOpen (&h, (UINT) pick, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { std::printf ("OPENFAIL\n"); return 4; }
    timeBeginPeriod (1);
    const DWORD t0 = timeGetTime();
    for (const auto& ev : evs)
    {
        while (timeGetTime() - t0 < ev.ms) Sleep (1);
        const DWORD msg = (ev.on ? 0x90u : 0x80u) | ((DWORD) ev.note << 8) | ((DWORD) (ev.on ? ev.vel : 0) << 16);
        midiOutShortMsg (h, msg);
    }
    timeEndPeriod (1);
    midiOutClose (h);
    std::printf ("SENT %zu\n", evs.size());
    return 0;
}

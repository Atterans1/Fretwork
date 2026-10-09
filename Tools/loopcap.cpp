// Records the default render endpoint via WASAPI loopback to a float WAV.
// usage: loopcap <out.wav> <seconds>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#pragma comment(lib, "ole32.lib")

static void writeWav (const char* path, const std::vector<float>& d, int ch, int fs)
{
    FILE* f = std::fopen (path, "wb");
    if (! f) { std::printf ("cannot open %s\n", path); return; }
    const unsigned dataBytes = (unsigned) (d.size() * sizeof (float));
    const unsigned riff = 36 + dataBytes;
    const unsigned short fmt = 3, bits = 32, chs = (unsigned short) ch, align = (unsigned short) (ch * 4);
    const unsigned rate = (unsigned) fs, bps = rate * align, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&chs, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    std::fwrite (d.data(), 1, dataBytes, f);
    std::fclose (f);
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: loopcap out.wav seconds\n"); return 2; }
    const double secs = std::atof (argv[2]);
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioClient* ac = nullptr; IAudioCaptureClient* cap = nullptr;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))) return 3;
    if (FAILED (en->GetDefaultAudioEndpoint (eRender, eConsole, &dev))) return 4;
    if (FAILED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &ac))) return 5;
    WAVEFORMATEX* wf = nullptr; ac->GetMixFormat (&wf);
    const int ch = wf->nChannels, fs = (int) wf->nSamplesPerSec;
    bool isFloat = wf->wBitsPerSample == 32;
    if (FAILED (ac->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, wf, nullptr))) return 6;
    if (FAILED (ac->GetService (__uuidof (IAudioCaptureClient), (void**) &cap))) return 7;
    std::printf ("READY fs=%d ch=%d bits=%d\n", fs, ch, wf->wBitsPerSample); std::fflush (stdout);
    std::vector<float> data; data.reserve ((size_t) (secs * fs * ch) + 4096);
    ac->Start();
    const size_t want = (size_t) (secs * fs) * (size_t) ch;
    const ULONGLONG t0 = GetTickCount64();
    while (data.size() < want)
    {
        Sleep (5);
        UINT32 pkt = 0; cap->GetNextPacketSize (&pkt);
        while (pkt > 0)
        {
            BYTE* buf = nullptr; UINT32 frames = 0; DWORD flags = 0;
            cap->GetBuffer (&buf, &frames, &flags, nullptr, nullptr);
            const size_t n = (size_t) frames * ch;
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || ! isFloat) data.insert (data.end(), n, 0.f);
            else { const float* p = (const float*) buf; data.insert (data.end(), p, p + n); }
            cap->ReleaseBuffer (frames);
            cap->GetNextPacketSize (&pkt);
        }
        // silence when nothing plays: WASAPI delivers no packets, so pad by wall clock
        const size_t byClock = (size_t) ((GetTickCount64() - t0) / 1000.0 * fs) * (size_t) ch;
        if (byClock > data.size() + (size_t) fs / 5 * ch) data.insert (data.end(), byClock - data.size(), 0.f);
    }
    ac->Stop();
    data.resize (want);
    writeWav (argv[1], data, ch, fs);
    std::printf ("WROTE %s frames=%zu\n", argv[1], data.size() / (size_t) ch);
    return 0;
}

// HostCheck: loads the *deployed* Fretwork.vst3 through JUCE's VST3 host, the
// same route FL Studio takes, and checks what otherwise needs a person at the
// DAW: host automation, preset save/recall, editor open/close/reopen at 1x and
// 2x scale (pixels captured from the plugin's own window, kept off-screen so
// nobody's desktop is disturbed) and MIDI note onsets.
//
// usage: HostCheck [path-to-.vst3] [snapshot-dir]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <windows.h>

#include <cmath>
#include <cstdio>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

// VST3 exposes numeric parameter IDs to the host, so look up by display name,
// as a DAW's automation list does.
juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* param : p.getParameters())
        if (param->getName (64).equalsIgnoreCase (name))
            return param;
    return nullptr;
}

struct Note { double at; int note; bool on; };

// Renders `seconds` of audio. `beforeBlock` runs ahead of each block so a test
// can move parameters mid-render, as host automation does.
juce::AudioBuffer<float> render (juce::AudioPluginInstance& p, double seconds,
                                 const std::vector<Note>& notes,
                                 std::function<void (int64_t)> beforeBlock = {})
{
    const int total = (int) (seconds * kRate);
    const int channels = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> out (2, total), block (channels, kBlock);
    out.clear();

    for (int start = 0; start < total; start += kBlock)
    {
        const int n = juce::jmin (kBlock, total - start);
        if (beforeBlock)
            beforeBlock (start);

        juce::MidiBuffer midi;
        for (const auto& e : notes)
        {
            const int s = (int) (e.at * kRate);
            if (s >= start && s < start + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) 100)
                                    : juce::MidiMessage::noteOff (1, e.note), s - start);
        }

        block.setSize (channels, n, false, false, true);
        block.clear();
        p.processBlock (block, midi);

        for (int c = 0; c < 2 && c < block.getNumChannels(); ++c)
            out.copyFrom (c, start, block, c, 0, n);
    }
    return out;
}

float rms (const juce::AudioBuffer<float>& b, double from, double to)
{
    const int s0 = (int) (from * kRate), s1 = juce::jmin (b.getNumSamples(), (int) (to * kRate));
    double sum = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = s0; i < s1; ++i)
            sum += (double) b.getSample (c, i) * b.getSample (c, i);
    return (float) std::sqrt (sum / juce::jmax (1, 2 * (s1 - s0)));
}

// Onsets: 10 ms RMS frames; a frame that jumps past 4x the loudest of the
// previous 50 ms (and above the noise floor) starts a note. 150 ms refractory.
std::vector<double> onsets (const juce::AudioBuffer<float>& b)
{
    std::vector<float> frames;
    for (double t = 0; t + 0.01 <= b.getNumSamples() / kRate; t += 0.01)
        frames.push_back (rms (b, t, t + 0.01));

    std::vector<double> found;
    double last = -1.0;
    for (size_t i = 5; i < frames.size(); ++i)
    {
        float prior = 0;
        for (size_t k = i - 5; k < i; ++k)
            prior = juce::jmax (prior, frames[k]);
        const double t = (double) i * 0.01;
        if (frames[i] > 1.0e-3f && frames[i] > 4.0f * prior && t - last > 0.15)
        {
            found.push_back (t);
            last = t;
        }
    }
    return found;
}

//==============================================================================
// Off-screen editor window + capture of the real HWND pixels.
juce::Image capture (juce::Component& c)
{
    auto* peer = c.getPeer();
    if (peer == nullptr)
        return {};

    auto hwnd = (HWND) peer->getNativeHandle();
    RECT r {};
    GetClientRect (hwnd, &r);
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0)
        return {};

    HDC screen = GetDC (nullptr);
    HDC mem = CreateCompatibleDC (screen);
    BITMAPINFO bi {};
    bi.bmiHeader.biSize = sizeof (bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection (screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto old = SelectObject (mem, dib);
    PrintWindow (hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);

    juce::Image img (juce::Image::RGB, w, h, false);
    {
        juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
        auto* src = static_cast<const uint8_t*> (bits);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto* px = src + (y * w + x) * 4;
                data.setPixelColour (x, y, juce::Colour (px[2], px[1], px[0]));
            }
    }

    SelectObject (mem, old);
    DeleteObject (dib);
    DeleteDC (mem);
    ReleaseDC (nullptr, screen);
    return img;
}

// Luminance spread: a blank or failed capture is one flat colour.
float lumaStdDev (const juce::Image& img)
{
    double sum = 0, sq = 0;
    int n = 0;
    for (int y = 0; y < img.getHeight(); y += 3)
        for (int x = 0; x < img.getWidth(); x += 3)
        {
            const double l = img.getPixelAt (x, y).getPerceivedBrightness();
            sum += l; sq += l * l; ++n;
        }
    const double m = sum / juce::jmax (1, n);
    return (float) std::sqrt (juce::jmax (0.0, sq / juce::jmax (1, n) - m * m));
}

float meanAbsDiff (const juce::Image& a, const juce::Image& b)
{
    if (a.getBounds() != b.getBounds())
        return 1.0f;
    double sum = 0;
    int n = 0;
    for (int y = 0; y < a.getHeight(); y += 2)
        for (int x = 0; x < a.getWidth(); x += 2)
        {
            sum += std::abs (a.getPixelAt (x, y).getPerceivedBrightness() - b.getPixelAt (x, y).getPerceivedBrightness());
            ++n;
        }
    return (float) (sum / juce::jmax (1, n));
}

void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

struct Shown
{
    juce::Image image;
    juce::Rectangle<int> bounds;
};

Shown showEditor (juce::AudioPluginInstance& p, float scale, const juce::File& png)
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorIfNeeded());
    if (ed == nullptr)
        return {};

    ed->setScaleFactor (scale);
    // Temporary + no activation: no taskbar entry, never takes focus, and it
    // sits far off every monitor.
    ed->setTopLeftPosition (-8000, -8000);
    ed->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    ed->setVisible (true);
    pump (1500);

    Shown s;
    s.bounds = ed->getPeer() != nullptr ? ed->getPeer()->getBounds() : juce::Rectangle<int>();
    s.image = capture (*ed);
    if (s.image.isValid())
    {
        png.deleteFile();
        juce::FileOutputStream os (png);
        juce::PNGImageFormat().writeImageToStream (s.image, os);
    }

    p.editorBeingDeleted (ed.get());
    ed.reset();
    pump (200);
    return s;
}

void captureBody (juce::AudioPluginInstance& p, juce::AudioProcessorParameter* body,
                    int index, const juce::File& png)
{
    if (body != nullptr)
        body->setValueNotifyingHost ((float) index / 3.0f);

    const auto shown = showEditor (p, 1.0f, png);
    std::printf ("  body %d -> %s (%dx%d, luma spread %.3f)\n", index, png.getFileName().toRawUTF8(),
                 shown.image.getWidth(), shown.image.getHeight(), lumaStdDev (shown.image));
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File plugin (argc > 1 ? argv[1] : "C:\\Program Files\\Common Files\\VST3\\Fretwork.vst3");
    const juce::File snapDir (argc > 2 ? argv[2] : "C:\\Users\\jeffr\\Fretwork\\Docs\\screenshots\\host");
    snapDir.createDirectory();

    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> types;
    fm.getFormat (0)->findAllTypesForFile (types, plugin.getFullPathName());
    if (types.isEmpty())
    {
        std::printf ("no plugin found in %s\n", plugin.getFullPathName().toRawUTF8());
        return 2;
    }

    juce::String err;
    auto p = fm.createPluginInstance (*types[0], kRate, kBlock, err);
    if (p == nullptr)
    {
        std::printf ("load failed: %s\n", err.toRawUTF8());
        return 2;
    }

    std::printf ("loaded %s %s (%s), %d params\n", types[0]->name.toRawUTF8(), types[0]->version.toRawUTF8(),
                 plugin.getFullPathName().toRawUTF8(), p->getParameters().size());
    p->enableAllBuses();
    p->prepareToPlay (kRate, kBlock);

    auto* mode = findParam (*p, "Mode");
    auto* output = findParam (*p, "Output");
    auto* strum = findParam (*p, "Strum Time");
    auto* body = findParam (*p, "Body");
    check (mode && output && strum && body, "Mode / Output / Strum Time / Body reachable by name through the VST3 host");
    if (! (mode && output && strum && body))
        for (int i = 0; i < juce::jmin (90, p->getParameters().size()); ++i)
            std::printf ("    %d: %s\n", i, p->getParameters()[i]->getName (64).toRawUTF8());
    if (! (mode && output && strum && body))
        return 1;

    mode->setValueNotifyingHost (0.0f);          // POLY
    render (*p, 0.2, {});

    // --- MIDI ------------------------------------------------------------------
    std::puts ("midi:");
    {
        const auto audio = render (*p, 3.0, { { 0.30, 52, true }, { 0.80, 52, false },
                                              { 1.10, 55, true }, { 1.60, 55, false },
                                              { 1.90, 59, true }, { 2.40, 59, false } });
        const auto found = onsets (audio);
        juce::String at;
        for (auto t : found)
            at << juce::String (t, 2) << "s ";
        check (found.size() == 3, "3 notes in -> " + juce::String ((int) found.size()) + " onsets at " + at);
        bool timed = found.size() == 3;
        const double expect[] { 0.30, 1.10, 1.90 };
        for (size_t i = 0; timed && i < 3; ++i)
            timed = std::abs (found[i] - expect[i]) < 0.05;
        check (timed, "onsets within 50 ms of the note-ons");
        render (*p, 1.5, {});
    }

    // --- automation ---------------------------------------------------------------
    std::puts ("automation:");
    {
        const float outDefault = output->getValue();
        const auto audio = render (*p, 2.0, { { 0.05, 48, true }, { 1.95, 48, false } },
                                   [&] (int64_t s)
                                   {
                                       // ramp the output parameter to 0 between 0.8 s and 1.0 s
                                       const double t = (double) s / kRate;
                                       if (t >= 0.8 && t <= 1.0)
                                           output->setValueNotifyingHost ((float) (outDefault * (1.0 - (t - 0.8) / 0.2)));
                                       else if (t > 1.0)
                                           output->setValueNotifyingHost (0.0f);
                                   });
        const float before = rms (audio, 0.4, 0.75), after = rms (audio, 1.3, 1.9);
        const float dropDb = 20.0f * std::log10 (juce::jmax (1.0e-9f, after) / juce::jmax (1.0e-9f, before));
        check (before > 1.0e-3f, "note audible before automation (rms " + juce::String (before, 4) + ")");
        check (dropDb < -20.0f, "automating Output to minimum drops level by " + juce::String (dropDb, 1) + " dB");
        output->setValueNotifyingHost (outDefault);
        render (*p, 1.0, {});
    }

    // --- preset save / recall ----------------------------------------------------
    std::puts ("preset:");
    {
        strum->setValueNotifyingHost (0.8f);
        body->setValueNotifyingHost (1.0f);
        render (*p, 0.1, {});
        const float savedStrum = strum->getValue(), savedBody = body->getValue();
        juce::MemoryBlock preset;
        p->getStateInformation (preset);

        strum->setValueNotifyingHost (0.1f);
        body->setValueNotifyingHost (0.0f);
        render (*p, 0.1, {});

        p->setStateInformation (preset.getData(), (int) preset.getSize());
        render (*p, 0.1, {});
        pump (100);
        check (preset.getSize() > 0, "state saved (" + juce::String ((int) preset.getSize()) + " bytes)");
        check (std::abs (strum->getValue() - savedStrum) < 1.0e-3f,
               "Strum Time recalled: " + strum->getCurrentValueAsText() + " (normalised " + juce::String (strum->getValue(), 3) + ")");
        check (std::abs (body->getValue() - savedBody) < 1.0e-3f,
               "Body recalled: " + body->getCurrentValueAsText());
    }

    // --- every body, one capture each ---------------------------------------
    std::puts ("bodies:");
    for (int b = 0; b < 4; ++b)
        captureBody (*p, body, b, snapDir.getChildFile ("host_body" + juce::String (b) + ".png"));
    if (body != nullptr)
        body->setValueNotifyingHost (0.0f);

    // --- editor: open at 1x and 2x, close, reopen --------------------------------
    std::puts ("editor:");
    {
        const auto a = showEditor (*p, 1.0f, snapDir.getChildFile ("host_1x.png"));
        check (a.image.isValid() && lumaStdDev (a.image) > 0.05f,
               "opens at 1x: " + juce::String (a.image.getWidth()) + "x" + juce::String (a.image.getHeight())
               + ", drawn (luma spread " + juce::String (lumaStdDev (a.image), 3) + ")");

        const auto b = showEditor (*p, 2.0f, snapDir.getChildFile ("host_2x.png"));
        check (b.image.isValid() && b.image.getWidth() >= 2 * a.image.getWidth() - 4
                   && lumaStdDev (b.image) > 0.05f,
               "opens at 2x: " + juce::String (b.image.getWidth()) + "x" + juce::String (b.image.getHeight()));

        const auto c = showEditor (*p, 1.0f, snapDir.getChildFile ("host_reopen.png"));
        const float diff = meanAbsDiff (a.image, c.image);
        check (c.image.isValid() && diff < 0.02f,
               "close + reopen renders the same (mean diff " + juce::String (diff, 4) + ")");
    }

    p->releaseResources();
    p.reset();
    std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}

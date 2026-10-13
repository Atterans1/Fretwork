#include "E2E.h"

namespace e2e
{

Pcm decodeToMono (const juce::File& file, double targetFs, juce::String& formatName, double& sourceFs, int& sourceChannels)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return {};

    formatName = reader->getFormatName();
    sourceFs = reader->sampleRate;
    sourceChannels = (int) reader->numChannels;

    const int len = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);

    std::vector<float> mono ((size_t) len, 0.f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), buf.getReadPointer (c), 1.f / (float) buf.getNumChannels(), len);

    // 4th-order low-pass at 45 % of the target rate, then resample.
    for (int pass = 0; pass < 2; ++pass)
    {
        juce::IIRFilter lp;
        lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sourceFs, 0.45 * targetFs));
        lp.processSamples (mono.data(), len);
    }

    Pcm out;
    out.fs = targetFs;
    const double ratio = sourceFs / targetFs;
    const int outLen = (int) std::floor ((double) (len - 4) / ratio);
    out.x.resize ((size_t) outLen);
    juce::LagrangeInterpolator interp;
    interp.process (ratio, mono.data(), out.x.data(), outLen);

    float peak = 0.f;
    for (float s : out.x) peak = std::max (peak, std::abs (s));
    if (peak > 0.f)
        for (auto& s : out.x) s /= peak;
    return out;
}

void writeWav (const juce::File& file, const std::vector<float>& left, const std::vector<float>& right, double fs)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::FileOutputStream> (file);
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.release(), fs, 2, 24, {}, 0));
    if (w == nullptr) return;
    const int n = (int) std::min (left.size(), right.size());
    const float* chans[] = { left.data(), right.data() };
    w->writeFromFloatArrays (chans, 2, n);
}

std::vector<int> parseTuning (const juce::String& text)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    std::vector<int> out;
    for (auto tok : juce::StringArray::fromTokens (text.replace (",", " "), " ", {}))
    {
        tok = tok.trim().replace ("b", "#-");   // Eb -> handled below
        if (tok.isEmpty()) continue;
        int pc = -1; int len = 0;
        for (int i = 0; i < 12; ++i)
        {
            const juce::String n (names[i]);
            if (tok.startsWithIgnoreCase (n) && n.length() > len) { pc = i; len = n.length(); }
        }
        if (pc < 0) return {};
        juce::String rest = tok.substring (len);
        if (rest.startsWith ("#-")) { pc = (pc + 11) % 12; rest = rest.substring (2); }
        if (! rest.containsOnly ("-0123456789") || rest.isEmpty()) return {};
        out.push_back ((rest.getIntValue() + 1) * 12 + pc);
    }
    return out;
}

std::vector<TabNote> parseTab (const juce::String& text, const std::vector<int>& openMidiTopDown, juce::String& error)
{
    juce::StringArray lines;
    lines.addLines (text);

    const auto isStringLine = [] (const juce::String& raw)
    {
        const auto l = raw.trimStart();
        int i = 0;
        if (l.length() < 2 || ! juce::String ("ABCDEFGabcdefg").containsChar (l[0])) return -1;
        i = 1;
        if (l[i] == '#' || l[i] == 'b') ++i;
        return i < l.length() && l[i] == '|' ? i + 1 : -1;
    };

    std::vector<TabNote> notes;
    int system = 0;
    for (int i = 0; i < lines.size(); ++i)
    {
        if (isStringLine (lines[i]) < 0) continue;
        int count = 0;
        while (i + count < lines.size() && isStringLine (lines[i + count]) >= 0) ++count;

        std::vector<int> open = openMidiTopDown;
        if (open.empty())
        {
            if (count == 4)      open = { 43, 38, 33, 28 };
            else if (count == 5) open = { 43, 38, 33, 28, 23 };
            else if (count == 6) open = { 64, 59, 55, 50, 45, 40 };
            else if (count == 7) open = { 64, 59, 55, 50, 45, 40, 35 };
        }
        if ((int) open.size() != count)
        {
            error = "tab system with " + juce::String (count) + " strings at line " + juce::String (i + 1) + " needs --tuning with that many notes";
            return {};
        }

        ++system;
        std::vector<TabNote> sys;
        for (int s = 0; s < count; ++s)
        {
            const auto l = lines[i + s].trimStart();
            char pendingTech = 0;
            for (int c = isStringLine (l); c < l.length(); ++c)
            {
                const auto ch = l[c];
                if (juce::CharacterFunctions::isDigit (ch))
                {
                    int fret = ch - '0';
                    const int start = c;
                    if (c + 1 < l.length() && juce::CharacterFunctions::isDigit (l[c + 1]))
                        fret = fret * 10 + (l[++c] - '0');
                    TabNote n;
                    n.line = system;
                    n.column = start;
                    n.stringNo = s + 1;
                    n.fret = fret;
                    n.midi = open[(size_t) s] + fret;
                    n.technique = pendingTech;
                    sys.push_back (n);
                    pendingTech = 0;
                }
                else if (ch == '\\' || ch == '/' || ch == 'p' || ch == 'h')
                    pendingTech = (char) ch;
                else if (ch == '|')
                    pendingTech = 0;
            }
        }
        std::stable_sort (sys.begin(), sys.end(), [] (const TabNote& x, const TabNote& y) { return x.column < y.column; });
        for (auto& n : sys) { n.index = (int) notes.size(); notes.push_back (n); }
        i += count - 1;
    }
    if (notes.empty() && error.isEmpty())
        error = "no tab systems found";
    return notes;
}

std::vector<TabNote> parseMidiReference (const juce::File& file, juce::String& error)
{
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) { error = "cannot read MIDI file"; return {}; }
    mf.convertTimestampTicksToSeconds();

    struct On { double t; int midi; };
    std::vector<On> ons;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (const auto* e : *mf.getTrack (t))
            if (e->message.isNoteOn() && e->message.getVelocity() > 0)
                ons.push_back ({ e->message.getTimeStamp(), e->message.getNoteNumber() });
    std::stable_sort (ons.begin(), ons.end(), [] (const On& x, const On& y) { return x.t != y.t ? x.t < y.t : x.midi < y.midi; });

    std::vector<TabNote> notes;
    for (const auto& o : ons)
    {
        TabNote n;
        n.index = (int) notes.size();
        n.midi = o.midi;
        notes.push_back (n);
    }
    if (notes.empty()) error = "MIDI file has no notes";
    return notes;
}

} // namespace e2e

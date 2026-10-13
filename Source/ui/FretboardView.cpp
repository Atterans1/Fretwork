#include "FretboardView.h"
#include "WavesStyle.h"
#include "engine/ChordDetect.h"

namespace fw::ui
{

void FretboardView::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();

    // Waves group frame titled on its top rule, with the diagram in a black
    // LCD well inside it.
    waves::group (g, full, "Fretboard");

    const auto well = full.withTrimmedTop (19.0f).reduced (8.0f, 0.0f).withTrimmedBottom (8.0f);

    if (well.getWidth() <= 24.0f || well.getHeight() <= 16.0f)
        return;

    waves::lcd (g, well, 3.0f);

    // String 0 (low E) is drawn first, at the top, so the visual order matches
    // the UiSnapshot index order used by every other panel.
    const auto area = well.reduced (12.0f, 6.0f);

    const auto stepX = area.getWidth() / (float) kNumFrets;

    const auto fretX = [area, stepX] (int fret)
    {
        return area.getX() + (float) juce::jlimit (0, kNumFrets, fret) * stepX;
    };

    const auto stringY = [area] (int index)
    {
        return area.getY() + ((float) index + 0.5f) * area.getHeight() / (float) fw::kNumStrings;
    };

    const auto top = stringY (0) - 3.0f;
    const auto bottom = stringY (fw::kNumStrings - 1) + 3.0f;

    // ---- fret wires: hairlines; the nut is a slightly heavier light rule
    for (int f = 0; f <= kNumFrets; ++f)
    {
        const auto x = fretX (f);

        if (f == 0)
        {
            g.setColour (Palette::caption.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, top, 2.0f, bottom - top));
        }
        else
        {
            g.setColour (Palette::line.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (x - 0.5f, top, 1.0f, bottom - top));
        }
    }

    // ---- inlay markers at 3, 5, 7, 9, 12 (double) and 15, dim, between the
    // middle strings like a real neck
    const auto inlay = [&fretX] (int fret)
    {
        return (fretX (fret - 1) + fretX (fret)) * 0.5f;
    };

    const auto midY = (stringY (2) + stringY (3)) * 0.5f;
    const auto d = 2.0f;

    g.setColour (Palette::dim.withAlpha (0.35f));

    for (int f : { 3, 5, 7, 9, 15 })
        g.fillEllipse (inlay (f) - d, midY - d, d * 2.0f, d * 2.0f);

    {
        const auto y1 = (stringY (0) + stringY (1)) * 0.5f;
        const auto y2 = (stringY (4) + stringY (5)) * 0.5f;
        g.fillEllipse (inlay (12) - d, y1 - d, d * 2.0f, d * 2.0f);
        g.fillEllipse (inlay (12) - d, y2 - d, d * 2.0f, d * 2.0f);
    }

    // ---- strings: hairlines, the wound low strings a touch brighter
    for (int s = 0; s < fw::kNumStrings; ++s)
    {
        g.setColour (Palette::dim.withAlpha (s < 3 ? 0.85f : 0.65f));
        g.fillRect (juce::Rectangle<float> (area.getX(), stringY (s) - 0.5f, area.getWidth(), 1.0f));
    }

    if (ui == nullptr)
        return;

    // ---- chord name when the held notes are being played as a guitar shape
    {
        char name[24];
        chord::label (ChordName::unpack (ui->chordName.load()), name, (int) sizeof (name));
        if (name[0] != 0)
        {
            g.setColour (Palette::accent.brighter (0.35f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 (name), well.reduced (8.0f, 2.0f).withHeight (16.0f),
                        juce::Justification::topRight, false);
        }
    }

    // ---- sounding fret per string: violet LED dot with a soft glow. Fretted
    // notes sit between their wires where the finger goes; open strings get a
    // hollow LED ring on the nut.
    for (int s = 0; s < fw::kNumStrings; ++s)
    {
        const int fret = ui->fret[s].load();

        if (fret < 0)
            continue;

        const int f = juce::jlimit (0, kNumFrets, fret);
        const auto x = f == 0 ? fretX (0) : inlay (f);
        const auto y = stringY (s);
        const auto centre = juce::Point<float> (x, y);
        const float r = 3.6f;

        g.setColour (Palette::accent.withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (r * 5.0f, r * 5.0f).withCentre (centre));
        g.setColour (Palette::accent.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (centre));

        if (f == 0)
        {
            g.setColour (Palette::lcd);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
            g.setColour (Palette::accent.brighter (0.25f));
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), 1.5f);
        }
        else
        {
            g.setColour (Palette::accent.brighter (0.25f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillEllipse (juce::Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (centre.translated (-r * 0.25f, -r * 0.25f)));
        }
    }
}

} // namespace fw::ui

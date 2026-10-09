#include "FretboardView.h"

namespace fw::ui
{

void FretboardView::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (full, 6.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (full.reduced (0.5f), 6.0f, 1.0f);

    // String 0 (low E) is drawn first, at the top, so the visual order matches
    // the UiSnapshot index order used by every other panel.
    auto area = full.reduced (8.0f, 7.0f);

    g.setFont (Palette::font (9.0f));
    g.setColour (Palette::dim);
    g.drawText ("FRETBOARD", area.removeFromTop (12.0f), juce::Justification::centredRight, false);

    if (area.getWidth() <= 8.0f || area.getHeight() <= 8.0f)
        return;

    const auto stepX = area.getWidth() / (float) kNumFrets;

    const auto fretX = [area, stepX] (int fret)
    {
        return area.getX() + (float) juce::jlimit (0, kNumFrets, fret) * stepX;
    };

    const auto stringY = [area] (int index)
    {
        return area.getY() + ((float) index + 0.5f) * area.getHeight() / (float) fw::kNumStrings;
    };

    // ---- fret wire lines
    g.setColour (Palette::line);
    for (int f = 0; f <= kNumFrets; ++f)
    {
        const auto x = fretX (f);
        g.setColour (f == 0 ? Palette::amber.withAlpha (0.75f) : Palette::line);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, area.getY() - 3.0f,
                                            f == 0 ? 2.0f : 1.0f, area.getHeight() + 6.0f));
    }

    // ---- inlay dots at 3, 5, 7, 9, 12 (double) and 15
    const auto inlay = [&fretX] (int fret)
    {
        return (fretX (fret - 1) + fretX (fret)) * 0.5f;
    };

    g.setColour (Palette::dim.withAlpha (0.55f));

    for (int f : { 3, 5, 7, 9, 15 })
    {
        const auto d = 2.2f;
        g.fillEllipse (inlay (f) - d, stringY (0) - d, d * 2.0f, d * 2.0f);
    }

    {
        const auto d = 2.2f, off = 5.5f, y = stringY (0);
        g.fillEllipse (inlay (12) - off - d, y - d, d * 2.0f, d * 2.0f);
        g.fillEllipse (inlay (12) + off - d, y - d, d * 2.0f, d * 2.0f);
    }

    // ---- strings: low strings drawn thicker
    for (int s = 0; s < fw::kNumStrings; ++s)
    {
        const float thickness = 1.9f - (float) s * 0.16f;
        g.setColour (Palette::dim.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (area.getX(), stringY (s) - thickness * 0.5f,
                                            area.getWidth(), thickness));
    }

    if (ui == nullptr)
        return;

    // ---- sounding fret per string
    for (int s = 0; s < fw::kNumStrings; ++s)
    {
        const int fret = ui->fret[s].load();

        if (fret < 0)
            continue;

        const auto x = fretX (juce::jlimit (0, kNumFrets, fret));
        const auto y = stringY (s);
        const float d = 4.0f;

        g.setColour (Palette::accent.withAlpha (0.30f));
        g.fillEllipse (x - d - 1.5f, y - d - 1.5f, (d + 1.5f) * 2.0f, (d + 1.5f) * 2.0f);
        g.setColour (juce::Colour (0xffe8ebee));
        g.fillEllipse (x - d, y - d, d * 2.0f, d * 2.0f);
    }
}

} // namespace fw::ui

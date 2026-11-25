#pragma once

// Flat drawing primitives shared by FretLookAndFeel and every widget's
// paint(). Each one is a solid fill with a 1 px edge. The traits are
// documented in Docs/waves-ref/DESIGN.md.

#include "FretLookAndFeel.h"

#include <cmath>

namespace fw::ui::waves
{

using RF = juce::Rectangle<float>;

//==============================================================================
/** The plugin chassis: a flat frame with a 1 px edge around a flat charcoal face. */
inline void chassis (juce::Graphics& g, RF r)
{
    g.setColour (Palette::frame);
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);

    g.setColour (Palette::background);
    g.fillRoundedRectangle (r.reduced (6.0f), 6.0f);
}

/** Small-caps section caption, tracked out, warm grey. */
inline void caption (juce::Graphics& g, RF r, const juce::String& text,
                     juce::Justification j = juce::Justification::centred,
                     juce::Colour c = Palette::caption)
{
    g.setColour (c);
    g.setFont (Palette::fontSemi (Palette::Scale::small).withExtraKerningFactor (0.08f));
    g.drawText (text.toUpperCase(), r, j, true);
}

/** Waves Tune group frame: a hairline rounded rule with the title sitting
    centred in a gap on the top edge. An empty title draws a closed frame. */
inline void group (juce::Graphics& g, RF r, const juce::String& title = {})
{
    const float top = r.getY() + 7.0f;
    const auto box = r.withTop (top);

    g.setColour (Palette::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (box, 5.0f);

    juce::Path rule;
    rule.addRoundedRectangle (box.reduced (0.5f), 5.0f);

    juce::Graphics::ScopedSaveState s (g);

    if (title.isNotEmpty())
    {
        const auto font = Palette::fontSemi (Palette::Scale::small).withExtraKerningFactor (0.08f);
        const float w = juce::GlyphArrangement::getStringWidth (font, title.toUpperCase()) + 16.0f;
        const RF gap (box.getCentreX() - w * 0.5f, top - 6.0f, w, 12.0f);
        g.excludeClipRegion (gap.toNearestInt());
        g.setColour (Palette::line);
        g.strokePath (rule, juce::PathStrokeType (1.0f));
        g.restoreState();
        g.saveState();
        caption (g, gap.withHeight (14.0f).translated (0.0f, -1.0f), title);
        return;
    }

    g.setColour (Palette::line);
    g.strokePath (rule, juce::PathStrokeType (1.0f));
}

/** Flat black LCD well with a 1 px edge. */
inline void lcd (juce::Graphics& g, RF r, float corner = 3.0f)
{
    g.setColour (Palette::lcd);
    g.fillRoundedRectangle (r, corner);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

/** Inset numeric readout (Waves Tune "15.0 ms" boxes). */
inline void valueBox (juce::Graphics& g, RF r, const juce::String& text, float fontHeight,
                      bool enabled = true, juce::Colour colour = Palette::text,
                      juce::Justification j = juce::Justification::centred)
{
    lcd (g, r, 3.0f);
    g.setColour (enabled ? colour : Palette::dim.withAlpha (0.6f));
    g.setFont (Palette::fontNum (fontHeight));
    g.drawFittedText (text, r.reduced (4.0f, 0.0f).toNearestInt(), j, 1, 0.7f);
}

/** LED dot ring: dots around the sweep, lit up to the value, with an amber dot
    marking the current position. Lit and unlit are solid colours only. */
inline void ledRing (juce::Graphics& g, juce::Point<float> centre, float radius,
                     float startAngle, float endAngle, float proportion, bool enabled)
{
    const float sweep = endAngle - startAngle;
    const float arcLen = std::abs (sweep) * radius;
    const int dots = juce::jlimit (12, 64, (int) std::round (arcLen / 5.2f));
    const float dotR = juce::jlimit (0.9f, 1.9f, radius * 0.055f);
    const bool closed = std::abs (std::abs (sweep) - juce::MathConstants<float>::twoPi) < 0.01f;
    const int steps = closed ? dots : dots - 1;
    const float p = juce::jlimit (0.0f, 1.0f, proportion);
    const int lit = (int) std::floor (p * (float) steps + 0.5f);

    for (int i = 0; i < dots; ++i)
    {
        const float a = startAngle + sweep * (float) i / (float) steps;
        const juce::Point<float> pt (centre.x + std::sin (a) * radius, centre.y - std::cos (a) * radius);
        const bool on = enabled && i <= lit && p > 0.0f;

        g.setColour (on ? Palette::accent : Palette::unlit);
        g.fillEllipse (RF (dotR * 2.0f, dotR * 2.0f).withCentre (pt));
    }

    if (! enabled)
        return;

    // amber position marker
    const float a = startAngle + sweep * p;
    const juce::Point<float> pt (centre.x + std::sin (a) * radius, centre.y - std::cos (a) * radius);
    g.setColour (Palette::amber);
    g.fillEllipse (RF (dotR * 2.6f, dotR * 2.6f).withCentre (pt));
}

/** Flat knob cap: one face colour inside a 1 px edge, with a single pointer line. */
inline void knobCap (juce::Graphics& g, juce::Point<float> centre, float radius,
                     float angle, bool large, bool enabled)
{
    const auto disc = RF (radius * 2.0f, radius * 2.0f).withCentre (centre);
    const auto face = disc.reduced (radius * (large ? 0.16f : 0.12f));

    g.setColour (Palette::panelHi);
    g.fillEllipse (disc);
    g.setColour (Palette::line);
    g.drawEllipse (disc, 1.0f);

    // pointer notch
    const float s = std::sin (angle), c = -std::cos (angle);
    const float r0 = face.getWidth() * 0.5f * (large ? 0.55f : 0.35f);
    const float r1 = face.getWidth() * 0.5f * 0.92f;
    g.setColour (enabled ? Palette::text : Palette::dim);
    g.drawLine (centre.x + s * r0, centre.y + c * r0, centre.x + s * r1, centre.y + c * r1,
                juce::jmax (1.5f, radius * 0.07f));
}

/** Vertical dotted LED meter (Atlas In/Out columns). */
inline void ledMeter (juce::Graphics& g, RF r, float level01, int segments = 24)
{
    lcd (g, r, 2.0f);
    const auto inner = r.reduced (3.0f, 4.0f);
    const float step = inner.getHeight() / (float) segments;
    const float d = juce::jmin (inner.getWidth() * 0.5f, step * 0.62f);
    const int lit = (int) std::round (juce::jlimit (0.0f, 1.0f, level01) * (float) segments);

    for (int i = 0; i < segments; ++i)
    {
        const float cy = inner.getBottom() - step * ((float) i + 0.5f);
        const bool on = i < lit;
        const auto col = i >= segments - 2 ? Palette::amber : Palette::accent;
        for (float cx : { inner.getX() + inner.getWidth() * 0.3f, inner.getX() + inner.getWidth() * 0.7f })
        {
            g.setColour (on ? col.brighter (0.2f) : Palette::unlit);
            g.fillEllipse (RF (d, d).withCentre ({ cx, cy }));
        }
    }
}

/** Flat push-button face, all states: a 1 px lcd edge, then a solid fill whose
    colour carries the state. on: accent. hover: lighter. down: darker. */
inline void buttonFace (juce::Graphics& g, RF r, bool on, bool hover, bool down, bool enabled,
                        float corner = 3.0f)
{
    juce::Graphics::ScopedSaveState s (g);
    if (! enabled) g.setOpacity (0.4f);

    g.setColour (Palette::lcd);
    g.fillRoundedRectangle (r, corner);

    juce::Colour fill = Palette::panelHi;

    if (on)
        fill = down ? Palette::accent.darker (0.25f) : (hover ? Palette::accent.brighter (0.25f) : Palette::accent);
    else if (down)
        fill = Palette::background;
    else if (hover)
        fill = Palette::panelHi.brighter (0.06f);

    g.setColour (fill);
    g.fillRoundedRectangle (r.reduced (1.0f), corner - 0.5f);
}

} // namespace fw::ui::waves

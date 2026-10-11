#pragma once

// Waves-style drawing primitives shared by FretLookAndFeel and every widget's
// paint(). The traits they implement are documented, with the reference
// renders, in Docs/waves-ref/DESIGN.md.

#include "FretLookAndFeel.h"

#include <cmath>

namespace fw::ui::waves
{

using RF = juce::Rectangle<float>;

//==============================================================================
/** The plugin chassis: a bevelled outer frame around a charcoal face. */
inline void chassis (juce::Graphics& g, RF r)
{
    // Outer frame: lighter rim with a top highlight and a bottom shadow line.
    g.setGradientFill (juce::ColourGradient (Palette::frame.brighter (0.25f), 0.0f, r.getY(),
                                             Palette::frame.darker (0.35f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 9.0f);

    const auto face = r.reduced (6.0f);
    g.setGradientFill (juce::ColourGradient (Palette::background.brighter (0.06f), 0.0f, face.getY(),
                                             Palette::background.darker (0.25f), 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, 6.0f);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (face.expanded (0.5f), 6.5f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine ((int) face.getY() + 1, face.getX() + 6.0f, face.getRight() - 6.0f);
}

/** Small-caps section caption, tracked out, warm grey. */
inline void caption (juce::Graphics& g, RF r, const juce::String& text,
                     juce::Justification j = juce::Justification::centred,
                     juce::Colour c = Palette::caption)
{
    g.setColour (c);
    g.setFont (Palette::fontSemi (10.5f).withExtraKerningFactor (0.08f));
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
        const auto font = Palette::fontSemi (10.5f).withExtraKerningFactor (0.08f);
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

/** Black LCD well with an inner shadow and a faint lower highlight. */
inline void lcd (juce::Graphics& g, RF r, float corner = 3.0f)
{
    g.setColour (Palette::lcd);
    g.fillRoundedRectangle (r, corner);

    // inner shadow along the top
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, r.getY(),
                                             juce::Colours::transparentBlack, 0.0f, r.getY() + 6.0f, false));
    g.fillRoundedRectangle (r.withHeight (juce::jmin (6.0f, r.getHeight())), corner);

    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) r.getBottom(), r.getX() + corner, r.getRight() - corner);
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

/** Atlas LED dot ring: dots around the sweep, lit up to the value, with a
    warm amber dot marking the current position. */
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

        if (on)
        {
            g.setColour (Palette::accent.withAlpha (0.28f));
            g.fillEllipse (RF (dotR * 4.4f, dotR * 4.4f).withCentre (pt));
            g.setColour (Palette::accent.brighter (0.25f));
        }
        else
        {
            g.setColour (Palette::unlit);
        }
        g.fillEllipse (RF (dotR * 2.0f, dotR * 2.0f).withCentre (pt));
    }

    if (! enabled)
        return;

    // amber position marker
    const float a = startAngle + sweep * p;
    const juce::Point<float> pt (centre.x + std::sin (a) * radius, centre.y - std::cos (a) * radius);
    g.setColour (Palette::amber.withAlpha (0.35f));
    g.fillEllipse (RF (dotR * 6.0f, dotR * 6.0f).withCentre (pt));
    g.setColour (Palette::amber.brighter (0.3f));
    g.fillEllipse (RF (dotR * 2.6f, dotR * 2.6f).withCentre (pt));
}

/** Glossy dark knob cap with a light pointer notch at angle. */
inline void knobCap (juce::Graphics& g, juce::Point<float> centre, float radius,
                     float angle, bool large, bool enabled)
{
    const auto disc = RF (radius * 2.0f, radius * 2.0f).withCentre (centre);

    // drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (disc.translated (0.0f, radius * 0.08f).expanded (radius * 0.04f));

    // skirt
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4d55), centre.x, disc.getY(),
                                             juce::Colour (0xff0f1013), centre.x, disc.getBottom(), false));
    g.fillEllipse (disc);

    // face
    const auto face = disc.reduced (radius * (large ? 0.16f : 0.12f));
    juce::ColourGradient grad (juce::Colour (0xff2c2e34), face.getCentreX() - radius * 0.3f, face.getY(),
                               juce::Colour (0xff111215), face.getCentreX() + radius * 0.3f, face.getBottom(), true);
    g.setGradientFill (grad);
    g.fillEllipse (face);

    // specular highlight
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), centre.x, face.getY(),
                                             juce::Colours::transparentWhite, centre.x, face.getCentreY(), false));
    g.fillEllipse (face.withHeight (face.getHeight() * 0.55f).reduced (face.getWidth() * 0.12f, 0.0f));

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (face, 1.0f);

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

/** Black rectangular push-button face (Waves Tune), all states.
    on: accent fill with glow. hover: lifted. down: inverted gradient. */
inline void buttonFace (juce::Graphics& g, RF r, bool on, bool hover, bool down, bool enabled,
                        float corner = 3.0f)
{
    juce::Graphics::ScopedSaveState s (g);
    if (! enabled) g.setOpacity (0.4f);

    // outer black edge
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r, corner);
    const auto f = r.reduced (1.0f);

    if (on)
    {
        g.setColour (Palette::accent.withAlpha (0.35f));
        g.drawRoundedRectangle (r.expanded (1.5f), corner + 1.5f, 2.0f);

        const auto top = down ? Palette::accent.darker (0.25f) : Palette::accent.brighter (hover ? 0.25f : 0.12f);
        const auto bot = down ? Palette::accent.darker (0.05f) : Palette::accent.darker (0.2f);
        g.setGradientFill (juce::ColourGradient (top, 0.0f, f.getY(), bot, 0.0f, f.getBottom(), false));
        g.fillRoundedRectangle (f, corner - 0.5f);
        g.setColour (juce::Colours::white.withAlpha (down ? 0.1f : 0.35f));
        g.drawHorizontalLine ((int) f.getY(), f.getX() + corner, f.getRight() - corner);
        return;
    }

    const float lift = hover ? 0.06f : 0.0f;
    const auto top = down ? juce::Colour (0xff121316) : juce::Colour (0xff2c2e34).brighter (lift);
    const auto bot = down ? juce::Colour (0xff24262b) : juce::Colour (0xff15161a).brighter (lift);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, f.getY(), bot, 0.0f, f.getBottom(), false));
    g.fillRoundedRectangle (f, corner - 0.5f);

    // 1 px light top bevel
    g.setColour (juce::Colours::white.withAlpha (down ? 0.04f : (hover ? 0.22f : 0.14f)));
    g.drawHorizontalLine ((int) f.getY(), f.getX() + corner, f.getRight() - corner);
}

} // namespace fw::ui::waves

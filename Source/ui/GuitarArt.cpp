#include "GuitarArt.h"

#include <cmath>

namespace fw::ui
{
namespace
{
// The art is laid out in a fixed 800 x 320 design space and mapped into the
// target rectangle by hand, because juce::Graphics has no transform stack.
// Only the guitar is drawn, so the design box is trimmed to its real
// extent: a wide empty box would letterbox the instrument down to a third of
// the panel.
constexpr float kDesignW = 400.0f + 82.0f;
constexpr float kDesignH = 320.0f;

const juce::Colour maple { static_cast<juce::uint32> (0xffd6b083) };
const juce::Colour mapleDark { static_cast<juce::uint32> (0xffb98a5c) };
const juce::Colour rosewood { static_cast<juce::uint32> (0xff5b3a29) };
const juce::Colour cherry { static_cast<juce::uint32> (0xff7a1f16) };
const juce::Colour cherryTop { static_cast<juce::uint32> (0xffa8362a) };
const juce::Colour sunburst { static_cast<juce::uint32> (0xff2b1810) };
const juce::Colour pickguardW { static_cast<juce::uint32> (0xfff2efe6) };
const juce::Colour chrome { static_cast<juce::uint32> (0xffcfd4d8) };

// Every design-space coordinate in the drawing code goes through these.
struct Map
{
    juce::Point<float> P (float x, float y) const { return { ox + x * s, oy + y * s }; }
    juce::Rectangle<float> R (float x, float y, float w, float h) const { return { ox + x * s, oy + y * s, w * s, h * s }; }
    float L (float v) const { return v * s; }
    juce::Rectangle<float> designBounds (float x, float y, float w, float h) const { return R (x, y, w, h); }
    float ox, oy, s;
};

juce::Colour lerpCol (juce::Colour a, juce::Colour b, float t)
{
    return juce::Colour (static_cast<juce::uint8> (juce::roundToInt (a.getRed()   + (b.getRed()   - a.getRed())   * t)),
                         static_cast<juce::uint8> (juce::roundToInt (a.getGreen() + (b.getGreen() - a.getGreen()) * t)),
                         static_cast<juce::uint8> (juce::roundToInt (a.getBlue()  + (b.getBlue()  - a.getBlue())  * t)));
}

// A guitar body is a figure eight: a wide lower bout, a pinched waist and a
// narrower upper bout, mirrored left/right about x = 400. One closed path,
// clockwise from the top centre.
juce::Path bodyPath (float waist, float lower, float upper, float corner)
{
    const float topY = 96.0f, waistY = 196.0f, botY = 256.0f + lower;
    const float topR = 400.0f - upper, waistR = 400.0f - waist, botR = 400.0f - lower;
    const float topL = 400.0f + upper, waistL = 400.0f + waist, botL = 400.0f + lower;
    juce::Path p;
    p.startNewSubPath (400.0f, topY);
    p.cubicTo (topR + upper * 0.35f, topY, topR, topY + upper * 0.25f, topR, topY + upper);
    p.cubicTo (topR, waistY - upper * 0.45f, waistR + (topR - waistR) * 0.4f, waistY - 26.0f, waistR, waistY);
    p.cubicTo (waistR + (botR - waistR) * 0.35f, waistY + 30.0f, botR, waistY + 40.0f, botR, waistY + lower * 0.55f);
    p.cubicTo (botR, botY - corner, botR - corner, botY, 400.0f, botY);
    p.cubicTo (botL + corner, botY, botL, botY - corner, botL, waistY + lower * 0.55f);
    p.cubicTo (botL, waistY + 40.0f, waistL - (botL - waistL) * 0.35f, waistY + 30.0f, waistL, waistY);
    p.cubicTo (waistL, waistY - 26.0f, topL, waistY - upper * 0.45f, topL, topY + upper);
    p.cubicTo (topL, topY + upper * 0.25f, topL - upper * 0.35f, topY, 400.0f, topY);
    p.closeSubPath();
    return p;
}
juce::Path neckPath (float w, float x0, float y0, float h)
{
    juce::Path p;
    p.addRoundedRectangle (juce::Rectangle<float> (x0, y0, w, h), w * 0.4f);
    return p;
}

juce::Path headstockPath (float neckW)
{
    // A Fender-style paddle: narrow at the nut, flaring to the tip, with a
    // shallow waist so it does not read as a rectangle.
    const float hw = neckW * 0.5f;
    juce::Path p;
    p.startNewSubPath (400.0f - hw, 40.0f);
    p.cubicTo (400.0f - hw * 1.15f, 30.0f, 400.0f - hw * 1.35f, 24.0f, 400.0f - hw * 1.45f, 14.0f);
    p.cubicTo (400.0f - hw * 1.6f, 4.0f, 400.0f - hw * 1.35f, 0.0f, 400.0f - hw * 0.85f, 0.0f);
    p.lineTo (400.0f + hw * 0.85f, 0.0f);
    p.cubicTo (400.0f + hw * 1.35f, 0.0f, 400.0f + hw * 1.6f, 4.0f, 400.0f + hw * 1.45f, 14.0f);
    p.cubicTo (400.0f + hw * 1.35f, 24.0f, 400.0f + hw * 1.15f, 30.0f, 400.0f + hw, 40.0f);
    p.closeSubPath();
    return p;
}

void mapped (juce::Graphics& g, const Map& m, const juce::Path& design, juce::Path& out)
{
    // juce::Path::applyTransform takes an AffineTransform, which is the one
    // transform that does not need a Graphics transform stack.
    out = design;
    out.applyTransform (juce::AffineTransform::scale (m.s, m.s).translated (m.ox, m.oy));
    juce::ignoreUnused (g);
}

void fillGradient (juce::Graphics& g, const juce::Path& path, const juce::Rectangle<float>& bounds,
                   juce::Colour top, juce::Colour bottom)
{
    const juce::ColourGradient grad (top, bounds.getCentreX(), bounds.getY(), bottom, bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (path);
}

void outline (juce::Graphics& g, const juce::Path& path, juce::Colour c, float w)
{
    g.strokePath (path, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Fret wires follow the real thing: they get closer together toward the
// bridge. f is the fret number from the nut.
float fretY (int f, float neckLen)
{
    return 40.0f - neckLen * (1.0f - std::pow (0.943f, (float) f));
}

void drawNeck (juce::Graphics& g, const Map& m, float neckW, float neckLen, juce::Colour wood, int frets, bool inlays)
{
    juce::Path neck;
    mapped (g, m, neckPath (neckW * 2.0f, 400.0f - neckW, 40.0f - neckLen, neckLen), neck);
    const auto nb = neck.getBounds();
    fillGradient (g, neck, nb, wood.brighter (0.25f), wood.darker (0.2f));

    if (inlays)
        for (int f = 3; f <= frets; f += 2)
        {
            const float y = fretY (f, neckLen);
            g.setColour (juce::Colour (0xffe8e8e8).withAlpha (0.75f));
            g.fillEllipse (m.R (400.0f - neckW * 0.22f, y - neckW * 0.22f, neckW * 0.44f, neckW * 0.44f));
        }

    for (int f = 1; f <= frets; ++f)
    {
        const auto r = m.R (400.0f - neckW, fretY (f, neckLen), neckW * 2.0f, 1.6f);
        g.setColour (juce::Colour (0xfff0f0f0).withAlpha (0.5f));
        g.fillRect (r);
    }

    const auto nut = m.R (400.0f - neckW, 40.0f - 3.0f, neckW * 2.0f, 3.0f);
    g.setColour (juce::Colour (0xfff2f2f2));
    g.fillRect (nut);
}

void drawHeadstock (juce::Graphics& g, const Map& m, float neckW, juce::Colour wood, int tuners)
{
    juce::Path hs;
    mapped (g, m, headstockPath (neckW), hs);
    const auto hb = m.designBounds (400.0f - neckW * 1.7f, 0.0f, neckW * 3.4f, 40.0f);
    fillGradient (g, hs, hb, wood.brighter (0.2f), wood.darker (0.15f));
    outline (g, hs, juce::Colour (0xff101010).withAlpha (0.5f), m.L (1.0f));

    // Tuner plates down each side, with a post on every plate.
    const float hw = neckW * 0.5f;
    const float plateW = neckW * 0.30f, plateH = 2.6f;
    const float pitch = 30.0f / juce::jmax (1, tuners - 1);
    for (int i = 0; i < tuners; ++i)
    {
        const float y = 6.0f + (float) i * pitch;
        for (int side = -1; side <= 1; side += 2)
        {
            const float px = 400.0f + side * (hw * 1.30f) - plateW * 0.5f;
            g.setColour (juce::Colour (chrome).withAlpha (0.85f));
            g.fillRoundedRectangle (m.R (px, y, plateW, plateH), m.L (0.8f));
            g.setColour (juce::Colour (0xff8b8f93));
            g.fillEllipse (m.R (400.0f + side * (hw * 1.30f) - 1.3f, y + plateH * 0.5f - 1.3f, 2.6f, 2.6f));
        }
    }
}

void drawBridge (juce::Graphics& g, const Map& m, float x, float y, float w, float h)
{
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (m.R (x - w * 0.5f, y - h * 0.5f, w, h), m.L (2.0f));
    g.setColour (juce::Colour (0xffd8d8d8));
    for (int s = 0; s < 6; ++s)
        g.fillRect (m.R (x - w * 0.42f + (float) s * (w * 0.84f / 5.0f) - 0.75f, y - h * 0.5f - 1.0f, 1.5f, h + 2.0f));
}

void drawSingleCoil (juce::Graphics& g, const Map& m, const juce::Rectangle<float>& r)
{
    g.setColour (juce::Colour (0xfff0f0ec));
    g.fillRoundedRectangle (m.designBounds (r.getX(), r.getY(), r.getWidth(), r.getHeight()), m.L (2.0f));
    g.setColour (juce::Colour (0xff1a1a1a));
    for (int p = 0; p < 6; ++p)
        g.fillRect (m.R (r.getX() + 4.0f + (float) p * (r.getWidth() - 8.0f) / 6.0f, r.getY(), 1.5f, r.getHeight()));
}

void drawHumbucker (juce::Graphics& g, const Map& m, const juce::Rectangle<float>& r)
{
    g.setColour (juce::Colour (0xffe8e4dc));
    g.fillRoundedRectangle (m.designBounds (r.getX(), r.getY(), r.getWidth(), r.getHeight()), m.L (3.0f));
    g.setColour (juce::Colour (0xff151515));
    g.fillRoundedRectangle (m.designBounds (r.getX() + 3.0f, r.getY() + 3.0f, r.getWidth() - 6.0f, r.getHeight() - 6.0f), m.L (2.0f));
    g.setColour (juce::Colour (0xfff5c518));
    g.fillRect (m.R (r.getX() + r.getHeight() * 0.5f + 1.0f, r.getCentreY() - 1.0f, r.getWidth() - r.getHeight() - 2.0f, 2.0f));
    g.setColour (juce::Colour (0xffe8e4dc));
    for (int p = 0; p < 6; ++p)
    {
        const float x = r.getX() + r.getHeight() * 0.5f + 2.0f + (float) p * (r.getWidth() - r.getHeight() - 4.0f) / 5.0f;
        g.fillEllipse (m.R (x - 1.5f, r.getCentreY() - 1.5f, 3.0f, 3.0f));
    }
}

void drawKnobs (juce::Graphics& g, const Map& m, float x0, float y0, float dx, float dy, int count, float r)
{
    for (int k = 0; k < count; ++k)
    {
        const float x = x0 + (float) k * dx, y = y0 + (float) k * dy;
        const auto outer = m.R (x - r - 1.0f, y - r - 1.0f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f);
        g.setColour (juce::Colour (0xfff5f5f5).withAlpha (0.2f));
        g.fillEllipse (outer);
        const auto inner = m.R (x - r, y - r, r * 2.0f, r * 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffdfdfd), inner.getCentreX(), inner.getY(),
                                                 juce::Colour (0xff2a2a2a), inner.getCentreX(), inner.getBottom(), false));
        g.fillEllipse (inner);
        g.setColour (juce::Colour (0xffd0d0d0).withAlpha (0.6f));
        g.fillRect (m.R (x - 0.75f, y - r * 0.9f, 1.5f, r * 0.9f));
    }
}

void drawStrings (juce::Graphics& g, const Map& m, float neckW, float nutY, float bridgeY, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const float t = (float) i / (float) (count - 1);
        const float x = 400.0f + (t - 0.5f) * neckW * 1.45f;
        // Wound strings at the bass end are visibly thicker than the trebles.
        const float lw = neckW * (0.035f + t * 0.030f);
        g.setColour (juce::Colour (0xffd8dce0).withAlpha (0.75f));
        juce::Path line;
        line.startNewSubPath (m.P (x, nutY));
        line.lineTo (m.P (x, bridgeY));
        g.strokePath (line, juce::PathStrokeType (m.L (lw)));
    }
}

// Every top is a warm red-brown; the bottoms differ so the four bodies read
// apart at a glance.
juce::Colour bottomColour (GuitarArt::Body body, juce::Colour top)
{
    switch (body)
    {
        case GuitarArt::Body::ClassicalNylon:  return lerpCol (top, juce::Colour (0xff9a6f42), 0.45f);
        case GuitarArt::Body::SolidElectric:   return lerpCol (top, juce::Colour (0xff5c140c), 0.6f);
        case GuitarArt::Body::SemiHollow:      return lerpCol (top, juce::Colour (0xff1a1008), 0.5f);
        default:                               return lerpCol (top, sunburst, 0.35f);
    }
}

juce::Colour topColour (GuitarArt::Body body)
{
    switch (body)
    {
        case GuitarArt::Body::ClassicalNylon:  return lerpCol (cherry, maple, 0.55f);
        case GuitarArt::Body::SolidElectric:   return cherryTop;
        case GuitarArt::Body::SemiHollow:      return lerpCol (cherry, sunburst, 0.2f);
        default:                               return lerpCol (cherry, sunburst, 0.1f);
    }
}
} // namespace

//==============================================================================
void GuitarArt::paint (juce::Graphics& g, const juce::Rectangle<float>& area, Body body)
{
    const auto s = juce::jmin (area.getWidth() / kDesignW, area.getHeight() / kDesignH);
    const Map m { area.getCentreX() - kDesignW * s * 0.5f, area.getCentreY() - kDesignH * s * 0.5f, s };

    // Sized in design units (y grows downwards). The body spans bodyTopY..botY
    // and everything else is positioned relative to those, so one change moves
    // the whole instrument consistently.
    const bool nylon = body == Body::ClassicalNylon;
    const bool solid = body == Body::SolidElectric;
    const bool semi = body == Body::SemiHollow;

    const float neckW   = nylon ? 20.0f : 24.0f;
    const float nutY    = 40.0f;
    const float bodyTopY = 120.0f;
    const float botY    = nylon ? 300.0f : solid ? 308.0f : semi ? 312.0f : 316.0f;
    const float waistY  = (bodyTopY + botY) * 0.5f;
    const float bridgePad = 24.0f;   // vertical gap kept clear between pickguard and bridge

    // --- body ---------------------------------------------------------------
    // Figure eight. upper < lower so the lower bout is the widest part, which
    // is what makes the shape read as a guitar rather than a balloon.
    const float upper  = nylon ? 46.0f : solid ? 58.0f : 54.0f;
    const float waist  = nylon ? 30.0f : solid ? 34.0f : 32.0f;
    const float lower  = nylon ? 66.0f : solid ? 62.0f : semi ? 70.0f : 72.0f;
    const float corner = (botY - waistY) * 0.30f;

    const float topR = 400.0f - upper, waistR = 400.0f - waist, botR = 400.0f - lower;
    const float topL = 400.0f + upper, waistL = 400.0f + waist, botL = 400.0f + lower;

    juce::Path design;
    design.startNewSubPath (400.0f, bodyTopY);
    // upper bout: shoulders out to topR, then curve in to the waist
    design.cubicTo (topR + upper * 0.30f, bodyTopY, topR, bodyTopY + upper * 0.30f, topR, bodyTopY + upper);
    design.cubicTo (topR, waistY - (waistY - bodyTopY) * 0.55f, waistR + (topR - waistR) * 0.55f, waistY - 20.0f, waistR, waistY);
    // lower bout: swell out to botR, round the bottom
    design.cubicTo (waistR + (botR - waistR) * 0.55f, waistY + 26.0f, botR, waistY + 44.0f, botR, waistY + (botY - waistY) * 0.62f);
    design.cubicTo (botR, botY - corner, botR - corner, botY, 400.0f, botY);
    design.cubicTo (400.0f + corner, botY, botL, botY - corner, botL, waistY + (botY - waistY) * 0.62f);
    design.cubicTo (botL, waistY + 44.0f, waistL - (botL - waistL) * 0.55f, waistY + 26.0f, waistL, waistY);
    design.cubicTo (waistL, waistY - 20.0f, topL, waistY - (waistY - bodyTopY) * 0.55f, topL, bodyTopY + upper);
    design.cubicTo (topL, bodyTopY + upper * 0.30f, topL - upper * 0.30f, bodyTopY, 400.0f, bodyTopY);
    design.closeSubPath();

    juce::Path p;
    mapped (g, m, design, p);
    const auto top = topColour (body);
    fillGradient (g, p, p.getBounds(), top, bottomColour (body, top));
    outline (g, p, juce::Colours::white.withAlpha (0.16f), m.L (1.6f));
    outline (g, p, juce::Colour (0xff101010).withAlpha (0.55f), m.L (1.2f));

    // --- neck and headstock, behind the body edge at the joint --------------
    const auto neckWood = nylon ? maple : rosewood;
    const float neckLen = bodyTopY + 18.0f - nutY;
    drawNeck (g, m, neckW, neckLen, neckWood, nylon ? 18 : 21, ! nylon);
    drawHeadstock (g, m, neckW, neckWood, nylon ? 3 : 6);

    // --- fittings on the body ------------------------------------------------
    if (! solid && ! semi)
    {
        const float holeR = nylon ? 18.0f : 21.0f;
        const float hy = waistY - 6.0f;
        g.setColour (juce::Colour (0xff080808));
        g.fillEllipse (m.R (400.0f - holeR, hy - holeR, holeR * 2.0f, holeR * 2.0f));
        g.setColour (nylon ? mapleDark : rosewood);
        g.drawEllipse (m.R (400.0f - holeR * 1.45f, hy - holeR * 1.45f, holeR * 2.9f, holeR * 2.9f), m.L (3.0f));

        // Pickguard wraps the bass side of the soundhole, its outer edge kept
        // inside the widest point of the upper bout so it cannot spill.
        const float pgR = 400.0f - holeR - 4.0f;
        const float pgW = juce::jmax (14.0f, pgR - topR + 6.0f);
        const float pgH = (botY - bridgePad - hy) * 0.92f;
        g.setColour (pickguardW.withAlpha (0.88f));
        g.fillRoundedRectangle (m.R (pgR - pgW, hy + holeR * 0.35f, pgW, pgH), m.L (pgW * 0.5f));
        juce::Path pgEdge;
        pgEdge.addRoundedRectangle (m.R (pgR - pgW, hy + holeR * 0.35f, pgW, pgH), m.L (pgW * 0.5f));
        outline (g, pgEdge, juce::Colour (0xff000000).withAlpha (0.22f), m.L (0.8f));

    }
    else if (solid)
    {
        // three single coils, staggered like a Strat
        drawSingleCoil (g, m, { 400.0f - 20.0f, waistY - 30.0f, 40.0f, 13.0f });
        drawSingleCoil (g, m, { 400.0f - 24.0f, waistY, 48.0f, 13.0f });
        drawSingleCoil (g, m, { 400.0f - 20.0f, waistY + 30.0f, 40.0f, 13.0f });
        drawKnobs (g, m, 400.0f + lower - 26.0f, waistY + 4.0f, 0.0f, 26.0f, 2, 6.5f);
    }
    else
    {
        // f-holes, humbucker, two knobs
        g.setColour (juce::Colour (0xff120a08));
        for (int side = -1; side <= 1; side += 2)
        {
            const float fx = 400.0f + (float) side * 46.0f;
            juce::Path f;
            f.startNewSubPath (fx, waistY - 34.0f);
            f.cubicTo (fx - side * 12.0f, waistY - 18.0f, fx + side * 14.0f, waistY + 6.0f, fx - side * 3.0f, waistY + 26.0f);
            f.cubicTo (fx - side * 11.0f, waistY + 33.0f, fx - side * 13.0f, waistY + 37.0f, fx - side * 7.0f, waistY + 40.0f);
            f.lineTo (fx - side * 2.0f, waistY + 37.0f);
            f.cubicTo (fx - side * 7.0f, waistY + 33.0f, fx - side * 5.0f, waistY + 27.0f, fx + side * 3.0f, waistY + 18.0f);
            f.cubicTo (fx + side * 12.0f, waistY, fx - side * 5.0f, waistY - 18.0f, fx + side * 3.0f, waistY - 28.0f);
            f.closeSubPath();
            juce::Path out;
            mapped (g, m, f, out);
            g.fillPath (out);
        }
        drawHumbucker (g, m, { 400.0f - 22.0f, waistY + 4.0f, 44.0f, 15.0f });
        drawKnobs (g, m, 400.0f + lower - 24.0f, waistY + 20.0f, 0.0f, 22.0f, 2, 6.0f);
    }

    const float bridgeY = (solid || semi) ? botY - 22.0f : botY - 26.0f;
    drawBridge (g, m, 400.0f, bridgeY, (solid || semi) ? 66.0f : 74.0f, (solid || semi) ? 10.0f : 12.0f);

    // --- strings last, so they run over the pickups to the bridge -------------
    drawStrings (g, m, neckW, nutY, bridgeY, 6);
}

} // namespace fw::ui

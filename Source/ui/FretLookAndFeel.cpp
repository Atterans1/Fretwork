#include "FretLookAndFeel.h"
#include "AssetCache.h"

#include <cmath>

namespace fw::ui
{

//==============================================================================
FretLookAndFeel::RotaryGeometry FretLookAndFeel::rotaryGeometry (float sliderPos,
                                                                  float startAngleRadians,
                                                                  float endAngleRadians)
{
    const auto p = juce::jlimit (0.0f, 1.0f, sliderPos);

    // A Slider can hand back a zero-width range, which leaves the pointer with
    // no direction at all, so fall back to the conventional 270-degree sweep.
    // These constants are RADIANS, because that is what JUCE passes in.
    const float rawSpan = endAngleRadians - startAngleRadians;
    const float absSpan = rawSpan < 0.0f ? -rawSpan : rawSpan;
    const bool  usable = absSpan >= 0.05f;      // ~3 degrees

    const float sweep = usable ? rawSpan : 1.5f * juce::MathConstants<float>::pi;
    const float start = usable ? startAngleRadians : 0.75f * juce::MathConstants<float>::pi;

    // No conversion here: JUCE's rotaryStartAngle/rotaryEndAngle are already
    // radians. Treating them as degrees made the whole sweep span about six
    // degrees instead of a full turn.
    const auto theta = start + sweep * p;

    // Angles run clockwise from the top, so the pointer direction is
    // (sin t, -cos t) in screen coordinates where y grows downwards.
    RotaryGeometry out {};
    out.dirX = (float) std::sin (theta);
    out.dirY = (float) -std::cos (theta);
    out.artworkRotationDeg = (float) juce::radiansToDegrees (theta);
    return out;
}

FretLookAndFeel::KnobTier FretLookAndFeel::knobCapForRadius (float radius)
{
    if (radius < 9.0f)          // too small for either bitmap: draw the vector
        return KnobTier::none;

    if (radius < 16.0f)         // the micro-knob row sits around radius 10.5
        return KnobTier::dark;

    return KnobTier::chrome;    // the main knobs sit around radius 29
}

FretLookAndFeel::FretLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);

    setColour (juce::Label::textColourId,       Palette::text);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TextButton::buttonColourId,    Palette::panelHi);
    setColour (juce::TextButton::buttonOnColourId, Palette::accent);
    setColour (juce::TextButton::textColourOffId,  Palette::text);
    setColour (juce::TextButton::textColourOnId,   Palette::background);

    setColour (juce::ComboBox::backgroundColourId, Palette::panelHi);
    setColour (juce::ComboBox::textColourId,       Palette::text);
    setColour (juce::ComboBox::outlineColourId,    Palette::line);
    setColour (juce::ComboBox::arrowColourId,      Palette::dim);

    setColour (juce::PopupMenu::backgroundColourId,            Palette::panel);
    setColour (juce::PopupMenu::textColourId,                  Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::panelHi);
    setColour (juce::PopupMenu::highlightedTextColourId,       Palette::text);

    setColour (juce::Slider::rotarySliderFillColourId,    Palette::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, Palette::line);
    setColour (juce::Slider::thumbColourId,               Palette::text);

    setColour (juce::ToggleButton::textColourId,  Palette::text);
    setColour (juce::ToggleButton::tickColourId,  Palette::accent);

    // JUCE 8 TabbedButtonBar exposes exactly these four tab colour ids.
    setColour (juce::TabbedButtonBar::tabOutlineColourId,   Palette::line);
    setColour (juce::TabbedButtonBar::tabTextColourId,      Palette::dim);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, Palette::accent);
    setColour (juce::TabbedButtonBar::frontTextColourId,    Palette::accent);
}

//==============================================================================
void FretLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                            .reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    if (radius <= 2.0f)
        return;

    

    const auto centre = bounds.getCentre();

    // The track is a full ring, drawn with drawEllipse. It has no angular
    // dependence, which matters here: every angular formulation tried
    // (Path::addCentredArc, a 49-point polyline, explicit drawLine segments,
    // and a zero-span guard on the rotary angles) collapsed to a single point
    // for this Slider style, so the arc simply never appeared.
    g.setColour (Palette::dim.withAlpha (0.55f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);

    const auto geo = rotaryGeometry (sliderPos, rotaryStartAngle, rotaryEndAngle);
    const float c = geo.dirX, s = geo.dirY;

    // Large knobs get the chrome cap; the small ones get the black cap with
    // its silver rim, which has far more edge contrast against the dark panel
    // and so tells the two tiers apart without reading the captions. Below the
    // bitmap threshold neither reads and the vector pointer takes over.
    const auto tier = knobCapForRadius (radius);
    const auto& assets = AssetCache::get();
    const juce::Image* cap = tier == KnobTier::dark   ? &assets.knobDark()
                          : tier == KnobTier::chrome ? &assets.knobChrome()
                                                    : nullptr;

    if (cap != nullptr && ! cap->isValid())
        cap = nullptr;

    if (cap != nullptr)
    {
        const auto artworkRotation = juce::degreesToRadians (geo.artworkRotationDeg);

        juce::Graphics::ScopedSaveState saved (g);
        g.addTransform (juce::AffineTransform::rotation (artworkRotation, centre.x, centre.y));
        g.drawImage (*cap,
                     juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }
    else
    {
        g.setColour (slider.isEnabled() ? Palette::text : Palette::dim);
        g.drawLine (centre.x + c * radius * 0.30f, centre.y + s * radius * 0.30f,
                    centre.x + c * radius * 0.86f, centre.y + s * radius * 0.86f,
                    2.0f);
    }

    // Value dot at the pointer tip, drawn over either path: the dot reads the
    // value on its own, so the knob stays legible without an arc fill.
    const float dotR = juce::jmax (2.5f, radius * 0.11f);
    g.setColour (slider.isEnabled() ? Palette::accent : Palette::dim.withAlpha (0.45f));
    g.fillEllipse (centre.x + c * radius * 0.86f - dotR, centre.y + s * radius * 0.86f - dotR,
                   dotR * 2.0f, dotR * 2.0f);
}

//==============================================================================
void FretLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const float cx = (float) width - 12.0f;
    const float cy = (float) height * 0.5f;

    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (juce::Point<float> (cx, cy + 2.5f));
    chevron.lineTo (juce::Point<float> (cx + 4.0f, cy - 2.0f));
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

//==============================================================================
void FretLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                         bool isSeparator, bool, bool isHighlighted, bool, bool,
                                         const juce::String& text, const juce::String&,
                                         const juce::Drawable*, const juce::Colour*)
{
    if (area.isEmpty() || isSeparator)
        return;

    if (isHighlighted)
    {
        g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRect (area.reduced (2, 1));
    }

    g.setFont (getPopupMenuFont());
    g.setColour (isHighlighted ? findColour (juce::PopupMenu::highlightedTextColourId)
                               : findColour (juce::PopupMenu::textColourId));
    g.drawFittedText (text, area.reduced (10, 0), juce::Justification::centredLeft, 1);
}

//==============================================================================
void FretLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    const auto r = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    g.setColour (on ? Palette::accent : Palette::panelHi);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    g.setFont (Palette::font (11.0f));
    g.setColour (on ? Palette::background : Palette::text);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 0),
                      juce::Justification::centred, 1);
}

//==============================================================================
void FretLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool, bool shouldDrawButtonAsDown)
{
    const auto r = button.getLocalBounds().toFloat().reduced (0.5f);

    auto c = button.getToggleState() ? Palette::accent : backgroundColour;
    if (shouldDrawButtonAsDown)
        c = c.brighter (0.14f);

    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

//==============================================================================
void FretLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                                     bool, bool)
{
    auto area = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool front = button.isFrontTab();

    g.setColour (front ? Palette::panelHi : Palette::panel);
    g.fillRect (area);

    g.setFont (getTabButtonFont (button, (float) button.getHeight()));
    g.setColour (front ? findColour (juce::TabbedButtonBar::frontTextColourId)
                       : findColour (juce::TabbedButtonBar::tabTextColourId));
    g.drawText (button.getButtonText(), area.reduced (6.0f, 0.0f),
                juce::Justification::centred, true);

    if (front)
    {
        g.setColour (findColour (juce::TabbedButtonBar::frontOutlineColourId));
        g.fillRect (area.removeFromBottom (2.0f));
    }
    else
    {
        g.setColour (findColour (juce::TabbedButtonBar::tabOutlineColourId));
        g.fillRect (area.removeFromBottom (1.0f));
    }
}

//==============================================================================
juce::Font FretLookAndFeel::getTabButtonFont (juce::TabBarButton&, float)
{
    return Palette::font (11.0f, juce::Font::bold);
}

//==============================================================================
void FretLookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.setColour (Palette::panel);
    g.fillRect (bar.getLocalBounds());
}

juce::Font FretLookAndFeel::getLabelFont (juce::Label&)
{
    return Palette::font (11.0f);
}

juce::Font FretLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return Palette::font ((float) comboBoxFontHeight - 6.0f);
}

juce::Font FretLookAndFeel::getTextButtonFont (juce::TextButton& button, int)
{
    if (auto* fb = dynamic_cast<FretButton*> (&button))
        return fb->getFretFont();

    return Palette::font (11.0f);
}

juce::Font FretLookAndFeel::getPopupMenuFont()
{
    return Palette::font (13.0f);
}

} // namespace fw::ui

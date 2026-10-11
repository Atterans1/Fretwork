#include "FretLookAndFeel.h"
#include "WavesStyle.h"

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
    if (radius < 9.0f)          // too small for a cap: ring and pointer only
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
    setColour (juce::Label::outlineColourId,    juce::Colours::transparentBlack);

    setColour (juce::TextButton::buttonColourId,   Palette::panelHi);
    setColour (juce::TextButton::buttonOnColourId, Palette::accent);
    setColour (juce::TextButton::textColourOffId,  Palette::text);
    setColour (juce::TextButton::textColourOnId,   juce::Colours::white);

    setColour (juce::ComboBox::backgroundColourId, Palette::lcd);
    setColour (juce::ComboBox::textColourId,       Palette::lcdText);
    setColour (juce::ComboBox::outlineColourId,    juce::Colours::black);
    setColour (juce::ComboBox::arrowColourId,      Palette::caption);

    setColour (juce::PopupMenu::backgroundColourId,            Palette::lcd);
    setColour (juce::PopupMenu::textColourId,                  Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.28f));
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

    setColour (juce::Slider::rotarySliderFillColourId,    Palette::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, Palette::unlit);
    setColour (juce::Slider::thumbColourId,               Palette::text);
    setColour (juce::Slider::trackColourId,               Palette::accent);
    setColour (juce::Slider::backgroundColourId,          Palette::lcd);
    setColour (juce::Slider::textBoxTextColourId,         Palette::text);
    setColour (juce::Slider::textBoxBackgroundColourId,   Palette::lcd);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::black);

    setColour (juce::ToggleButton::textColourId,  Palette::text);
    setColour (juce::ToggleButton::tickColourId,  Palette::accent);

    setColour (juce::TabbedButtonBar::tabOutlineColourId,   juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabTextColourId,      Palette::caption);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, Palette::accent);
    setColour (juce::TabbedButtonBar::frontTextColourId,    Palette::text);

    setColour (juce::ScrollBar::thumbColourId,      Palette::panelHi);
    setColour (juce::ScrollBar::trackColourId,      Palette::lcd);
    setColour (juce::ScrollBar::backgroundColourId, Palette::lcd);

    setColour (juce::TextEditor::backgroundColourId,     Palette::lcd);
    setColour (juce::TextEditor::textColourId,           Palette::text);
    setColour (juce::TextEditor::highlightColourId,      Palette::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextEditor::outlineColourId,        juce::Colours::black);
    setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
    setColour (juce::CaretComponent::caretColourId,      Palette::accent);
}

//==============================================================================
// Atlas knob: LED dot ring around a glossy dark cap with a light pointer
// notch; the amber dot on the ring marks the value.
void FretLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                            .reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    if (radius <= 2.0f)
        return;

    const auto centre  = bounds.getCentre();
    const bool enabled = slider.isEnabled();
    const bool hover   = slider.isMouseOverOrDragging() && enabled;
    const auto geo     = rotaryGeometry (sliderPos, rotaryStartAngle, rotaryEndAngle);
    const float angle  = juce::degreesToRadians (geo.artworkRotationDeg);

    const float span = std::abs (rotaryEndAngle - rotaryStartAngle);
    const float start = span >= 0.05f ? rotaryStartAngle : 0.75f * juce::MathConstants<float>::pi;
    const float end   = span >= 0.05f ? rotaryEndAngle   : 2.25f * juce::MathConstants<float>::pi;

    const auto tier = knobCapForRadius (radius);

    juce::Graphics::ScopedSaveState s (g);
    if (! enabled) g.setOpacity (0.45f);

    waves::ledRing (g, centre, radius * 0.9f, start, end, sliderPos, enabled);

    if (tier == KnobTier::none)
    {
        g.setColour (enabled ? Palette::text : Palette::dim);
        g.drawLine (centre.x + geo.dirX * radius * 0.15f, centre.y + geo.dirY * radius * 0.15f,
                    centre.x + geo.dirX * radius * 0.7f,  centre.y + geo.dirY * radius * 0.7f, 1.6f);
        return;
    }

    const float capR = radius * (tier == KnobTier::chrome ? 0.70f : 0.66f);
    waves::knobCap (g, centre, capR, angle, tier == KnobTier::chrome, enabled);

    if (hover)
    {
        g.setColour (Palette::accent.withAlpha (0.35f));
        g.drawEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre).expanded (1.5f), 1.2f);
    }
}

//==============================================================================
// Waves Tune fader: an inset slot with tick marks, accent fill up to the
// value and a ribbed dark thumb.
void FretLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool vertical = style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical;
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const bool enabled = slider.isEnabled();
    const bool hover = slider.isMouseOverOrDragging() && enabled;

    juce::Graphics::ScopedSaveState s (g);
    if (! enabled) g.setOpacity (0.45f);

    const float slotT = 5.0f;
    const auto slot = vertical ? area.withSizeKeepingCentre (slotT, area.getHeight() - 8.0f)
                               : area.withSizeKeepingCentre (area.getWidth() - 8.0f, slotT);
    waves::lcd (g, slot, 2.0f);

    // ticks
    g.setColour (Palette::line);
    for (int i = 0; i <= 8; ++i)
    {
        const float t = (float) i / 8.0f;
        if (vertical)
        {
            const float ty = slot.getBottom() - t * slot.getHeight();
            g.drawHorizontalLine ((int) ty, slot.getRight() + 3.0f, slot.getRight() + (i % 4 == 0 ? 8.0f : 5.0f));
        }
        else
        {
            const float tx = slot.getX() + t * slot.getWidth();
            g.drawVerticalLine ((int) tx, slot.getBottom() + 3.0f, slot.getBottom() + (i % 4 == 0 ? 8.0f : 5.0f));
        }
    }

    // fill
    const auto fill = vertical ? slot.withTop (juce::jlimit (slot.getY(), slot.getBottom(), sliderPos))
                               : slot.withRight (juce::jlimit (slot.getX(), slot.getRight(), sliderPos));
    g.setColour (enabled ? Palette::accent : Palette::dim);
    g.fillRoundedRectangle (fill.reduced (1.0f), 1.5f);

    // thumb
    const auto thumb = vertical ? juce::Rectangle<float> (18.0f, 11.0f).withCentre ({ slot.getCentreX(), sliderPos })
                                : juce::Rectangle<float> (11.0f, 18.0f).withCentre ({ sliderPos, slot.getCentreY() });
    waves::buttonFace (g, thumb, false, hover, slider.isMouseButtonDown(), true, 2.0f);
    g.setColour (Palette::text.withAlpha (0.8f));
    if (vertical)
        g.drawHorizontalLine ((int) thumb.getCentreY(), thumb.getX() + 3.0f, thumb.getRight() - 3.0f);
    else
        g.drawVerticalLine ((int) thumb.getCentreX(), thumb.getY() + 3.0f, thumb.getBottom() - 3.0f);
}

//==============================================================================
// LCD selector: black well, glowing text, separate arrow cell (Waves Tune).
void FretLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const bool enabled = box.isEnabled();
    const bool hover = box.isMouseOver (true) && enabled;

    juce::Graphics::ScopedSaveState s (g);
    if (! enabled) g.setOpacity (0.45f);

    waves::lcd (g, r, 3.0f);

    const float cellW = juce::jmin (22.0f, (float) height);
    const auto cell = r.withLeft (r.getRight() - cellW).reduced (1.0f);
    waves::buttonFace (g, cell, false, hover, isButtonDown, true, 2.5f);

    if (box.hasKeyboardFocus (true) || hover)
    {
        g.setColour (Palette::accent.withAlpha (hover ? 0.6f : 0.4f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }

    juce::Path tri;
    const auto c = cell.getCentre();
    tri.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).brighter (hover ? 0.4f : 0.0f));
    g.fillPath (tri);
}

void FretLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const int cellW = juce::jmin (22, box.getHeight());
    label.setBounds (6, 1, box.getWidth() - cellW - 8, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

//==============================================================================
void FretLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.fillAll (Palette::lcd);
    g.setColour (Palette::line);
    g.drawRect (r, 1.0f);
}

void FretLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                         bool isSeparator, bool isActive, bool isHighlighted,
                                         bool isTicked, bool, const juce::String& text,
                                         const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (area.isEmpty())
        return;

    if (isSeparator)
    {
        g.setColour (Palette::line);
        g.drawHorizontalLine (area.getCentreY(), (float) area.getX() + 8.0f, (float) area.getRight() - 8.0f);
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRect (area.reduced (2, 1));
        g.setColour (Palette::accent);
        g.fillRect (area.getX() + 2, area.getY() + 1, 2, area.getHeight() - 2);
    }

    if (isTicked)
    {
        g.setColour (Palette::accent);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) area.getX() + 12.0f, (float) area.getCentreY() }));
    }

    g.setFont (getPopupMenuFont());
    g.setColour (! isActive ? Palette::dim
                            : isHighlighted ? findColour (juce::PopupMenu::highlightedTextColourId)
                                            : (isTicked ? Palette::lcdText : findColour (juce::PopupMenu::textColourId)));
    g.drawFittedText (text, area.withTrimmedLeft (22).withTrimmedRight (8), juce::Justification::centredLeft, 1);
}

//==============================================================================
// Waves Tune "ON" switch: a small push-button lit with the accent when on.
void FretLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                        bool highlighted, bool down)
{
    const auto r = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    waves::buttonFace (g, r, on, highlighted, down, button.isEnabled());

    juce::Graphics::ScopedSaveState s (g);
    if (! button.isEnabled()) g.setOpacity (0.4f);
    g.setFont (Palette::fontSemi (11.0f));
    g.setColour (on ? juce::Colours::white : Palette::text);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 0),
                      juce::Justification::centred, 1);
}

//==============================================================================
void FretLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour&, bool highlighted, bool down)
{
    waves::buttonFace (g, button.getLocalBounds().toFloat().reduced (0.5f),
                       button.getToggleState(), highlighted, down, button.isEnabled());
}

void FretLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool)
{
    const bool on = button.getToggleState();
    auto colour = on ? juce::Colours::white
                     : button.findColour (juce::TextButton::textColourOffId);

    // Off text sits a step back from fully lit, and lifts on hover.
    if (! on) colour = colour.withMultipliedAlpha (highlighted ? 1.0f : 0.82f);
    if (! button.isEnabled()) colour = colour.withMultipliedAlpha (0.4f);

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (colour);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 1),
                      juce::Justification::centred, 2, 0.8f);
}

//==============================================================================
// Atlas tabs ("Damp  Space  Ducker  [EQ]"): plain text, the front tab boxed
// with a hairline accent outline.
void FretLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                                     bool isMouseOver, bool isMouseDown)
{
    const auto area = button.getLocalBounds().toFloat().reduced (4.0f, 5.0f);
    const bool front = button.isFrontTab();

    if (front)
    {
        g.setColour (Palette::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (area, 3.0f, 1.2f);
    }
    else if (isMouseOver)
    {
        g.setColour (juce::Colours::white.withAlpha (isMouseDown ? 0.08f : 0.04f));
        g.fillRoundedRectangle (area, 3.0f);
    }

    g.setFont (getTabButtonFont (button, (float) button.getHeight()));
    g.setColour (front ? findColour (juce::TabbedButtonBar::frontTextColourId)
                       : findColour (juce::TabbedButtonBar::tabTextColourId).brighter (isMouseOver ? 0.3f : 0.0f));
    g.drawText (button.getButtonText(), area, juce::Justification::centred, true);
}

juce::Font FretLookAndFeel::getTabButtonFont (juce::TabBarButton&, float)
{
    return Palette::fontSemi (12.0f);
}

void FretLookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    const auto r = bar.getLocalBounds().toFloat();
    g.setColour (Palette::line);
    g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX() + 4.0f, r.getRight() - 4.0f);
}

//==============================================================================
void FretLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                     bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    const auto track = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    waves::lcd (g, vertical ? track.reduced (2.0f, 0.0f) : track.reduced (0.0f, 2.0f), 2.0f);

    const auto thumb = vertical ? juce::Rectangle<float> ((float) x, (float) thumbStart, (float) width, (float) thumbSize)
                                : juce::Rectangle<float> ((float) thumbStart, (float) y, (float) thumbSize, (float) height);
    if (thumbSize > 0)
        waves::buttonFace (g, thumb.reduced (1.0f), false, over, down, true, 2.5f);
}

void FretLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    juce::Graphics::ScopedSaveState s (g);
    if (! ed.isEnabled()) g.setOpacity (0.45f);
    waves::lcd (g, juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 3.0f);
}

void FretLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    if (ed.hasKeyboardFocus (true) && ! ed.isReadOnly())
    {
        g.setColour (ed.findColour (juce::TextEditor::focusedOutlineColourId));
        g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f), 3.0f, 1.2f);
    }
}

void FretLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));

    if (label.isBeingEdited())
        return;

    auto colour = label.findColour (juce::Label::textColourId);
    if (! label.isEnabled()) colour = colour.withMultipliedAlpha (0.45f);

    g.setColour (colour);
    g.setFont (getLabelFont (label));
    g.drawFittedText (label.getText(), label.getBorderSize().subtractedFrom (label.getLocalBounds()),
                      label.getJustificationType(),
                      juce::jmax (1, (int) ((float) label.getHeight() / label.getFont().getHeight())),
                      label.getMinimumHorizontalScale());

    const auto outline = label.findColour (juce::Label::outlineColourId);
    if (! outline.isTransparent())
    {
        g.setColour (outline);
        g.drawRect (label.getLocalBounds());
    }
}

//==============================================================================
juce::Font FretLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Font FretLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return Palette::fontSemi ((float) comboBoxFontHeight - 5.0f);
}

juce::Font FretLookAndFeel::getTextButtonFont (juce::TextButton& button, int)
{
    if (auto* fb = dynamic_cast<FretButton*> (&button))
        return fb->getFretFont();

    return Palette::fontSemi (11.0f);
}

juce::Font FretLookAndFeel::getPopupMenuFont()
{
    return Palette::fontSemi (13.0f);
}

} // namespace fw::ui

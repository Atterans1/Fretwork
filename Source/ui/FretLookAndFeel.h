#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fw::ui
{

/** Flat dark palette shared by every Fretwork panel.

    These are flat fills only - the palette itself carries no gradients or
    bevels. Photographic artwork is layered on top elsewhere (knob bitmaps in
    drawRotarySlider, body art in GuitarBodyView, a brushed wash behind the
    editor), so "flat" describes the fills, not the finished UI. */
struct Palette
{
    inline static const juce::Colour background { 0xff1c1e22 };
    inline static const juce::Colour panel      { 0xff25282d };
    inline static const juce::Colour panelHi    { 0xff2d3036 };
    inline static const juce::Colour line       { 0xff3a3e45 };
    inline static const juce::Colour text       { 0xffc8ccd2 };
    inline static const juce::Colour dim        { 0xff7d838c };
    inline static const juce::Colour accent     { 0xff4fb6c2 };
    inline static const juce::Colour accent2    { 0xffb04a8f };
    inline static const juce::Colour accent3    { 0xff7a5cd6 };
    inline static const juce::Colour amber      { 0xffe0a458 };
    inline static const juce::Colour whiteKey   { 0xffd9dce0 };
    inline static const juce::Colour blackKey   { 0xff15171a };

    static juce::Font font (float height, int style = juce::Font::plain)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", height, style));
    }
};


/** JUCE 8 removed Button::setFont(), so a TextButton's font can only come from
    LookAndFeel::getTextButtonFont(). FretButton carries the height it wants and
    FretLookAndFeel::getTextButtonFont() reads it back, which keeps the old
    per-button sizing working. */
class FretButton : public juce::TextButton
{
public:
    explicit FretButton (const juce::String& text = {}) : juce::TextButton (text) {}

    void setFretFont (const juce::Font& f) { fretFont = f; }
    const juce::Font& getFretFont() const noexcept { return fretFont; }

private:
    juce::Font fretFont { Palette::font (11.0f) };
};

/** LookAndFeel used by the whole editor: flat fills, 1px outlines, one accent.

    In JUCE 8 the popup-menu, combobox and rotary drawing hooks live in
    LookAndFeel_V4 (reached through V3/V2), and TabbedButtonBar is drawn through
    the TabBarButton hooks plus its ColourIds - there is no Tab/TextAndOutlineTab
    pair any more. */
class FretLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FretLookAndFeel();

    //==============================================================================
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool,
                       int, int, int, int, juce::ComboBox&) override;

    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool, bool) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool, bool shouldDrawButtonAsDown) override;

    //==============================================================================
    // JUCE 8 replaced getTextButtonLabelBounds() with the TabBarButton-based
    // tab hooks below; button text padding now comes from the button's own
    // component padding, so there is no label-bounds override to write.
    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                        bool isMouseOver, bool isMouseDown) override;

    juce::Font getTabButtonFont (juce::TabBarButton&, float height) override;

    void drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g) override;

    //==============================================================================
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;

    /** Where a rotary control's pointer sits for a given proportional value.

        Exposed so the angle convention can be asserted directly in the headless
        test binary: drawing into a software Image there produces a blank buffer,
        so the rendered result cannot be measured, but the geometry can. */
    struct RotaryGeometry
    {
        float dirX = 0.0f, dirY = 0.0f;   // unit vector along the pointer
        float artworkRotationDeg = 0.0f;  // rotation for artwork pointing up
    };

    /** Which cap artwork a knob of this radius should use.

        Small knobs get the black cap, whose silver rim has far more edge
        contrast against the dark panel than the chrome one, so the two tiers
        read apart without reading captions. Returns nullptr when the knob is
        too small for either bitmap. */
    enum class KnobTier { none, dark, chrome };

    static KnobTier knobCapForRadius (float radius);

    static RotaryGeometry rotaryGeometry (float sliderPos,
                                          float startAngleRadians,
                                          float endAngleRadians);

    //==============================================================================
    static constexpr int popupMenuItemHeight = 26;
    static constexpr int comboBoxFontHeight  = 18;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FretLookAndFeel)
};

} // namespace fw::ui

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fw::ui
{

/** Waves-style palette shared by every Fretwork panel (see
    Docs/waves-ref/DESIGN.md): a charcoal face, black LCD wells, warm off-white
    labels, one violet LED accent with an amber position marker. */
struct Palette
{
    inline static const juce::Colour background { 0xff17181c };   // chassis face
    inline static const juce::Colour frame      { 0xff2b2d33 };   // bevelled outer frame
    inline static const juce::Colour panel      { 0xff1f2126 };   // group fill
    inline static const juce::Colour panelHi    { 0xff2a2d33 };   // raised surfaces
    inline static const juce::Colour line       { 0xff3a3d45 };   // hairline rules
    inline static const juce::Colour lcd        { 0xff0b0c0e };   // LCD / value wells
    inline static const juce::Colour text       { 0xffece7dc };   // warm off-white
    inline static const juce::Colour caption    { 0xffa9adb5 };   // small-caps headers
    inline static const juce::Colour dim        { 0xff80848d };
    inline static const juce::Colour unlit      { 0xff34363d };   // unlit LED dot
    inline static const juce::Colour accent     { 0xff9d72ff };   // violet LED
    inline static const juce::Colour lcdText    { 0xffc6aeff };   // glowing LCD text
    inline static const juce::Colour accent2    { 0xffd23fd6 };   // magenta (Waves Tune)
    inline static const juce::Colour accent3    { 0xff6f8cff };
    inline static const juce::Colour amber      { 0xfff2b45a };   // pointer / peak marker
    inline static const juce::Colour whiteKey   { 0xffe9eaec };
    inline static const juce::Colour blackKey   { 0xff121316 };

    static juce::Font font (float height, int style = juce::Font::plain)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", height, style));
    }

    /** Labels and captions. */
    static juce::Font fontSemi (float height)
    {
        return juce::Font (juce::FontOptions ("Segoe UI Semibold", height, juce::Font::plain));
    }

    /** Numerals in value boxes and readouts. */
    static juce::Font fontNum (float height)
    {
        return juce::Font (juce::FontOptions ("Bahnschrift", height, juce::Font::plain));
    }
};


/** JUCE 8 removed Button::setFont(), so a TextButton's font can only come from
    LookAndFeel::getTextButtonFont(). FretButton carries the height it wants and
    FretLookAndFeel::getTextButtonFont() reads it back. */
class FretButton : public juce::TextButton
{
public:
    explicit FretButton (const juce::String& text = {}) : juce::TextButton (text) {}

    void setFretFont (const juce::Font& f) { fretFont = f; }
    const juce::Font& getFretFont() const noexcept { return fretFont; }

private:
    juce::Font fretFont { Palette::fontSemi (11.0f) };
};

/** LookAndFeel for the whole editor, in the Waves Atlas / Waves Tune style.
    Every widget is vector-drawn and covers default, hover, down, on and
    disabled states. The primitives live in WavesStyle.h. */
class FretLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FretLookAndFeel();

    //==============================================================================
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                        bool isMouseOver, bool isMouseDown) override;
    juce::Font getTabButtonFont (juce::TabBarButton&, float height) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    //==============================================================================
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;

    /** Where a rotary control's pointer sits for a given proportional value.
        Exposed so the angle convention can be asserted in the test binary. */
    struct RotaryGeometry
    {
        float dirX = 0.0f, dirY = 0.0f;   // unit vector along the pointer
        float artworkRotationDeg = 0.0f;  // rotation for artwork pointing up
    };

    /** Which knob cap a knob of this radius gets: none (tiny, ring and pointer
        only), dark (compact cap for the micro-knob rows) or chrome (the large
        glossy cap with a raised skirt for the main knobs). */
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

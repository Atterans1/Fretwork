#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fw::ui
{

/** Guitar artwork for the body display: one AI-generated photo per body,
    shipped as BinaryData (Assets/guitars).

    Each image was generated on pure black and cut out with alpha taken from
    brightness, so every partly transparent edge pixel blends towards black -
    the colour of the LCD well behind it - and cannot leave the grey fringe the
    old keyed PNGs had. Dark parts inside the outline (soundholes, pickguards)
    are forced opaque. */
class GuitarArt final
{
public:
    enum class Body
    {
        DreadnoughtSteel,
        ClassicalNylon,
        SolidElectric,
        SemiHollow
    };

    /** Draws the body's image centred in area, scaled to fit. */
    static void paint (juce::Graphics& g, const juce::Rectangle<float>& area, Body body);
};

} // namespace fw::ui

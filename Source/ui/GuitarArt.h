#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fw::ui
{

/** Vector guitar artwork, drawn in a 0..1 x 0..1 space and letterboxed by the
    caller.

    The bodies used to be keyed PNGs. Every one of them kept its grey drop
    shadow and ate parts of the fretboard in the alpha channel, which showed
    up as grey fringes and holes wherever they were drawn. Vector paths have no
    alpha channel to get wrong, render sharp at any scale, and cost a few
    hundred bytes instead of a quarter-megabyte of BinaryData. */
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

    static void paint (juce::Graphics& g, const juce::Rectangle<float>& area, Body body);
};

} // namespace fw::ui
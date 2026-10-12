#include "GuitarArt.h"

#include <BinaryData.h>

namespace fw::ui
{
namespace
{
juce::Image imageFor (GuitarArt::Body body)
{
    // Looked up in ImageCache on every paint (a hash lookup once decoded) and
    // never held in a static. A function-local static Image is destroyed when
    // the plugin DLL unloads, under the Windows loader lock and after JUCE has
    // shut down; releasing its Direct2D resources there deadlocked the host's
    // exit. ImageCache is freed during JUCE's own shutdown instead.
    switch (body)
    {
        case GuitarArt::Body::DreadnoughtSteel: return juce::ImageCache::getFromMemory (BinaryData::dreadnought_png, BinaryData::dreadnought_pngSize);
        case GuitarArt::Body::ClassicalNylon:   return juce::ImageCache::getFromMemory (BinaryData::classical_png,   BinaryData::classical_pngSize);
        case GuitarArt::Body::SolidElectric:    return juce::ImageCache::getFromMemory (BinaryData::electric_png,    BinaryData::electric_pngSize);
        case GuitarArt::Body::SemiHollow:
        default:                                return juce::ImageCache::getFromMemory (BinaryData::semihollow_png,  BinaryData::semihollow_pngSize);
    }
}
} // namespace

void GuitarArt::paint (juce::Graphics& g, const juce::Rectangle<float>& area, Body body)
{
    const auto img = imageFor (body);

    if (! img.isValid())
        return;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (img, area, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
}

} // namespace fw::ui

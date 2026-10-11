#include "GuitarArt.h"

#include <BinaryData.h>

namespace fw::ui
{
namespace
{
const juce::Image& imageFor (GuitarArt::Body body)
{
    // Decoded once on first use; ImageCache keeps them for the session.
    switch (body)
    {
        case GuitarArt::Body::DreadnoughtSteel:
        {
            static const auto img = juce::ImageCache::getFromMemory (BinaryData::dreadnought_png, BinaryData::dreadnought_pngSize);
            return img;
        }
        case GuitarArt::Body::ClassicalNylon:
        {
            static const auto img = juce::ImageCache::getFromMemory (BinaryData::classical_png, BinaryData::classical_pngSize);
            return img;
        }
        case GuitarArt::Body::SolidElectric:
        {
            static const auto img = juce::ImageCache::getFromMemory (BinaryData::electric_png, BinaryData::electric_pngSize);
            return img;
        }
        case GuitarArt::Body::SemiHollow:
        default:
        {
            static const auto img = juce::ImageCache::getFromMemory (BinaryData::semihollow_png, BinaryData::semihollow_pngSize);
            return img;
        }
    }
}
} // namespace

void GuitarArt::paint (juce::Graphics& g, const juce::Rectangle<float>& area, Body body)
{
    const auto& img = imageFor (body);

    if (! img.isValid())
        return;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (img, area, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
}

} // namespace fw::ui

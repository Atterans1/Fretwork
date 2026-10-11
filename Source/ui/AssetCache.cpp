#include "AssetCache.h"

#include <BinaryData.h>

namespace fw::ui
{

//==============================================================================
juce::Image AssetCache::decode (const char* name, int size)
{
    if (size <= 0)
    {
        jassertfalse;
        return {};
    }

    // JUCE 8 dropped Image::loadFromData; decoding goes through the format
    // objects now. The bytes are already the size they are drawn at, so there
    // is no rescale to do here.
    juce::MemoryInputStream stream (name, (size_t) size, false);
    return juce::PNGImageFormat().decodeImage (stream);
}

AssetCache::AssetCache()
{
    dreadnought_  = decode (BinaryData::guitardreadnought_png, BinaryData::guitardreadnought_pngSize);
    semiHollow_   = decode (BinaryData::guitarsemihollow_png,  BinaryData::guitarsemihollow_pngSize);
    nylon_        = decode (BinaryData::guitarnylon_png,       BinaryData::guitarnylon_pngSize);
    solid_        = decode (BinaryData::guitarsolid_png,        BinaryData::guitarsolid_pngSize);
    panelTexture_ = decode (BinaryData::paneltexture_png,     BinaryData::paneltexture_pngSize);
}

const AssetCache& AssetCache::get()
{
    static const AssetCache instance;
    return instance;
}

} // namespace fw::ui

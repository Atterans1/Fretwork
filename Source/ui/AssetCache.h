#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fw::ui
{

/** Decodes the embedded artwork once and hands out shared copies.

    Every PNG ships as BinaryData rather than a loose file, so the artwork is
    part of the plugin binary and renders identically wherever it is installed.
    Decoding is deferred to first use and cached: a `juce::Image` costs real
    memory, and the editor may be opened and closed repeatedly. */
class AssetCache final
{
public:
    static const AssetCache& get();

    const juce::Image& dreadnought()   const noexcept { return dreadnought_; }
    const juce::Image& semiHollow()    const noexcept { return semiHollow_; }
    const juce::Image& nylon()         const noexcept { return nylon_; }
    const juce::Image& solid()         const noexcept { return solid_; }
    const juce::Image& panelTexture()  const noexcept { return panelTexture_; }

private:
    AssetCache();

    static juce::Image decode (const char* name, int size);

    juce::Image dreadnought_, semiHollow_, nylon_, solid_, panelTexture_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssetCache)
};

} // namespace fw::ui

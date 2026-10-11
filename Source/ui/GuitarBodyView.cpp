#include "GuitarBodyView.h"
#include "AssetCache.h"
#include "WavesStyle.h"

namespace fw::ui
{

GuitarBodyView::GuitarBodyView (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
    state.addParameterListener (fw::id::body, this);
}

GuitarBodyView::~GuitarBodyView()
{
    state.removeParameterListener (fw::id::body, this);
}

void GuitarBodyView::parameterChanged (const juce::String&, float)
{
    // APVTS listeners fire on whichever thread moved the parameter, and host
    // automation or a state restore can be that thread. repaint() is not
    // thread-safe, so hand the work to the message thread.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<GuitarBodyView> (this)]
    {
        if (safe != nullptr)
            safe->repaint();
    });
}

void GuitarBodyView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    // A black LCD display window, with a faint spotlight behind the art so the
    // guitar reads as lit inside the well rather than pasted onto it.
    waves::lcd (g, area, 4.0f);

    g.setGradientFill (juce::ColourGradient (Palette::accent.withAlpha (0.10f), area.getCentreX(), area.getCentreY(),
                                             juce::Colours::transparentBlack, area.getCentreX(), area.getY() - area.getHeight() * 0.35f,
                                             true));
    g.fillRoundedRectangle (area.reduced (1.0f), 4.0f);

    const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (state.getParameter (fw::id::body));

    if (choice == nullptr)
        return;

    const int index = juce::jlimit (0, (int) choice->choices.size() - 1, choice->getIndex());

    const auto& assets = AssetCache::get();
    const juce::Image* art = nullptr;
    switch (index)
    {
        case 0:  art = &assets.dreadnought(); break;   // Dreadnought Steel
        case 1:  art = &assets.nylon();      break;   // Classical Nylon
        case 2:  art = &assets.solid();      break;   // Solid Electric
        case 3:  art = &assets.semiHollow(); break;   // Semi-Hollow
        default: break;
    }

    if (art == nullptr || ! art->isValid())
    {
        // No artwork for this body. Say which one is selected rather than
        // leaving the panel blank, which reads as a loading failure.
        g.setColour (Palette::lcdText);
        g.setFont (Palette::fontSemi (14.0f).withExtraKerningFactor (0.06f));
        g.drawText (choice->choices[index].toUpperCase(), area.reduced (10.0f, 0.0f),
                    juce::Justification::centred, true);
        return;
    }

    // Letterbox on whichever axis runs out first, so the whole guitar is always
    // visible rather than cropped to fill.
    const auto inner = area.reduced (10.0f, 8.0f);
    const auto scale = juce::jmin (inner.getWidth()  / (float) art->getWidth(),
                                   inner.getHeight() / (float) art->getHeight());
    const auto size = juce::Point<float> ((float) art->getWidth(), (float) art->getHeight()) * scale;

    g.drawImage (*art, inner.withSizeKeepingCentre (size.x, size.y));
}

} // namespace fw::ui

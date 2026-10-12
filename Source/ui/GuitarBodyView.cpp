#include "GuitarBodyView.h"
#include "GuitarArt.h"
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

    static constexpr GuitarArt::Body bodies[] { GuitarArt::Body::DreadnoughtSteel,   // Dreadnought Steel
                                                GuitarArt::Body::ClassicalNylon,    // Classical Nylon
                                                GuitarArt::Body::SolidElectric,     // Solid Electric
                                                GuitarArt::Body::SemiHollow };      // Semi-Hollow
    // Name caption along the bottom of the well, as a display label; the art
    // fills the rest so the two never overlap.
    auto inner = area.reduced (8.0f, 4.0f);
    const auto captionArea = inner.removeFromBottom (14.0f);
    GuitarArt::paint (g, inner, bodies[index]);

    g.setColour (Palette::caption.withAlpha (0.9f));
    g.setFont (Palette::fontSemi (9.0f).withExtraKerningFactor (0.1f));
    g.drawText (choice->choices[index].toUpperCase(), captionArea, juce::Justification::centred, true);
}

} // namespace fw::ui

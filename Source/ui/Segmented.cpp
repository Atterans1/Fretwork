#include "Segmented.h"

#include "WavesStyle.h"

namespace fw::ui
{

Segmented::Segmented()
{
    proxy.setVisible (false);
    proxy.setInterceptsMouseClicks (false, false);
    addChildComponent (proxy);
}

Segmented::~Segmented() = default;

// Waves Tune key-row button ("A B C D E F G"): black face, white semibold text,
// violet when selected. Faces and every state come from FretLookAndFeel's
// drawButtonBackground / drawButtonText (waves::buttonFace).
void Segmented::styleSegment (FretButton& seg)
{
    seg.setFretFont (Palette::fontSemi (11.0f));
    seg.setColour (juce::TextButton::buttonColourId,   Palette::blackKey);
    seg.setColour (juce::TextButton::buttonOnColourId, Palette::accent);
    seg.setColour (juce::TextButton::textColourOffId,  Palette::text);
    seg.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
}

juce::Rectangle<int> Segmented::trayBounds() const
{
    return getLocalBounds().withTrimmedTop (kCaptionH);
}

void Segmented::setCaption (const juce::String& text)
{
    caption = text;
    repaint();
}

void Segmented::addChoices (juce::AudioProcessorValueTreeState& state, const char* paramId)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));

    if (p == nullptr || choiceParam != nullptr || flagParam != nullptr)
        return;

    choiceParam = p;

    proxy.clear (juce::dontSendNotification);
    for (int i = 0; i < p->choices.size(); ++i)
        proxy.addItem (p->choices[i], i + 1);

    choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, proxy);

    const int n = p->choices.size();
    selected = juce::jlimit (0, juce::jmax (0, n - 1), (int) p->getIndex());

    segments.clear();
    segments.reserve ((std::size_t) n);

    for (int i = 0; i < n; ++i)
        segments.push_back (std::make_unique<FretButton>());

    for (int i = 0; i < n; ++i)
    {
        auto* seg = segments[(std::size_t) i].get();
        seg->setButtonText (p->choices[i].toUpperCase());
        seg->setClickingTogglesState (false);
        styleSegment (*seg);
        seg->onClick = [this, i]
        {
            if (proxy.getSelectedId() != i + 1)
                proxy.setSelectedId (i + 1, juce::sendNotification);

            selected = i;
            syncButtons();
        };

        addAndMakeVisible (seg);
    }

    syncButtons();
    resized();
}

void Segmented::addFlag (juce::AudioProcessorValueTreeState& state, const char* paramId)
{
    auto* p = dynamic_cast<juce::AudioParameterBool*> (state.getParameter (paramId));

    if (p == nullptr || choiceParam != nullptr || flagParam != nullptr)
        return;

    flagParam = p;
    segments.clear();
    segments.push_back (std::make_unique<FretButton>());

    auto* seg = segments[0].get();
    seg->setButtonText (juce::String (paramId).toUpperCase());
    seg->setClickingTogglesState (true);
    styleSegment (*seg);

    flagAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *seg);
    seg->onClick = [this] { syncButtons(); };

    addAndMakeVisible (seg);
    syncButtons();
    resized();
}

void Segmented::refresh()
{
    if (choiceParam != nullptr)
    {
        const int i = juce::jlimit (0, (int) segments.size() - 1, (int) choiceParam->getIndex());
        if (i != selected)
        {
            selected = i;
            syncButtons();
        }
    }
    else if (flagParam != nullptr)
    {
        if (segments.front()->getToggleState() != flagParam->get())
            syncButtons();
    }
}

void Segmented::syncButtons()
{
    // A flag mirrors its bool parameter. Driving it from the choice index
    // (always 0 here) kept Hold lit regardless of the parameter, and every
    // click was immediately undone, so Hold could never be switched on.
    if (flagParam != nullptr)
    {
        segments.front()->setToggleState (flagParam->get(), juce::dontSendNotification);
        return;
    }

    for (std::size_t i = 0; i < segments.size(); ++i)
        segments[i]->setToggleState ((int) i == selected, juce::dontSendNotification);
}

void Segmented::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto captionColour = isEnabled() ? Palette::caption : Palette::dim.withAlpha (0.6f);

    waves::caption (g, b.withHeight ((float) kCaptionH).withTrimmedLeft (2.0f),
                    caption, juce::Justification::centredLeft, captionColour);

    if (segments.empty())
        return;

    // Black recessed tray; the segments sit inside it 1 px apart so the group
    // reads as one joined row of buttons.
    waves::lcd (g, trayBounds().toFloat(), 3.0f);
}

void Segmented::resized()
{
    proxy.setBounds (0, 0, 1, 1);

    if (segments.empty())
        return;

    const auto row = trayBounds().reduced (kTrayInset);

    const int n = (int) segments.size();
    const auto w = (float) (row.getWidth() - kSegmentGap * (n - 1)) / (float) n;

    for (int i = 0; i < n; ++i)
    {
        const int x0 = juce::roundToInt ((float) i * w);
        const int x1 = juce::roundToInt ((float) (i + 1) * w);

        segments[(std::size_t) i]->setBounds (row.getX() + x0 + i * kSegmentGap,
                                              row.getY(),
                                              x1 - x0,
                                              row.getHeight());
    }
}

} // namespace fw::ui

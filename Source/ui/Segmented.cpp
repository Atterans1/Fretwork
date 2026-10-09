#include "Segmented.h"

namespace fw::ui
{

Segmented::Segmented()
{
    proxy.setVisible (false);
    proxy.setInterceptsMouseClicks (false, false);
    addChildComponent (proxy);
}

Segmented::~Segmented() = default;

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
        seg->setFretFont (Palette::font (11.0f));
        seg->setColour (juce::TextButton::buttonColourId,      juce::Colours::transparentBlack);
        seg->setColour (juce::TextButton::textColourOffId,     Palette::dim);
        seg->setColour (juce::TextButton::buttonOnColourId,    Palette::accent);
        seg->setColour (juce::TextButton::textColourOnId,      Palette::background);
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
    seg->setFretFont (Palette::font (11.0f));
    seg->setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    seg->setColour (juce::TextButton::textColourOffId,  Palette::dim);
    seg->setColour (juce::TextButton::buttonOnColourId, Palette::accent);
    seg->setColour (juce::TextButton::textColourOnId,   Palette::background);

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
    for (std::size_t i = 0; i < segments.size(); ++i)
        segments[i]->setToggleState ((int) i == selected, juce::dontSendNotification);
}

void Segmented::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour (Palette::dim);
    g.setFont (Palette::font (11.0f));
    g.drawText (caption, b.removeFromTop (18.0f).reduced (0, 3),
                juce::Justification::centredLeft, true);

    if (segments.empty())
        return;

    const auto pill = b.withTrimmedTop (18.0f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (pill, 6.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (pill.reduced (0.5f), 6.0f, 1.0f);
}

void Segmented::resized()
{
    proxy.setBounds (0, 0, 1, 1);

    if (segments.empty())
        return;

    auto b = getLocalBounds();
    b.removeFromTop (18);

    const int n = (int) segments.size();
    const auto w = (float) b.getWidth() / (float) n;

    for (int i = 0; i < n; ++i)
        segments[(std::size_t) i]->setBounds (
            b.getX() + juce::roundToInt ((float) i * w),
            b.getY(),
            juce::roundToInt ((float) (i + 1) * w) - juce::roundToInt ((float) i * w),
            b.getHeight());
}

} // namespace fw::ui

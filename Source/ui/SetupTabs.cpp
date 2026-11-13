#include "SetupTabs.h"

#include "KnobCell.h"
#include "Params.h"
#include "WavesStyle.h"

namespace fw::ui
{

//==============================================================================
class SetupTabs::ComboCell final : public juce::Component
{
public:
    ComboCell (juce::AudioProcessorValueTreeState& state, const char* paramId, const char* caption)
    {
        auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));

        if (p == nullptr)
            return;

        for (int i = 0; i < p->choices.size(); ++i)
            combo.addItem (p->choices[i], i + 1);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, combo);

        captionText = caption;
        addAndMakeVisible (combo);
    }

    juce::ComboBox& box() noexcept { return combo; }

    void resized() override
    {
        // Caption on the same 13 px top band as the KnobCell captions beside it;
        // the LCD combo is centred in the rest so it shares the knobs' centre line.
        auto b = getLocalBounds();
        captionArea = b.removeFromTop (kCaptionH);
        combo.setBounds (b.withSizeKeepingCentre (b.getWidth(), juce::jmin (b.getHeight(), kComboH)));
    }

    void paint (juce::Graphics& g) override
    {
        waves::caption (g, captionArea.toFloat(), captionText, juce::Justification::centred,
                        isEnabled() ? Palette::caption : Palette::dim.withAlpha (0.6f));
    }

    void enablementChanged() override { repaint(); }

private:
    static constexpr int kCaptionH = 13, kComboH = 30;

    juce::String captionText;
    juce::Rectangle<int> captionArea;
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

//==============================================================================
class SetupTabs::MaskCell final : public juce::Component
{
public:
    MaskCell (juce::AudioProcessorValueTreeState& state, const char* paramId, int number)
    {
        button.setButtonText (juce::String (number));
        // Black Waves Tune key button, violet when the step is enabled; every
        // state is drawn by FretLookAndFeel (waves::buttonFace).
        button.setFretFont (Palette::fontSemi (13.0f));
        button.setColour (juce::TextButton::buttonColourId,   Palette::blackKey);
        button.setColour (juce::TextButton::textColourOffId,  Palette::text);
        button.setColour (juce::TextButton::buttonOnColourId, Palette::accent);
        button.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        addAndMakeVisible (button);

        if (auto* p = dynamic_cast<juce::AudioParameterBool*> (state.getParameter (paramId)))
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }
    void paint (juce::Graphics&) override {}

private:
    FretButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
juce::Rectangle<int> SetupTabs::cellRect (int col, int row, int colSpan)
{
    const int h = (row >= 2) ? kCompactH : kCellH;
    return { kGridX + col * (kCellW + kGapX),
             kGridY + row * (kCellH + kGapY),
             kCellW * colSpan + kGapX * (colSpan - 1),
             h };
}

juce::Rectangle<int> SetupTabs::maskCellRect (int index)
{
    return { kMaskX + index * (kMaskW + kMaskGap),
             kGridY + 2 * (kCellH + kGapY),
             kMaskW,
             kCompactH };
}

//==============================================================================
SetupTabs::SetupTabs (FretworkAudioProcessor& p, juce::AudioProcessorValueTreeState& state)
    : processor (p)
{
    tabBar.addTab ("BODY", Palette::panel, Body + 1);
    tabBar.addTab ("STRINGS", Palette::panel, Strings + 1);
    tabBar.addTab ("PICKUP", Palette::panel, Pickup + 1);
    tabBar.addTab ("TONE", Palette::panel, Tone + 1);
    tabBar.addTab ("ARP", Palette::panel, Arp + 1);
    tabBar.addTab ("AMP", Palette::panel, Amp + 1);
    tabBar.addTab ("FX", Palette::panel, Fx + 1);
    tabBar.addChangeListener (this);
    addAndMakeVisible (tabBar);

    for (auto& page : pages)
    {
        page = new juce::Component();
        addChildComponent (page);
    }

    buildBody (*pages[Body], state);
    buildStrings (*pages[Strings], state);
    buildPickup (*pages[Pickup], state);
    buildTone (*pages[Tone], state);
    buildArp (*pages[Arp], state);
    buildAmp (*pages[Amp], state);
    buildFx (*pages[Fx], state);

    tabBar.setCurrentTabIndex (0);
    setCurrentPage (0);
}

SetupTabs::~SetupTabs()
{
    tabBar.removeChangeListener (this);
}

//==============================================================================
void SetupTabs::setCurrentPage (int index)
{
    if (index == currentPage)
        return;

    currentPage = index;

    for (int i = 0; i < NumPages; ++i)
        if (pages[(std::size_t) i] != nullptr)
            pages[(std::size_t) i]->setVisible (i == index);

    repaint();
}

void SetupTabs::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &tabBar)
        setCurrentPage (tabBar.getCurrentTabIndex());
}

void SetupTabs::resized()
{
    tabBar.setBounds (0, 0, getWidth(), kTabStrip);

    for (auto* page : pages)
        if (page != nullptr)
            page->setBounds (0, kTabStrip, getWidth(), getHeight() - kTabStrip);
}

void SetupTabs::paint (juce::Graphics& g)
{
    // Atlas layout: the tab strip sits above a Waves group frame that holds the
    // pages. waves::group draws its rule 7 px below the rect top, so starting
    // the rect 8 px above the strip's bottom lays that rule on the strip's own
    // baseline instead of doubling it.
    const auto full = getLocalBounds().toFloat();
    waves::group (g, full.withTrimmedTop ((float) (kTabStrip - 8)));
}

//==============================================================================
void SetupTabs::buildBody (juce::Component& page, juce::AudioProcessorValueTreeState& state)
{
    const auto knob = [&state, &page] (int col, int row, const char* id, const char* cap,
                                       bool micro = false, int colSpan = 1)
    {
        auto* cell = new KnobCell();
        cell->attach (state, id, cap, micro);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
        return cell;
    };

    const auto combo = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new ComboCell (state, id, cap);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
        return cell;
    };

    auto* bodyCell = combo (0, 0, fw::id::body, "BODY", 2);
    bodyCell->box().onChange = [this, bodyCell]
    {
        processor.applyBodyDefaults (bodyCell->box().getSelectedItemIndex());
    };

    combo (2, 0, fw::id::tuning, "TUNING", 2);
    knob (4, 0, fw::id::capo, "CAPO");

    knob (0, 1, fw::id::bodySize, "SIZE");
    knob (1, 1, fw::id::bodyRes, "RES");
    knob (2, 1, fw::id::topDamp, "TOP");
    knob (3, 1, fw::id::micBlend, "MIC");

    static const char* const kTuningIds[6] =
    {
        fw::id::tune0, fw::id::tune1, fw::id::tune2,
        fw::id::tune3, fw::id::tune4, fw::id::tune5
    };

    static const char* const kTuningNames[6] = { "E", "A", "D", "G", "B", "e" };

    for (int i = 0; i < 6; ++i)
    {
        auto* cell = new KnobCell();
        cell->attach (state, kTuningIds[i], kTuningNames[i], true);
        cell->setBounds (maskCellRect (i));
        page.addAndMakeVisible (cell);
    }
}

//==============================================================================
void SetupTabs::buildStrings (juce::Component& page, juce::AudioProcessorValueTreeState& state)
{
    const auto knob = [&state, &page] (int col, int row, const char* id, const char* cap, bool micro = false)
    {
        auto* cell = new KnobCell();
        cell->attach (state, id, cap, micro);
        cell->setBounds (cellRect (col, row));
        page.addAndMakeVisible (cell);
    };

    const auto combo = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new ComboCell (state, id, cap);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
    };

    combo (0, 0, fw::id::material, "MATERIAL", 2);
    combo (2, 0, fw::id::stringSet, "GAUGE", 2);
    combo (4, 0, fw::id::guitarType, "GUITAR");

    knob (0, 1, fw::id::courseLevel, "COURSE");
    knob (1, 1, fw::id::pickHard, "PICK HARD");
    knob (2, 1, fw::id::fretNoise, "FRETS");
    knob (3, 1, fw::id::sustain, "SUSTAIN");
    knob (4, 1, fw::id::pickPos, "PICK POS");

    // Compact row: caption-only cells, so the three extra controls fit without
    // sharing a slot with a neighbour.
    knob (0, 2, fw::id::stringAge, "AGE", true);
    knob (1, 2, fw::id::stiffness, "STIFF", true);
    knob (2, 2, fw::id::sympathetic, "SYMP", true);
}

//==============================================================================
void SetupTabs::buildPickup (juce::Component& page, juce::AudioProcessorValueTreeState& state)
{
    const auto knob = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new KnobCell();
        cell->attach (state, id, cap, false);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
    };

    const auto combo = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new ComboCell (state, id, cap);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
    };

    combo (0, 0, fw::id::pickupType, "TYPE", 2);
    combo (2, 0, fw::id::pickupSel, "PICKUP", 2);
    knob (4, 0, fw::id::pickupHeight, "HEIGHT");

    knob (0, 1, fw::id::pickupTone, "TONE");
    knob (1, 1, fw::id::pickupOut, "OUT");
}

//==============================================================================
void SetupTabs::buildTone (juce::Component& page, juce::AudioProcessorValueTreeState& state)
{
    const auto knob = [&state, &page] (int col, int row, const char* id, const char* cap, bool compact)
    {
        auto* cell = new KnobCell();
        cell->attach (state, id, cap, compact);
        cell->setBounds (cellRect (col, row));
        page.addAndMakeVisible (cell);
    };

    const auto combo = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new ComboCell (state, id, cap);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
    };

    knob (0, 0, fw::id::tone, "TONE", false);
    knob (1, 0, fw::id::drive, "DRIVE", false);
    knob (2, 0, fw::id::room, "ROOM", false);
    knob (3, 0, fw::id::width, "WIDTH", false);
    knob (4, 0, fw::id::output, "OUTPUT", false);

    combo (0, 2, fw::id::roomType, "ROOM TYPE", 2);

    knob (0, 1, fw::id::vibRate, "VIB RATE", false);
    knob (1, 1, fw::id::vibDepth, "VIB DEPTH", false);
    knob (2, 1, fw::id::glideTime, "GLIDE", false);
    knob (3, 1, fw::id::bendAmount, "BEND AMT", false);
    knob (4, 1, fw::id::bendTime, "BEND TIME", false);

    knob (2, 2, fw::id::wheelRange, "WHEEL", true);
}

//==============================================================================
void SetupTabs::buildAmp (juce::Component& page, juce::AudioProcessorValueTreeState& state)
{
    const auto knob = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new KnobCell();
        cell->attach (state, id, cap, false);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
    };

    const auto combo = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new ComboCell (state, id, cap);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
    };

    combo (0, 0, fw::id::ampModel, "AMP", 2);
    combo (2, 0, fw::id::cab, "CABINET", 2);
    knob (4, 0, fw::id::ampGain, "GAIN");

    knob (0, 1, fw::id::ampBass, "BASS");
    knob (1, 1, fw::id::ampMid, "MID");
    knob (2, 1, fw::id::ampTreble, "TREBLE");
    knob (3, 1, fw::id::ampLevel, "LEVEL");
}

//==============================================================================
void SetupTabs::buildFx (juce::Component& page, juce::AudioProcessorValueTreeState& state)
{
    const auto knob = [&state, &page] (int col, int row, const char* id, const char* cap)
    {
        auto* cell = new KnobCell();
        cell->attach (state, id, cap, false);
        cell->setBounds (cellRect (col, row));
        page.addAndMakeVisible (cell);
    };

    knob (0, 0, fw::id::chorusMix, "CHORUS");
    knob (1, 0, fw::id::chorusRate, "RATE");

    knob (0, 1, fw::id::delayMix, "DELAY");
    knob (1, 1, fw::id::delayTime, "TIME");
    knob (2, 1, fw::id::delayFeedback, "FEEDBK");
}

//==============================================================================
void SetupTabs::buildArp (juce::Component& page, juce::AudioProcessorValueTreeState& state)
{
    const auto knob = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new KnobCell();
        cell->attach (state, id, cap, false);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
    };

    const auto combo = [&state, &page] (int col, int row, const char* id, const char* cap, int colSpan = 1)
    {
        auto* cell = new ComboCell (state, id, cap);
        cell->setBounds (cellRect (col, row, colSpan));
        page.addAndMakeVisible (cell);
    };

    combo (0, 0, fw::id::arpOrder, "ORDER", 2);
    combo (2, 0, fw::id::arpRate, "RATE", 2);
    knob (4, 0, fw::id::arpStrings, "STRINGS");

    knob (0, 1, fw::id::arpGate, "GATE");
    knob (1, 1, fw::id::arpSwing, "SWING");

    static const char* const kMaskIds[6] =
    {
        fw::id::arpMask5, fw::id::arpMask4, fw::id::arpMask3,
        fw::id::arpMask2, fw::id::arpMask1, fw::id::arpMask0
    };

    for (int i = 0; i < 6; ++i)
    {
        auto* cell = new MaskCell (state, kMaskIds[i], 6 - i);
        cell->setBounds (maskCellRect (i));
        page.addAndMakeVisible (cell);
    }
}


} // namespace fw::ui

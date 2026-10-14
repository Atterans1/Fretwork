#include "PluginEditor.h"
#include "ui/WavesStyle.h"

namespace fw::ui
{

//==============================================================================
/** A choice parameter as a Waves Tune readout: small-caps caption, the value
    in an inset LCD box, and -/+ steppers underneath. */
class ChoiceCell final : public juce::Component
{
public:
    ChoiceCell (juce::AudioProcessorValueTreeState& state, const char* paramId, const char* caption)
        : captionText (caption)
    {
        auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));

        if (p == nullptr)
            return;

        param = p;

        for (int i = 0; i < p->choices.size(); ++i)
            proxy.addItem (p->choices[i], i + 1);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, proxy);
        proxy.setVisible (false);
        proxy.setInterceptsMouseClicks (false, false);
        addChildComponent (proxy);

        up.setButtonText ("+");
        down.setButtonText ("-");

        for (auto* b : { &up, &down })
        {
            b->setFretFont (Palette::fontNum (15.0f));
            addAndMakeVisible (b);
        }

        up.onClick = [this] { step (1); };
        down.onClick = [this] { step (-1); };

        refresh();
    }

    void refresh()
    {
        if (param == nullptr)
            return;

        const int index = (int) param->getIndex();
        const auto text = param->choices[juce::jlimit (0, param->choices.size() - 1, index)];

        if (text != valueText)
        {
            valueText = text;
            repaint (valueArea);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        captionArea = b.removeFromTop (16);

        // Waves Tune's Reference control: "-" and "+" flank the value in one
        // row ("- 440.00 +"). Stacking them under the value left the two cells'
        // steppers reading as one ambiguous "- + - +" row.
        const int arrowW = 18, gap = 2;
        auto row = b.withSizeKeepingCentre (b.getWidth(), juce::jmin (26, b.getHeight()));

        down.setBounds (row.removeFromLeft (arrowW));
        up.setBounds   (row.removeFromRight (arrowW));
        valueArea = row.reduced (gap, 0);
    }

    void paint (juce::Graphics& g) override
    {
        const bool enabled = isEnabled();

        waves::caption (g, captionArea.toFloat(), captionText, juce::Justification::centred,
                        enabled ? Palette::caption : Palette::dim.withAlpha (0.6f));
        waves::valueBox (g, valueArea.toFloat(), valueText, 14.0f, enabled);
    }

private:
    void step (int delta)
    {
        const int next = juce::jlimit (1, proxy.getNumItems(),
                                       proxy.getSelectedId() + delta);
        proxy.setSelectedId (next, juce::sendNotification);
        refresh();
    }

    juce::AudioParameterChoice* param = nullptr;
    juce::ComboBox proxy;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    juce::String captionText, valueText;
    juce::Rectangle<int> captionArea, valueArea;
    FretButton up, down;
};

//==============================================================================
/** One of the eight sequence-slot selectors: a black Waves Tune push button
    with a Bahnschrift numeral, lit violet when it is the selected slot. */
class SlotButton final : public FretButton
{
public:
    SlotButton (juce::Slider& proxy, const juce::AudioParameterInt& p, int number)
        : slotProxy (proxy), param (p), slotNumber (number)
    {
        setButtonText (juce::String (number));
        setClickingTogglesState (false);
        onClick = [this] { slotProxy.setValue ((double) slotNumber); };
    }

    void paintButton (juce::Graphics& g, bool, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.5f);
        const bool on = (int) param.get() == slotNumber;
        const bool enabled = isEnabled();

        waves::buttonFace (g, r, on, isOver(), isDown(), enabled, 3.0f);

        g.setColour (on ? Palette::background
                        : (enabled ? Palette::text : Palette::dim.withAlpha (0.6f)));
        g.setFont (Palette::fontNum (14.0f));
        g.drawText (getButtonText(), r.translated (0.0f, isDown() ? 1.0f : 0.0f),
                    juce::Justification::centred, false);
    }

private:
    juce::Slider& slotProxy;
    const juce::AudioParameterInt& param;
    int slotNumber;
};

} // namespace fw::ui

//==============================================================================
PluginEditor::PluginEditor (FretworkAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
    , processor (p)
    , seqGrid (p, p.getState())
    , setupTabs (p, p.getState())
    , keyboard (p.getKeyboardState(), p.getUi())
    , transport (p, p.getState())
{
    lookAndFeel = std::make_unique<fw::ui::FretLookAndFeel>();
    setLookAndFeel (lookAndFeel.get());

    addAndMakeVisible (fretboard);
    addAndMakeVisible (seqGrid);
    addAndMakeVisible (setupTabs);
    addAndMakeVisible (transport);
    addAndMakeVisible (keyboard);

    guitarView = std::make_unique<fw::ui::GuitarBodyView> (p.getState());
    addAndMakeVisible (*guitarView);

    fretboard.setSnapshot (&processor.getUi());

    // ---- region A
    static const char* const kStrummerIds[6] =
    {
        fw::id::strumTime, fw::id::release, fw::id::strokeVol,
        fw::id::timeOffset, fw::id::separation, fw::id::velOffset
    };

    static const char* const kStrummerCaptions[6] =
    {
        "Strum Time", "Release", "Stroke Volume", "Time", "Separation", "Velocity"
    };

    for (int i = 0; i < 6; ++i)
    {
        strummerKnobs[(std::size_t) i] = std::make_unique<fw::ui::KnobCell>();
        strummerKnobs[(std::size_t) i]->attach (processor.getState(),
                                                kStrummerIds[i],
                                                kStrummerCaptions[i]);
        addAndMakeVisible (*strummerKnobs[(std::size_t) i]);
    }

    // ---- region E
    slotParam = dynamic_cast<juce::AudioParameterInt*> (processor.getState().getParameter (fw::id::seqSlot));

    slotProxy.setVisible (false);
    slotProxy.setInterceptsMouseClicks (false, false);
    addChildComponent (slotProxy);

    if (slotParam != nullptr)
    {
        slotProxy.setRange (slotParam->getRange().getStart(), slotParam->getRange().getEnd(), 1.0);
        slotAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getState(), fw::id::seqSlot, slotProxy);

        for (int i = 0; i < 8; ++i)
        {
            slotButtons[(std::size_t) i] = std::make_unique<fw::ui::SlotButton> (slotProxy, *slotParam, i + 1);
            addAndMakeVisible (*slotButtons[(std::size_t) i]);
        }
    }

    slotLabel.setText ("SEQ #1", juce::dontSendNotification);
    // Glowing LCD text; paint() puts the black LCD strip behind it.
    slotLabel.setFont (fw::ui::Palette::fontNum (13.0f));
    slotLabel.setColour (juce::Label::textColourId, fw::ui::Palette::lcdText);
    slotLabel.setJustificationType (juce::Justification::centredLeft);
    slotLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (slotLabel);

    humanizeCell = std::make_unique<fw::ui::KnobCell>();
    humanizeCell->attach (processor.getState(), fw::id::humanize, "Humanization");
    addAndMakeVisible (*humanizeCell);

    choiceCells[0] = std::make_unique<fw::ui::ChoiceCell> (processor.getState(), fw::id::quantize, "Quantize");
    choiceCells[1] = std::make_unique<fw::ui::ChoiceCell> (processor.getState(), fw::id::signature, "Signature");
    addAndMakeVisible (*choiceCells[0]);
    addAndMakeVisible (*choiceCells[1]);

    // ---- region F
    static const char* const kArtIds[8] =
    {
        fw::id::excitation, fw::id::damping, fw::id::transition,
        fw::id::harmonic, fw::id::tremolo, fw::id::mode, fw::id::voicing
    };

    static const char* const kArtCaptions[8] =
    {
        "Excitation", "Damping", "Transition", "Harmonic", "Tremolo", "Mode", "Voicing", "Hold"
    };

    for (int i = 0; i < 8; ++i)
    {
        articulation[(std::size_t) i] = std::make_unique<fw::ui::Segmented>();
        articulation[(std::size_t) i]->setCaption (kArtCaptions[i]);
        addAndMakeVisible (*articulation[(std::size_t) i]);

        if (i < 7)
            articulation[(std::size_t) i]->addChoices (processor.getState(), kArtIds[i]);
        else
            articulation[(std::size_t) i]->addFlag (processor.getState(), fw::id::hold);
    }

    // Mode group: how each mode plays without any setup.
    articulation[5]->setSegmentTooltip (0, "Poly: each key plays its own string. Hammer-ons, slides, mutes and harmonics follow how you play.");
    articulation[5]->setSegmentTooltip (1, "Strum: hold a chord and it strums. Keys 28-35 strum by hand instead.");
    articulation[5]->setSegmentTooltip (2, "Arp: hold a chord and it arpeggiates.");
    articulation[5]->setSegmentTooltip (3, "Seq: hold a chord and the pattern in the selected slot plays. Slots 1-4 hold Pop, Folk, Rock and Funk.");
    articulation[7]->setSegmentTooltip (0, "Hold: keep the chord ringing after you let go of the keys.");

    setResizable (false, false);
    setSize (1120, 700);

    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    namespace waves = fw::ui::waves;

    // Bevelled charcoal chassis behind everything, then the Waves Tune group
    // frames for the regions the editor lays out itself. The fretboard, guitar
    // view, tabs, sequencer, transport and keyboard draw their own wells.
    waves::chassis (g, getLocalBounds().toFloat());

    waves::group (g, { 12.0f, 12.0f, 340.0f, 250.0f }, "Strummer");      // A
    waves::group (g, { 706.0f, 258.0f, 402.0f, 114.0f }, "Sequence");    // E

    // The "SEQ #n" readout sits on its own LCD strip.
    waves::lcd (g, slotLabel.getBounds().toFloat(), 2.0f);
}

//==============================================================================
void PluginEditor::resized()
{
    fretboard.setBounds (364, 12, 330, 120);
    seqGrid.setBounds (706, 12, 402, 240);
    setupTabs.setBounds (364, 144, 330, 228);
    // Region A ends at y 262, setupTabs starts at x 364, the articulation bar
    // at 384: this is the only strip left under the strummer knobs.
    guitarView->setBounds (12, 266, 340, 114);
    transport.setBounds (12, 452, 1096, 44);
    // 50 white keys at 22px = 1100; 1096 clipped the top C by 4px.
    keyboard.setBounds (12, 508, 1100, 180);

    // A: six 113 x 114 cells, 2 rows of 3. They start 16px below the region
    // top so the "STRUMMER" caption on the group's top rule stays clear of
    // the middle knob's caption, and end at 256, inside the frame's bottom rule.
    for (int i = 0; i < 6; ++i)
        strummerKnobs[(std::size_t) i]->setBounds (12 + 113 * (i % 3), 28 + 114 * (i / 3), 113, 114);

    // E: everything starts below the "SEQUENCE" caption on the top rule.
    slotLabel.setBounds (716, 278, 132, 18);
    slotLabel.setText ("SEQ #" + juce::String (slotParam != nullptr ? (int) slotParam->get() : 1),
                       juce::dontSendNotification);

    for (int i = 0; i < 8; ++i)
        slotButtons[(std::size_t) i]->setBounds (716 + 34 * (i % 4), 300 + 34 * (i / 4), 30, 30);

    // Region E spans x 706..1108. These three need ~6px gaps between them and
    // a right margin: at 76 wide they end at 934 / 1016 / 1098, and at 92
    // tall they end at 368, inside the frame's bottom rule at 372.
    humanizeCell->setBounds (858, 276, 76, 92);
    choiceCells[0]->setBounds (940, 276, 76, 92);
    choiceCells[1]->setBounds (1022, 276, 76, 92);

    layoutArticulationBar();
}

void PluginEditor::layoutArticulationBar()
{
    static constexpr int weights[8] = { 3, 3, 4, 2, 2, 4, 3, 1 };
    static constexpr int total = 22;
    constexpr int x0 = 12, y0 = 384, height = 56, gap = 8;

    const int usable = 1096 - gap * (8 - 1);
    int x = x0, assigned = 0;

    for (int i = 0; i < 8; ++i)
    {
        const int w = (i == 7) ? (usable - assigned) : (usable * weights[i]) / total;
        assigned += w;
        articulation[(std::size_t) i]->setBounds (x, y0, w, height);
        x += w + gap;
    }
}

//==============================================================================
void PluginEditor::timerCallback()
{
    fretboard.repaint();
    seqGrid.tick();
    keyboard.repaint();
    transport.tick();

    for (auto& cell : choiceCells)
        if (cell != nullptr)
            cell->refresh();

    for (auto& group : articulation)
        if (group != nullptr)
            group->refresh();

    for (auto& button : slotButtons)
        if (button != nullptr)
            button->repaint();

    if (slotParam != nullptr)
        slotLabel.setText ("SEQ #" + juce::String ((int) slotParam->get()), juce::dontSendNotification);

    applyKeyswitchChanges();
}

void PluginEditor::applyKeyswitchChanges()
{
    auto& ui = processor.getUi();
    const auto dirty = ui.ksDirtyMask.load (std::memory_order_relaxed);

    if (dirty == 0u)
        return;

    static const char* const kGroupIds[6] =
    {
        fw::id::excitation, fw::id::damping, fw::id::transition,
        fw::id::harmonic, fw::id::tremolo, fw::id::mode
    };

    auto& state = processor.getState();

    for (int i = 0; i < 6; ++i)
    {
        const auto bit = 1u << (std::uint32_t) i;

        if ((dirty & bit) == 0u)
            continue;

        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (kGroupIds[i])))
        {
            const int value = juce::jlimit (0, p->choices.size() - 1, ui.ks[i].load());
            p->setValueNotifyingHost ((float) value);
        }

        // The processor owns the snapshot, so it clears its own pending bit.
        processor.clearKeyswitchDirty (i);
    }
}

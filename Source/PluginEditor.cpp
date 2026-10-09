#include "PluginEditor.h"
#include "ui/AssetCache.h"

namespace fw::ui
{

//==============================================================================
class ChoiceCell final : public juce::Component
{
public:
    ChoiceCell (juce::AudioProcessorValueTreeState& state, const char* paramId, const char* caption)
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

        captionLabel.setText (juce::String (caption).toUpperCase(), juce::dontSendNotification);
        captionLabel.setJustificationType (juce::Justification::centred);
        captionLabel.setFont (Palette::font (11.0f));
        captionLabel.setColour (juce::Label::textColourId, Palette::dim);
        captionLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (captionLabel);

        up.setButtonText ("+");
        down.setButtonText ("-");

        for (auto* b : { &up, &down })
        {
            b->setFretFont (Palette::font (15.0f));
            b->setColour (juce::TextButton::buttonColourId, Palette::panelHi);
            addAndMakeVisible (b);
        }

        up.onClick = [this] { step (1); };
        down.onClick = [this] { step (-1); };

        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setFont (Palette::font (13.0f));
        valueLabel.setColour (juce::Label::textColourId, Palette::text);
        valueLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (valueLabel);

        refresh();
    }

    void refresh()
    {
        if (param == nullptr)
            return;

        const int index = (int) param->getIndex();
        valueLabel.setText (param->choices[juce::jlimit (0, param->choices.size() - 1, index)],
                            juce::dontSendNotification);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        captionLabel.setBounds (b.removeFromTop (15));

        // Both steppers share one bottom strip. Calling removeFromBottom twice
        // stacked them 20px apart with the "-" above the "+", which is upside
        // down for a stepper, so the strip is taken once and split.
        const int arrowW = 22;
        const auto strip = b.removeFromBottom (20);

        // withX() only moves the rectangle - without withWidth() the buttons
        // stayed cell-width and overlapped, which pushed the "-" glyph centre
        // past the cell edge where it was clipped away entirely.
        up.setBounds   (strip.withWidth (arrowW).withX (0));
        down.setBounds (strip.withWidth (arrowW).withX (b.getWidth() - arrowW));
        // The steppers live in their own bottom strip, so the label gets the
        // full middle width; trimming arrowW here left 32px and ellipsised
        // values like "1/16" and "12/8".
        valueLabel.setBounds (b);
    }

    void paint (juce::Graphics&) override {}

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
    juce::Label captionLabel, valueLabel;
    FretButton up, down;
};

//==============================================================================
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

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = (int) param.get() == slotNumber;

        g.setColour (on ? Palette::accent : Palette::panelHi);
        g.fillEllipse (r);
        g.setColour (Palette::line);
        g.drawEllipse (r, 1.0f);
        g.setColour (on ? Palette::background : Palette::dim);
        g.setFont (Palette::font (11.0f, juce::Font::bold));
        g.drawText (getButtonText(), r, juce::Justification::centred, false);
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
    slotLabel.setFont (fw::ui::Palette::font (13.0f));
    slotLabel.setColour (juce::Label::textColourId, fw::ui::Palette::accent);
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
    static const char* const kArtIds[7] =
    {
        fw::id::excitation, fw::id::damping, fw::id::transition,
        fw::id::harmonic, fw::id::tremolo, fw::id::mode
    };

    static const char* const kArtCaptions[7] =
    {
        "Excitation", "Damping", "Transition", "Harmonic", "Tremolo", "Mode", "Hold"
    };

    for (int i = 0; i < 7; ++i)
    {
        articulation[(std::size_t) i] = std::make_unique<fw::ui::Segmented>();
        articulation[(std::size_t) i]->setCaption (kArtCaptions[i]);
        addAndMakeVisible (*articulation[(std::size_t) i]);

        if (i < 6)
            articulation[(std::size_t) i]->addChoices (processor.getState(), kArtIds[i]);
        else
            articulation[(std::size_t) i]->addFlag (processor.getState(), fw::id::hold);
    }

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
    g.fillAll (fw::ui::Palette::background);

    // Brushed-metal wash under everything. Deliberately low contrast: it takes
    // the edge off the flat fill without competing with the panels on top.
    if (const auto& tex = fw::ui::AssetCache::get().panelTexture(); tex.isValid())
    {
        g.setOpacity (0.45f);
        const auto tw = tex.getWidth(), th = tex.getHeight();
        for (int y = 0; y < getHeight(); y += th)
            for (int x = 0; x < getWidth(); x += tw)
                g.drawImage (tex, x, y, tw, th, 0, 0, tw, th);
        g.setOpacity (1.0f);
    }

    const auto panel = [&g] (juce::Rectangle<float> r)
    {
        g.setColour (fw::ui::Palette::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (fw::ui::Palette::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    };

    panel ({ 12.0f, 12.0f, 340.0f, 250.0f });      // A
    panel ({ 706.0f, 258.0f, 402.0f, 114.0f });    // E
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

    // A: six 113 x 125 cells, 2 rows of 3
    for (int i = 0; i < 6; ++i)
        strummerKnobs[(std::size_t) i]->setBounds (12 + 113 * (i % 3), 12 + 125 * (i / 3), 113, 125);

    // E
    slotLabel.setBounds (714, 262, 90, 16);
    slotLabel.setText ("SEQ #" + juce::String (slotParam != nullptr ? (int) slotParam->get() : 1),
                       juce::dontSendNotification);

    for (int i = 0; i < 8; ++i)
        slotButtons[(std::size_t) i]->setBounds (714 + 38 * (i % 4), 282 + 32 * (i / 4), 30, 30);

    // Region E spans x 706..1108. These three need ~6px gaps between them and
    // a right margin: at 76 wide they end at 936 / 1018 / 1100.
    humanizeCell->setBounds (860, 262, 76, 100);
    choiceCells[0]->setBounds (942, 262, 76, 100);
    choiceCells[1]->setBounds (1024, 262, 76, 100);

    layoutArticulationBar();
}

void PluginEditor::layoutArticulationBar()
{
    static constexpr int weights[7] = { 3, 3, 4, 2, 2, 4, 1 };
    static constexpr int total = 19;
    constexpr int x0 = 12, y0 = 384, height = 56, gap = 8;

    const int usable = 1096 - gap * (7 - 1);
    int x = x0, assigned = 0;

    for (int i = 0; i < 7; ++i)
    {
        const int w = (i == 6) ? (usable - assigned) : (usable * weights[i]) / total;
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

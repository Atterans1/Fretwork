#include "SeqGrid.h"

#include <cmath>

namespace fw::ui
{

//==============================================================================
// JUCE 8 removed RangedAudioParameterListener; the APVTS listener registered per
// parameter ID is the supported replacement.
class SeqGrid::LayoutListener final : public juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit LayoutListener (SeqGrid& g) : owner (g) {}

    void parameterChanged (const juce::String&, float) override { owner.recomputeLayout(); }

private:
    SeqGrid& owner;
};

namespace
{
    constexpr int kNumRows = 12;
    constexpr int kMaxCols = 32;
    constexpr int kIconCol = 44;
    constexpr int kGap = 1;
    constexpr int kGridTop = 22;
}

//==============================================================================
SeqGrid::SeqGrid (FretworkAudioProcessor& p, juce::AudioProcessorValueTreeState& state)
    : processor (p)
    , apvts (&state)
{
    slotParam = dynamic_cast<juce::AudioParameterInt*> (state.getParameter (fw::id::seqSlot));
    quantizeParam = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (fw::id::quantize));
    signatureParam = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (fw::id::signature));

    layoutListener = std::make_unique<LayoutListener> (*this);

    if (quantizeParam != nullptr)
        state.addParameterListener (fw::id::quantize, layoutListener.get());

    if (signatureParam != nullptr)
        state.addParameterListener (fw::id::signature, layoutListener.get());

    recomputeLayout();
}

SeqGrid::~SeqGrid()
{
    if (apvts != nullptr)
    {
        if (quantizeParam != nullptr)
            apvts->removeParameterListener (fw::id::quantize, layoutListener.get());

        if (signatureParam != nullptr)
            apvts->removeParameterListener (fw::id::signature, layoutListener.get());
    }
}

//==============================================================================
void SeqGrid::recomputeLayout()
{
    if (quantizeParam == nullptr || signatureParam == nullptr)
        return;

    const int q = juce::jlimit (0, 2, (int) quantizeParam->getIndex());
    const int s = juce::jlimit (0, 6, (int) signatureParam->getIndex());

    const int stepsPerQuarter = 2 << q;   // 0 -> 1/8, 1 -> 1/16, 2 -> 1/32
    stepsPerQuarter_ = stepsPerQuarter;

    static constexpr int kSignatures[7][2] =
    {
        { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 }
    };

    const double numerator = (double) kSignatures[s][0];
    const double denominator = (double) kSignatures[s][1];

    const int computed = juce::jlimit (1, kMaxCols,
                                       (int) std::lround (numerator * 4.0 / denominator
                                                          * (double) stepsPerQuarter));

    if (computed != stepsPerBar)
    {
        stepsPerBar = computed;
        resized();
    }

    repaint();
}

void SeqGrid::tick()
{
    recomputeLayout();
}

int SeqGrid::currentSlot() const noexcept
{
    if (slotParam == nullptr)
        return 0;

    return juce::jlimit (0, processor.getSeqStore().slotCount() - 1, (int) slotParam->get() - 1);
}

void SeqGrid::resized()
{
    const int gridW = juce::jmax (4, getWidth() - kIconCol);
    cellW = juce::jmax (4, (gridW - (stepsPerBar - 1) * kGap) / stepsPerBar);
    rowH = juce::jmax (6, (getHeight() - kGridTop - (kNumRows - 1) * kGap) / kNumRows);
}

//==============================================================================
juce::Rectangle<float> SeqGrid::cellRect (int row, int col) const noexcept
{
    return { (float) (kIconCol + col * (cellW + kGap)),
             (float) (kGridTop + row * (rowH + kGap)),
             (float) cellW,
             (float) rowH };
}

int SeqGrid::colAt (float x) const noexcept
{
    const int c = (int) std::floor ((x - (float) kIconCol) / (float) (cellW + kGap));
    return (c < 0 || c >= stepsPerBar) ? -1 : c;
}

int SeqGrid::rowAt (float y) const noexcept
{
    const int r = (int) std::floor ((y - (float) kGridTop) / (float) (rowH + kGap));
    return (r < 0 || r >= kNumRows) ? -1 : r;
}

//==============================================================================
void SeqGrid::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (full, 6.0f);

    juce::Path clip;
    clip.addRoundedRectangle (full, 6.0f);

    g.saveState();
    g.reduceClipRegion (clip);

    // ---- header: playhead position and slot number
    const auto& store = processor.getSeqStore();
    const int slot = currentSlot();

    g.setFont (Palette::font (9.0f));
    g.setColour (Palette::dim);
    g.drawText ("SEQUENCER", 6.0f, 5.0f, 90.0f, 12.0f, juce::Justification::centredLeft, false);
    g.setColour (Palette::accent);
    g.drawText ("SLOT " + juce::String (slot + 1),
                (float) (kIconCol - 8), 5.0f, (float) (getWidth() - kIconCol + 8), 12.0f,
                juce::Justification::centredRight, false);

    // ---- playhead column
    const int step = processor.getUi().seqStep.load();

    if (step >= 0 && step < stepsPerBar)
    {
        const auto col = cellRect (0, step);
        g.setColour (Palette::accent.withAlpha (0.25f));
        g.fillRect (col.getX(), (float) kGridTop, (float) cellW,
                    (float) (kGridTop + kNumRows * (rowH + kGap)));
    }

    // ---- beat and bar lines
    for (int c = 0; c < stepsPerBar; ++c)
    {
        const bool downbeat = (c % stepsPerQuarter_) == 0;
        const bool barLine = (c % stepsPerBar) == 0;

        if (! downbeat && ! barLine)
            continue;

        const auto x = cellRect (0, c).getX() - 0.5f;
        g.setColour (barLine ? Palette::dim.withAlpha (0.65f) : Palette::line);
        g.fillRect (juce::Rectangle<float> (x, (float) kGridTop, 1.0f,
                                            (float) (kNumRows * (rowH + kGap) - kGap)));
    }

    // ---- notes, read live from the store
    const auto& pattern = store.getSlot (slot);

    for (int i = 0; i < pattern.count; ++i)
    {
        const auto& note = pattern.notes[(std::size_t) i];
        const int row = (int) note.row;
        const int start = (int) note.start;
        const int len = juce::jmax (1, (int) note.len);

        if (row < 0 || row >= kNumRows || start >= stepsPerBar)
            continue;

        const int visibleLen = juce::jmin (len, stepsPerBar - start);
        const auto r = cellRect (row, start);
        const auto box = r.withWidth ((float) (visibleLen * cellW + (visibleLen - 1) * kGap));

        juce::Colour base = row <= 3 ? (row % 2 == 0 ? Palette::accent2 : Palette::accent3)
                                     : (row == 4 || row == 5 ? (row == 4 ? Palette::accent2 : Palette::accent3)
                                                            : Palette::amber);

        if (row < 6)
            base = base.withAlpha (0.55f);

        const float bright = (juce::jlimit (1, 127, (int) note.vel) - 1) / 126.0f;
        g.setColour (base.interpolatedWith (juce::Colours::white, bright * 0.45f));
        g.fillRoundedRectangle (box, juce::jmin (2.5f, box.getHeight() * 0.3f));
    }

    g.restoreState();

    // ---- row glyph column
    g.setColour (Palette::panel);
    g.fillRect (juce::Rectangle<float> (1.0f, 1.0f, (float) (kIconCol - 1), (float) (getHeight() - 2)));
    g.setColour (Palette::line);
    g.fillRect (juce::Rectangle<float> ((float) (kIconCol - 1), 1.0f, 1.0f, (float) (getHeight() - 2)));
    g.setColour (Palette::line);
    g.drawRoundedRectangle (full.reduced (0.5f), 6.0f, 1.0f);

    static const char* const kStrokeGlyphs[6] = { "D", "U", "xD", "xU", "PD", "PU" };

    g.setFont (juce::Font (juce::FontOptions ("Segoe UI", 9.0f, juce::Font::bold)));
    g.setColour (Palette::dim);

    for (int row = 0; row < kNumRows; ++row)
    {
        const auto r = cellRect (row, 0);
        const juce::Rectangle<float> area (2.0f, r.getY(),
                                           (float) (kIconCol - 4), r.getHeight());
        const juce::String text = row < 6 ? juce::String (kStrokeGlyphs[row])
                                          : juce::String (row - 5);
        g.drawText (text, area, juce::Justification::centred, false);
    }
}

//==============================================================================
void SeqGrid::mouseDown (const juce::MouseEvent& e)
{
    resetDrag();

    const int col = colAt (e.position.x);
    const int row = rowAt (e.position.y);

    if (row < 0 || col < 0)
        return;

    const int slot = currentSlot();
    auto& store = processor.getSeqStore();
    const auto& pattern = store.getSlot (slot);
    const int index = pattern.findIndex ((std::uint8_t) row, (std::uint8_t) col);

    if (e.mods.isRightButtonDown())
    {
        if (index >= 0)
            store.removeNote (slot, row, col);

        repaint();
        return;
    }

    if (index < 0)
    {
        const fw::SeqNote note { (std::uint8_t) row, (std::uint8_t) col, 1, 100 };
        store.addNote (slot, note);
        repaint();
        return;
    }

    const auto held = pattern.notes[(std::size_t) index];
    dragRow = row;
    dragStart = col;
    dragLen = juce::jmax (1, (int) held.len);
    dragVel = juce::jlimit (1, 127, (int) held.vel);

    // Right-hand 40% of the note resizes; the rest of the body is a delete target.
    dragMode = (e.position.x >= cellRect (row, col).getX() + 0.6f * (float) cellW)
                 ? DragMode::resize
                 : DragMode::erase;
}

void SeqGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode != DragMode::resize)
        return;

    const int col = colAt (e.position.x);
    if (col < 0)
        return;

    const int len = juce::jmax (1, juce::jmin (stepsPerBar - dragStart, col - dragStart + 1));

    if (len == dragLen)
        return;

    dragLen = len;

    const fw::SeqNote note { (std::uint8_t) dragRow, (std::uint8_t) dragStart,
                             (std::uint8_t) len, (std::uint8_t) dragVel };
    processor.getSeqStore().setNote (currentSlot(), dragRow, dragStart, note);
    repaint();
}

void SeqGrid::mouseUp (const juce::MouseEvent&)
{
    resetDrag();
}

void SeqGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int col = colAt (e.position.x);
    const int row = rowAt (e.position.y);

    if (row < 0 || col < 0)
        return;

    const int slot = currentSlot();
    auto& store = processor.getSeqStore();
    const auto& pattern = store.getSlot (slot);
    const int index = pattern.findIndex ((std::uint8_t) row, (std::uint8_t) col);

    if (index < 0)
        return;

    auto note = pattern.notes[(std::size_t) index];
    const int delta = wheel.deltaY < 0.0f ? 8 : -8;
    note.vel = (std::uint8_t) juce::jlimit (1, 127, (int) note.vel + delta);

    store.setNote (slot, row, col, note);
    repaint();
}

void SeqGrid::resetDrag() noexcept
{
    dragMode = DragMode::none;
    dragRow = dragStart = 0;
    dragLen = 1;
    dragVel = 100;
}

} // namespace fw::ui

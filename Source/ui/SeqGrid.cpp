#include "SeqGrid.h"

#include "WavesStyle.h"

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

    // Paint-only: refresh when the playhead step or the edited-note highlight
    // differs from what is on screen.
    if (processor.getUi().seqStep.load() != paintedStep || dragMode != paintedDrag)
        repaint();
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
    const bool enabled = isEnabled();

    // Disabled: the whole display sits back at 40% (DESIGN.md widget states).
    if (! enabled)
        g.beginTransparencyLayer (0.4f);

    // ---- Atlas EQ display: one black LCD well for header, glyph gutter and grid
    waves::lcd (g, full, 6.0f);

    juce::Path clip;
    clip.addRoundedRectangle (full.reduced (1.0f), 5.0f);

    g.saveState();
    g.reduceClipRegion (clip);

    const auto& store = processor.getSeqStore();
    const int slot = currentSlot();
    const int step = processor.getUi().seqStep.load();
    const bool playheadOn = step >= 0 && step < stepsPerBar;
    paintedStep = step;
    paintedDrag = dragMode;

    const float gridX = (float) kIconCol;
    const float gridY = (float) kGridTop;
    const float gridW = (float) (stepsPerBar * (cellW + kGap) - kGap);
    const float gridH = (float) (kNumRows * (rowH + kGap) - kGap);

    // ---- header: section caption and slot number
    waves::caption (g, { 8.0f, 4.0f, 120.0f, 14.0f }, "Sequencer", juce::Justification::centredLeft);

    g.setFont (Palette::fontNum (11.5f));
    g.setColour (Palette::lcdText);
    g.drawText ("SLOT " + juce::String (slot + 1),
                (float) (kIconCol - 8), 4.0f, (float) (getWidth() - kIconCol), 14.0f,
                juce::Justification::centredRight, false);

    // ---- unlit LED cells, hairline grid
    for (int row = 0; row < kNumRows; ++row)
        for (int c = 0; c < stepsPerBar; ++c)
        {
            const bool hovered = enabled && row == hoverRow && c == hoverCol;
            g.setColour (hovered ? Palette::accent.withAlpha (0.22f) : Palette::unlit.withAlpha (0.32f));
            g.fillRoundedRectangle (cellRect (row, c).reduced (0.5f), 1.5f);
        }

    // beat lines in the hairline grey, bar lines a step brighter
    for (int c = 0; c < stepsPerBar; ++c)
    {
        const bool downbeat = (c % stepsPerQuarter_) == 0;
        const bool barLine = (c % stepsPerBar) == 0;

        if (! downbeat && ! barLine)
            continue;

        const auto x = cellRect (0, c).getX() - 0.5f;
        g.setColour (barLine ? Palette::dim.withAlpha (0.55f) : Palette::line);
        g.fillRect (juce::Rectangle<float> (x, gridY, 1.0f, gridH));
    }

    // stroke rows (D..PU) and string rows (1..6) separated by a hairline
    {
        const float y = cellRect (6, 0).getY() - 0.5f * (float) kGap - 0.5f;
        g.setColour (Palette::line);
        g.fillRect (juce::Rectangle<float> (4.0f, y, gridX + gridW - 4.0f, 1.0f));
    }

    // ---- playhead column: amber wash plus a marker on the header rule
    if (playheadOn)
    {
        const auto col = cellRect (0, step);
        g.setColour (Palette::amber.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (col.getX(), gridY, (float) cellW, gridH));

        g.setColour (Palette::amber.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float> (col.getX() - 1.0f, gridY - 5.0f, (float) cellW + 2.0f, 4.0f), 1.5f);
        g.setColour (Palette::amber.brighter (0.3f));
        g.fillRoundedRectangle (juce::Rectangle<float> (col.getX(), gridY - 4.0f, (float) cellW, 2.0f), 1.0f);
    }

    // ---- notes as violet LED cells, read live from the store. Brightness
    // follows velocity; a note under the playhead lights up further.
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
        const auto box = r.withWidth ((float) (visibleLen * cellW + (visibleLen - 1) * kGap)).reduced (0.5f);
        const float corner = juce::jmin (2.5f, box.getHeight() * 0.3f);

        const bool playing = playheadOn && step >= start && step < start + visibleLen;
        const bool active = dragMode != DragMode::none && row == dragRow && start == dragStart;
        const bool hovered = enabled && row == hoverRow && hoverCol >= start && hoverCol < start + visibleLen;

        const float bright = (juce::jlimit (1, 127, (int) note.vel) - 1) / 126.0f;
        const float lift = (playing ? 0.45f : 0.0f) + (hovered || active ? 0.15f : 0.0f);
        const auto led = Palette::accent.withMultipliedBrightness (0.55f + 0.45f * bright).brighter (lift);

        // soft glow halo
        g.setColour (led.withAlpha (playing ? 0.30f : 0.16f));
        g.fillRoundedRectangle (box.expanded (2.5f), corner + 2.5f);
        g.setColour (led.withAlpha (playing ? 0.40f : 0.24f));
        g.fillRoundedRectangle (box.expanded (1.2f), corner + 1.2f);

        // LED body with a lighter top half
        g.setGradientFill (juce::ColourGradient (led.brighter (0.25f), 0.0f, box.getY(),
                                                 led.darker (0.15f), 0.0f, box.getBottom(), false));
        g.fillRoundedRectangle (box, corner);
        g.setColour (juce::Colours::white.withAlpha (playing ? 0.35f : 0.18f));
        g.drawHorizontalLine ((int) box.getY() + 1, box.getX() + corner, box.getRight() - corner);

        // being edited: crisp white rim
        if (active)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawRoundedRectangle (box, corner, 1.0f);
        }
    }

    // ---- row glyph gutter, divided from the grid by a hairline
    g.setColour (Palette::line);
    g.fillRect (juce::Rectangle<float> ((float) (kIconCol - 2), gridY, 1.0f, gridH));

    static const char* const kStrokeGlyphs[6] = { "D", "U", "xD", "xU", "PD", "PU" };

    // Small-caps style captions, kept mixed-case: "xD" / "xU" carry meaning.
    g.setFont (Palette::fontSemi (9.5f).withExtraKerningFactor (0.06f));

    for (int row = 0; row < kNumRows; ++row)
    {
        const auto r = cellRect (row, 0);
        const juce::Rectangle<float> area (4.0f, r.getY(), (float) (kIconCol - 8), r.getHeight());
        const juce::String text = row < 6 ? juce::String (kStrokeGlyphs[row])
                                          : juce::String (row - 5);
        g.setColour (enabled && row == hoverRow ? Palette::text : Palette::caption);
        g.drawText (text, area, juce::Justification::centred, false);
    }

    g.restoreState();

    if (! enabled)
        g.endTransparencyLayer();
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

void SeqGrid::mouseMove (const juce::MouseEvent& e)
{
    // Hover is paint-only feedback: it never edits the pattern.
    const int col = colAt (e.position.x);
    const int row = col < 0 ? -1 : rowAt (e.position.y);
    const int c = row < 0 ? -1 : col;

    if (row != hoverRow || c != hoverCol)
    {
        hoverRow = row;
        hoverCol = c;
        repaint();
    }
}

void SeqGrid::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow >= 0 || hoverCol >= 0)
    {
        hoverRow = hoverCol = -1;
        repaint();
    }
}

void SeqGrid::resetDrag() noexcept
{
    dragMode = DragMode::none;
    dragRow = dragStart = 0;
    dragLen = 1;
    dragVel = 100;
}

} // namespace fw::ui

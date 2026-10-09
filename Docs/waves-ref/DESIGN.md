# Fretwork UI: Waves-style design traits

References (product hero renders, saved here):
- `atlas.png` / `atlas_crop.png`: https://www.waves.com/plugins/atlas-reverb
- `tune.png` / `tune_crop.png`: https://www.waves.com/plugins/waves-tune-real-time

## Traits observed

| Trait | Atlas Reverb | Waves Tune Real-Time | Fretwork mapping |
|---|---|---|---|
| Chassis | Near-black charcoal (#1b1c20) face inside a lighter textured bevel frame; rounded corners ~8 px | Two-tone: light brushed-grey upper deck (#d4d6d9), dark slate lower deck (#3a4049) | Charcoal face `#1a1b1f`, bevelled outer frame `#2c2e33` with 1 px top highlight; panels inset `#141518` |
| Accent | Violet LED (#9b6bff) with soft glow; warm amber pointer tip (#ffc46b) | Magenta (#d23fd6) for ON states, active key, meters | Single accent violet `#9d72ff` + amber tip `#f2b45a`; ON fill = violet |
| Knobs | Dark glossy cap, digital value readout in the cap; outer ring of LED dots that light up to the value; dim unlit dots for the rest | Cream knob with black skirt, tick scale, min/max labels under | Vector knob: dark radial-gradient cap, 1 px rim highlight, ring of LED dots (lit = violet, unlit = #34363c), amber dot at pointer |
| Value display | Violet 7-segment-style digits in a recessed LCD | White digits in black inset boxes with unit to the right | Inset value box `#0e0f11`, 1 px dark outline + 1 px light bottom edge, light numerals (Bahnschrift) |
| Meters | Vertical dotted LED columns at both edges, "In"/"Out" captions | Thin vertical bars with dB scale (-4, -12, -20, -28) | Output LED meter column on right edge of transport, dotted segments |
| Graph | Black LCD with thin grey grid, frequency labels, white curve, glowing handle dots | Arc tuner with segmented wedges | Fretboard + seq grid on black LCD with hairline grid; active cells violet glow |
| Buttons | Outlined text tab with accent border when active ("EQ") | Black rectangular buttons, 1 px light top bevel, white bold text; ON = accent fill | `drawButtonBackground`: dark gradient #26282d→#17181b, 1 px #000 outline, 1 px top highlight; hover +6% light; down inverted gradient; ON = violet fill; disabled = 40% alpha |
| Section headers | Small caps labels ("Pre Delay", "Decay") warm off-white | Small caps centred in a hairline-ruled group frame ("RANGE", "MIDI INPUT / KEYBOARD") | Group frames: 1 px #3a3d44 rounded rule, caption centred on the top rule in small caps #a9adb5 |
| Combo / selector | LCD line with left/right arrow steppers, glowing text | Black box, white bold text, separated drop-down arrow cell | LCD combo: inset black, violet text, separate arrow cell with divider |
| Typography | Geometric sans, labels ~13 px warm white (#e8dcc8) | Bold condensed sans, caps headers ~11 px | Labels Segoe UI Semibold 12, headers 10.5 caps tracked, numerals Bahnschrift |
| Keyboard | — | Flat white keys with thin grey separators, black keys with slight top highlight; active key filled with accent; per-key indicator strip above | White keys #e9eaec, black keys gradient #2a2b2f→#0c0c0e, pressed = violet, keyswitch band tinted |
| Spacing | Generous padding, ~16 px gutters, controls centred in their zones | Tight 8 px grid inside framed groups | 8 px grid; 12 px outer margin; 8 px gutters |

## Widget states

- Default: as above.
- Hover: +6% brightness on fill, rim highlight raised to 25% white.
- Active / down: gradient inverted, 1 px inner shadow.
- On / selected: accent fill, text #0d0d10, soft outer glow (2 px, accent 35%).
- Disabled: whole widget at 40% opacity, accent replaced by #55575d.

## JUCE mapping

`FretLookAndFeel`: drawRotarySlider, drawLinearSlider, drawComboBox, drawPopupMenuBackground, drawPopupMenuItem, drawButtonBackground, drawButtonText, drawToggleButton, drawTabButton, drawTabbedButtonBarBackground, drawScrollbar, fillTextEditorBackground, drawTextEditorOutline, drawLabel. Panel/section/meter drawing lives in `ui/WavesStyle.h` helpers used by each widget's paint().

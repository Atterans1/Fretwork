# Fretwork tests

One console binary, `FretworkTests`, built with the plugin:

    cmake --build build --config Release --target FretworkTests
    build\FretworkTests_artefacts\Release\FretworkTests.exe              # everything
    build\FretworkTests_artefacts\Release\FretworkTests.exe --list       # test names
    build\FretworkTests_artefacts\Release\FretworkTests.exe --only midi: # name filter

Exit code 0 means every check passed.

## Layout

| File | What |
|---|---|
| `Harness.h` | The fixture. `fwtest::Plugin` drives a real `FretworkAudioProcessor` like a host (parameters in real units, sample-accurate MIDI, host-sized blocks, save/load). `fwtest::Take` holds the audio and measures it (rms, peak, pitch, brightness, onsets, stereo side). `FW_TEST` / `EXPECT` register and assert. |
| `FunctionalTests.cpp` | User-facing behaviour, grouped by prefix: `params:`, `state:`, `midi:`, `output:`, `modes:`, `host:`, `editor:`. |
| `ParamGolden.inc` | The parameter contract (id, type, range, default). `params: layout` fails if any of them changes. Regenerate only for an intentional contract change. |
| `main.cpp` | The original DSP/engine tests (T1-T25) and the runner. |

## Writing a test

```cpp
FW_TEST ("midi: note sounds")
{
    fwtest::Plugin p;                          // 48 kHz, 480-sample blocks
    p.set ("room", 0.f);                       // real units
    p.noteOn (0.01, 52, 0.8f).noteOff (1.0, 52);
    const auto take = p.render (1.5);
    EXPECT (take.rms (0.05, 0.3) > 0.01);
}
```

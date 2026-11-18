# Spectral repair effect

A built-in Audacity effect that reconstructs a box-selected time-frequency
region from the surrounding spectro-temporal texture. It targets isolated,
localised events such as keyboard clicks and mouth ("saliva") noises without
the broadband dulling of sibilance that a conventional denoiser introduces.

## Where the code lives

- `src/effects/builtin_collection/spectralrepair/spectralrepair.{h,cpp}`
  - Dependency-free STFT engine (`SpectralRepair::Process`), plain C++17 so it
    can be unit-tested without wxWidgets or Qt.
- `src/effects/builtin_collection/spectralrepair/spectralrepaireffect.{h,cpp}`
  - Audacity `StatefulEffect` wrapper. Reads the box bounds from the time
    selection (`mT0/mT1`) and the spectral selection (`mF0/mF1`), processes
    every selected wave channel, and commits through `EffectOutputTracks`.
- `src/effects/builtin_collection/spectralrepair/spectralrepairviewmodel.{h,cpp}`
  and `SpectralRepairView.qml`
  - Parameters dialog (FFT size, texture frames/bins, strength, sensitivity).
- Registration: `internal/builtincollectionloader.cpp`,
  `builtin_effects_collection.qrc`, `CMakeLists.txt`.
- Spectrogram right-click menu: `SpectralEffectId::SpectralRepair` in
  `src/spectrogram/spectrogramtypes.h` and
  `src/spectrogram/view/trackspectrogramcontextmenumodel.cpp`.

## How the reconstruction works

1. The selected channel is analysed with a Hann-windowed, 50%-overlap STFT
   (default 2048-point FFT).
2. Every masked bin (the dilated time-frequency box) is compared against a
   texture level: the median magnitude of the bins in a
   `(2*textureFrames+1) x (2*textureBins+1)` neighbourhood, ignoring any bin
   that itself lies inside the mask so the transient cannot bias its own
   estimate. Mask frames and conjugate mirror bins are handled correctly.
3. Bins protruding above the texture by more than the sensitivity threshold
   are pulled back to the texture level using a linear-in-dB soft curve; bins
   at or below the texture (wanted voice, sibilance) keep their magnitude.
   The original phase is always preserved.
4. A global noise-floor fallback lets the effect remove events that sit in
   digital silence.
5. The frames are inverse transformed and overlap-added with squared-Hann
   COLA normalisation, which is identity away from the repaired box.

## Preview and undo

- **Preview** comes from the shared `DestructiveEffectsViewerDialog`, which
  calls `BuiltinEffectModel::startPreview`. Preview renders the same
  `SpectralRepairEffect::Process` on a temporary preview track, so parameter
  changes (including the strength slider) can be auditioned before applying.
- **Undo** requires no extra code: applying the effect runs inside the
  project `TransactionScope`, so a single undo step restores the original
  samples.

## Verification

Two equivalent test entry points exercise the DSP core:

- `artifacts/spectral_repair_test/run_host_tests.sh` — standalone, no Audacity
  dependencies. Run it with `bash artifacts/spectral_repair_test/run_host_tests.sh`.
- `src/effects/builtin_collection/tests/SpectralRepairTests.cpp` — gtest suite
  wired into `AU_BUILD_EFFECTS_TESTS`.

The keyboard-click fixture mixes voice-like tones and a 7 kHz sibilance band
with a short broadband click (4.5/6.2 kHz, ~12 ms). Measured results:

- Residual energy inside the boxed spectral band drops by ~27 dB after repair.
- Time-domain RMS inside the selection drops ~5 dB.
- Sibilance energy measured outside the click time box is unchanged (<5%).
- The repaired signal is ~62% closer (in squared error) to the clean reference
  than the contaminated input.
- FFT round-trip, STFT identity, zero-strength no-op, and invalid-request
  guards are all asserted.

Note: the full application cannot be compiled in this environment because the
`muse` framework submodule and Qt are not provisioned; the STFT engine and its
tests are therefore validated host-less, while the effect/UI layer follows the
exact patterns of existing built-in effects such as Click Removal.

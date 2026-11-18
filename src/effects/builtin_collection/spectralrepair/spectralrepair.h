/**********************************************************************

  Audacity: A Digital Audio Editor

  SpectralRepair.h

  Texture-based repair of a user-selected time-frequency rectangle.

  The selected time-frequency region is reconstructed from the
  spectro-temporal texture *around* it (neighbouring frames and frequency
  bands), instead of being zeroed. This removes isolated events such as
  keyboard clicks or mouth noises while leaving sibilants outside the
  selection untouched.

  The algorithm is a self-contained, dependency-free STFT engine so that
  it can be exercised by host-less unit tests as well as from the Audacity
  effect wrapper.

**********************************************************************/
#pragma once

#include <cstddef>
#include <vector>

namespace au::effects {

struct SpectralRepairRegion
{
    //! Sample indices (relative to the processed buffer) of the selection.
    size_t startSample = 0;
    size_t endSample = 0;
    //! Frequency bounds of the selection in Hz. A negative value means
    //! "open bound" (down to DC / up to Nyquist).
    double lowHz = -1.0;
    double highHz = -1.0;
};

struct SpectralRepairSettings
{
    //! FFT size in samples. Must be a power of two.
    size_t fftSize = 2048;
    //! Number of neighbouring STFT frames (each side) used to learn the
    //! surrounding texture.
    int textureFrames = 6;
    //! Number of neighbouring frequency bins (each side) used to learn the
    //! surrounding texture.
    int textureBins = 4;
    //! How much of the estimated texture replaces the selection:
    //! 0 = keep original, 1 = full replacement.
    float strength = 1.0f;
    //! Soft-gate sensitivity in dB. Bins whose magnitude exceeds the
    //! surrounding texture by more than this many dB are pulled all the way
    //! down to the texture level; smaller excesses are attenuated smoothly.
    float gateThresholdDb = 6.0f;
    //! Extra dilation of the repaired rectangle in frames.
    int toleranceFrames = 1;
    //! Extra dilation of the repaired rectangle in frequency bins.
    int toleranceBins = 1;
};

class SpectralRepair
{
public:
    //! Repairs @p data in place. @p sampleRate is needed to map the
    //! frequency bounds of @p region onto FFT bins. Returns false when the
    //! request is invalid (empty region or non power-of-two FFT size).
    static bool Process(
        std::vector<float>& data, double sampleRate,
        const SpectralRepairRegion& region, const SpectralRepairSettings& settings);

    //! Radix-2 iterative in-place FFT. Size must be a power of two.
    //! When @p inverse is true the result is divided by the transform size.
    static void Fft(std::vector<float>& real, std::vector<float>& imag, bool inverse);

private:
    static void MakeHannWindow(size_t size, std::vector<float>& window);
};

}

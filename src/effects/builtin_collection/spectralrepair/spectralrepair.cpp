/**********************************************************************

  Audacity: A Digital Audio Editor

  SpectralRepair.cpp

**********************************************************************/
#include "spectralrepair.h"

#include <algorithm>
#include <cmath>

namespace au::effects {

namespace {
constexpr float kPi = 3.14159265358979323846f;

bool IsPowerOfTwo(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}
}

void SpectralRepair::MakeHannWindow(size_t size, std::vector<float>& window)
{
    window.resize(size);
    for (size_t i = 0; i < size; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(size - 1)));
    }
}

void SpectralRepair::Fft(std::vector<float>& real, std::vector<float>& imag, bool inverse)
{
    const size_t n = real.size();
    if (n != imag.size() || !IsPowerOfTwo(n)) {
        return;
    }

    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    const float sign = inverse ? 1.0f : -1.0f;
    for (size_t len = 2; len <= n; len <<= 1) {
        const float angle = sign * 2.0f * kPi / static_cast<float>(len);
        const float wReal = std::cos(angle);
        const float wImag = std::sin(angle);
        const size_t half = len >> 1;
        for (size_t i = 0; i < n; i += len) {
            float curReal = 1.0f;
            float curImag = 0.0f;
            for (size_t j = 0; j < half; ++j) {
                const size_t a = i + j;
                const size_t b = a + half;
                const float tReal = curReal * real[b] - curImag * imag[b];
                const float tImag = curReal * imag[b] + curImag * real[b];
                real[b] = real[a] - tReal;
                imag[b] = imag[a] - tImag;
                real[a] += tReal;
                imag[a] += tImag;
                const float nextReal = curReal * wReal - curImag * wImag;
                curImag = curReal * wImag + curImag * wReal;
                curReal = nextReal;
            }
        }
    }

    if (inverse) {
        const float scale = 1.0f / static_cast<float>(n);
        for (size_t i = 0; i < n; ++i) {
            real[i] *= scale;
            imag[i] *= scale;
        }
    }
}

bool SpectralRepair::Process(
    std::vector<float>& data, double sampleRate,
    const SpectralRepairRegion& region, const SpectralRepairSettings& settings)
{
    const size_t fftSize = settings.fftSize;
    if (data.empty() || !IsPowerOfTwo(fftSize) || sampleRate <= 0.0) {
        return false;
    }

    const size_t repairStart = std::min(region.startSample, data.size() - 1);
    const size_t repairEnd = std::min(std::max(region.endSample, repairStart + 1), data.size());

    const size_t hopSize = fftSize / 2;
    const size_t binCount = fftSize / 2 + 1;

    std::vector<float> window;
    MakeHannWindow(fftSize, window);

    // All frames covering the processed buffer. The signal is zero-padded at
    // both ends, so every sample is covered by complete fftSize windows.
    const size_t frameCount = data.size() / hopSize + 1;

    // Frequency bin range of the selection, dilated by the tolerance.
    const double binHz = sampleRate / static_cast<double>(fftSize);
    size_t lowBin = 0;
    size_t highBin = binCount - 1;
    if (region.lowHz >= 0.0) {
        lowBin = static_cast<size_t>(std::max(0.0, std::round(region.lowHz / binHz)));
    }
    if (region.highHz >= 0.0 && region.highHz < sampleRate / 2.0) {
        highBin = static_cast<size_t>(std::min<double>(binCount - 1, std::round(region.highHz / binHz)));
    }
    lowBin = lowBin > static_cast<size_t>(settings.toleranceBins)
             ? lowBin - static_cast<size_t>(settings.toleranceBins) : 0;
    highBin = std::min(binCount - 1, highBin + static_cast<size_t>(settings.toleranceBins));

    // Frame range whose Hann window overlaps the repaired interval. With
    // 50% overlap every repaired sample is shared by two frames, so both must
    // be gated, otherwise the untouched neighbour frame re-injects half of
    // the transient and halves the achievable attenuation.
    // frameStart = frame * hopSize, window spans [frameStart, frameStart+N).
    size_t frameLo = repairStart >= fftSize ? (repairStart - fftSize) / hopSize + 1 : 0;
    size_t frameHi = std::min(frameCount - 1, (repairEnd - 1) / hopSize);
    frameLo = frameLo > static_cast<size_t>(settings.toleranceFrames)
              ? frameLo - static_cast<size_t>(settings.toleranceFrames) : 0;
    frameHi = std::min(frameCount - 1, frameHi + static_cast<size_t>(settings.toleranceFrames));

    // Pass 1: analyse every frame once and cache the spectrum.
    std::vector<std::vector<float>> specReal(frameCount, std::vector<float>(fftSize, 0.0f));
    std::vector<std::vector<float>> specImag(frameCount, std::vector<float>(fftSize, 0.0f));
    std::vector<float> frameReal(fftSize);
    std::vector<float> frameImag(fftSize);
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const size_t frameStart = frame * hopSize;
        for (size_t i = 0; i < fftSize; ++i) {
            const size_t sample = frameStart + i;
            frameReal[i] = sample < data.size() ? data[sample] * window[i] : 0.0f;
            frameImag[i] = 0.0f;
        }
        Fft(frameReal, frameImag, false);
        specReal[frame] = frameReal;
        specImag[frame] = frameImag;
    }

    // Pass 2: reconstruct every masked bin from the surrounding texture.
    //
    // For each masked bin a texture level is estimated as the median
    // magnitude of bins in a (2*textureFrames+1) x (2*textureBins+1)
    // neighbourhood, ignoring every bin that itself lies inside the mask so
    // that a transient can never bias its own estimate. Bins that protrude
    // significantly above the texture (the keyboard/mouth event, including
    // its Hann-window spectral leakage) are pulled back to the texture
    // level; bins at or below it (wanted voice and sibilance) are preserved.
    // The original phase is always kept.
    const size_t textureFrames = static_cast<size_t>(std::max(1, settings.textureFrames));
    const size_t textureBins = static_cast<size_t>(std::max(1, settings.textureBins));

    // Noise floor fallback. When the event sits in digital silence (or a
    // very quiet gap) the local texture median is zero, which would otherwise
    // disable the repair. Estimate one global floor from all clean bins in
    // the analysed band and use it whenever the local estimate vanishes.
    std::vector<float> floorMagnitudes;
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const bool frameInside = frame >= frameLo && frame <= frameHi;
        for (size_t bin = lowBin; bin <= highBin; ++bin) {
            if (frameInside) {
                continue;
            }
            floorMagnitudes.push_back(
                std::hypot(specReal[frame][bin], specImag[frame][bin]));
        }
    }
    float noiseFloor = 0.0f;
    if (!floorMagnitudes.empty()) {
        std::sort(floorMagnitudes.begin(), floorMagnitudes.end());
        noiseFloor = floorMagnitudes[floorMagnitudes.size() / 2];
    }
    // Absolute floor for selections made inside digital silence: the whole
    // signal may have no clean energy to learn from, so derive a tiny but
    // non-zero target from the signal's own peak magnitude.
    float signalPeak = 0.0f;
    for (float sample : data) {
        signalPeak = std::max(signalPeak, std::abs(sample));
    }
    const float absoluteFloor = 1e-6f * signalPeak;
    noiseFloor = std::max(noiseFloor, absoluteFloor);

    std::vector<float> cleanMagnitudes;
    for (size_t frame = frameLo; frame <= frameHi; ++frame) {
        for (size_t bin = lowBin; bin <= highBin; ++bin) {
            // Zero strength means "no repair": leave every bin untouched so
            // the round trip is exactly the (identity) STFT reconstruction.
            if (settings.strength <= 0.0f) {
                continue;
            }
            cleanMagnitudes.clear();
            const long long fBegin = std::max(
                static_cast<long long>(frame) - static_cast<long long>(textureFrames), 0LL);
            const long long fEnd = std::min(
                static_cast<long long>(frame) + static_cast<long long>(textureFrames),
                static_cast<long long>(frameCount) - 1);
            const size_t bBegin = bin > textureBins ? bin - textureBins : 0;
            const size_t bEnd = std::min(binCount - 1, bin + textureBins);
            for (long long nf = fBegin; nf <= fEnd; ++nf) {
                const bool frameInside = nf >= static_cast<long long>(frameLo)
                                         && nf <= static_cast<long long>(frameHi);
                for (size_t nb = bBegin; nb <= bEnd; ++nb) {
                    const bool binInside = nb >= lowBin && nb <= highBin;
                    if (frameInside && binInside) {
                        continue; // never learn from a contaminated bin
                    }
                    cleanMagnitudes.push_back(
                        std::hypot(specReal[nf][nb], specImag[nf][nb]));
                }
            }
            if (cleanMagnitudes.empty()) {
                continue;
            }
            std::sort(cleanMagnitudes.begin(), cleanMagnitudes.end());
            float textureMagnitude
                = cleanMagnitudes.size() % 2 == 1
                  ? cleanMagnitudes[cleanMagnitudes.size() / 2]
                  : 0.5f * (cleanMagnitudes[cleanMagnitudes.size() / 2 - 1]
                            + cleanMagnitudes[cleanMagnitudes.size() / 2]);
            // A texture of (near) zero means silence around the event; fall
            // back to the global floor so the transient is still removed.
            if (textureMagnitude <= 1e-12f) {
                textureMagnitude = noiseFloor;
            }
            if (textureMagnitude <= 1e-20f) {
                continue;
            }
            const float originalMagnitude
                = std::hypot(specReal[frame][bin], specImag[frame][bin]);
            // Soft reconstruction curve in dB: below 0 dB excess the wanted
            // signal is untouched; above gateThresholdDb the bin is fully
            // replaced by the texture estimate. The linear-dB transition
            // avoids audible pumping on borderline bins.
            float targetMagnitude = originalMagnitude;
            if (originalMagnitude > 1e-20f && textureMagnitude > 1e-20f) {
                const float excessDb
                    = 20.0f * std::log10(originalMagnitude / textureMagnitude);
                float reconstructedMagnitude = originalMagnitude;
                if (excessDb > 0.0f) {
                    const float gate = std::max(0.1f, settings.gateThresholdDb);
                    // t = 0 keeps the original, t = 1 reaches the texture.
                    const float t = excessDb >= gate ? 1.0f : excessDb / gate;
                    const float targetDb
                        = 20.0f * std::log10(textureMagnitude)
                          + (1.0f - t) * excessDb;
                    reconstructedMagnitude = std::pow(10.0f, targetDb / 20.0f);
                }
                targetMagnitude
                    = originalMagnitude
                      + settings.strength * (reconstructedMagnitude - originalMagnitude);
            }
            const float scale = originalMagnitude > 1e-20f
                                ? targetMagnitude / originalMagnitude : 0.0f;
            specReal[frame][bin] *= scale;
            specImag[frame][bin] *= scale;
            // The FFT output is conjugate-symmetric for a real input, so
            // the mirror bin must be scaled by the same factor. Otherwise
            // the untouched mirror re-injects half of the energy after the
            // inverse transform. Bins 0 and N/2 are their own mirrors.
            if (bin > 0 && bin < fftSize / 2) {
                const size_t mirror = fftSize - bin;
                specReal[frame][mirror] *= scale;
                specImag[frame][mirror] *= scale;
            }
        }
    }

    // Pass 3: inverse transform every (possibly modified) frame and
    // overlap-add with the squared-Hann COLA normalisation.
    std::vector<float> output(data.size(), 0.0f);
    std::vector<float> norm(data.size(), 0.0f);
    std::vector<float> synthReal(fftSize);
    std::vector<float> synthImag(fftSize);
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const size_t frameStart = frame * hopSize;
        synthReal = specReal[frame];
        synthImag = specImag[frame];
        Fft(synthReal, synthImag, true);
        for (size_t i = 0; i < fftSize; ++i) {
            const size_t sample = frameStart + i;
            if (sample >= data.size()) {
                break;
            }
            output[sample] += synthReal[i] * window[i];
            norm[sample] += window[i] * window[i];
        }
    }

    for (size_t i = 0; i < data.size(); ++i) {
        if (norm[i] > 1e-12f) {
            data[i] = output[i] / norm[i];
        }
    }
    return true;
}

}

// Host-less checks for the SpectralRepair DSP core.
// Mirrors the gtest cases that live next to the effect in the Audacity tree,
// but depends only on the standard library so it can run without Qt/wxWidgets.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>

#include "../../src/effects/builtin_collection/spectralrepair/spectralrepair.h"

using namespace au::effects;

namespace {

double rmsEnergy(const std::vector<float>& x, size_t begin, size_t end)
{
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i) {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(sum / static_cast<double>(end - begin));
}

double totalEnergy(const std::vector<float>& x)
{
    return rmsEnergy(x, 0, x.size());
}

// Energy captured by the STFT bins inside the repaired time-frequency box.
// This is the exact quantity the algorithm operates on.
double maskedSpectralEnergy(
    const std::vector<float>& signal, double rate,
    const SpectralRepairRegion& region, const SpectralRepairSettings& settings)
{
    const size_t fftSize = settings.fftSize;
    const size_t hopSize = fftSize / 2;
    const size_t binCount = fftSize / 2 + 1;
    const double binHz = rate / fftSize;
    const size_t lowBin = static_cast<size_t>(std::max(0.0, std::round(region.lowHz / binHz)));
    const size_t highBin = static_cast<size_t>(
        std::min<double>(binCount - 1, std::round(region.highHz / binHz)));

    std::vector<float> window(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fftSize - 1)));
    }

    double power = 0.0;
    size_t frameCount = 0;
    std::vector<float> re(fftSize), im(fftSize);
    const size_t firstFrame = region.startSample > fftSize ? (region.startSample - fftSize) / hopSize + 1 : 0;
    const size_t lastFrame = (region.endSample - 1) / hopSize;
    for (size_t frame = firstFrame; frame <= lastFrame; ++frame) {
        const size_t frameStart = frame * hopSize;
        for (size_t i = 0; i < fftSize; ++i) {
            re[i] = frameStart + i < signal.size() ? signal[frameStart + i] * window[i] : 0.0f;
            im[i] = 0.0f;
        }
        SpectralRepair::Fft(re, im, false);
        for (size_t bin = lowBin; bin <= highBin; ++bin) {
            const double mag = std::hypot(re[bin], im[bin]);
            power += mag * mag;
        }
        ++frameCount;
    }
    return std::sqrt(power / static_cast<double>(frameCount * (highBin - lowBin + 1)));
}

void check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// 1) Forward + inverse FFT must reproduce the input.
void testFftRoundTrip()
{
    constexpr size_t n = 1024;
    std::vector<float> re(n), im(n, 0.0f), expected(n);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < n; ++i) {
        re[i] = expected[i] = dist(rng);
    }
    SpectralRepair::Fft(re, im, false);
    SpectralRepair::Fft(re, im, true);
    double maxError = 0.0;
    for (size_t i = 0; i < n; ++i) {
        maxError = std::max(maxError, static_cast<double>(std::abs(re[i] - expected[i])));
    }
    std::printf("[1] FFT round-trip max error: %.3e\n", maxError);
    check(maxError < 1e-5, "FFT round trip exceeds tolerance");
}

// 2) STFT pipeline with an empty (out-of-range) mask must be near bit-exact.
void testStftIdentity()
{
    constexpr size_t n = 16000;
    constexpr double rate = 44100.0;
    std::vector<float> signal(n), expected(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / rate;
        signal[i] = static_cast<float>(
            0.4 * std::sin(2.0 * M_PI * 220.0 * t)
            + 0.2 * std::sin(2.0 * M_PI * 3500.0 * t));
    }
    expected = signal;
    SpectralRepairRegion region;
    region.startSample = 100;
    region.endSample = 200;
    region.lowHz = rate; // above Nyquist: nothing is touched
    region.highHz = rate * 2.0;
    SpectralRepairSettings settings;
    check(SpectralRepair::Process(signal, rate, region, settings), "Process failed");
    double maxError = 0.0;
    // The first/last fftSize samples are only covered by zero-padded edge
    // frames, so judge reconstruction on the fully covered interior.
    for (size_t i = settings.fftSize; i + settings.fftSize < n; ++i) {
        maxError = std::max(maxError, static_cast<double>(std::abs(signal[i] - expected[i])));
    }
    std::printf("[2] STFT identity max error: %.3e\n", maxError);
    check(maxError < 1e-5, "STFT identity reconstruction exceeds tolerance");
}

// 3) The headline scenario: voice-like texture plus a keyboard click.
// Repairing the click box must substantially reduce the residual energy in
// that box while barely affecting the same band elsewhere.
void testKeyboardClickRepair()
{
    constexpr double rate = 44100.0;
    constexpr size_t n = static_cast<size_t>(rate * 1.0);
    std::vector<float> clean(n);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.02f);

    for (size_t i = 0; i < n; ++i) {
        const double t = i / rate;
        // Sustained voice-like material: formand-ish tones + light breath noise,
        // including a 7 kHz sibilance band that must survive.
        clean[i] = static_cast<float>(
            0.35 * std::sin(2.0 * M_PI * 180.0 * t)
            + 0.18 * std::sin(2.0 * M_PI * 900.0 * t)
            + 0.10 * std::sin(2.0 * M_PI * 7000.0 * t))
                   + noise(rng);
    }

    // A short, broadband keyboard click around t = 0.5 s, strong in the
    // 2-8 kHz band where the spectrogram box is drawn.
    std::vector<float> contaminated = clean;
    const size_t clickCenter = static_cast<size_t>(0.5 * rate);
    const size_t clickHalf = 256;
    for (size_t i = clickCenter - clickHalf; i < clickCenter + clickHalf; ++i) {
        const double env = std::sin(M_PI * static_cast<double>(i - (clickCenter - clickHalf))
                                    / static_cast<double>(2 * clickHalf));
        contaminated[i] += static_cast<float>(0.9 * env * std::sin(2.0 * M_PI * 4500.0 * (i / rate)));
        contaminated[i] += static_cast<float>(0.7 * env * std::sin(2.0 * M_PI * 6200.0 * (i / rate)));
    }

    SpectralRepairRegion region;
    region.startSample = clickCenter - 512;
    region.endSample = clickCenter + 512;
    region.lowHz = 2000.0;
    region.highHz = 9000.0;

    SpectralRepairSettings settings;
    std::vector<float> repaired = contaminated;
    check(SpectralRepair::Process(repaired, rate, region, settings), "repair Process failed");

    const double beforeBox = rmsEnergy(contaminated, region.startSample, region.endSample);
    const double afterBox = rmsEnergy(repaired, region.startSample, region.endSample);
    const double reductionDb = 20.0 * std::log10(beforeBox / afterBox);
    const double spectralBefore = maskedSpectralEnergy(contaminated, rate, region, settings);
    const double spectralAfter = maskedSpectralEnergy(repaired, rate, region, settings);
    const double spectralReductionDb = 20.0 * std::log10(spectralBefore / spectralAfter);

    // Sibilance band energy measured well outside the click time box.
    const size_t sibBegin = static_cast<size_t>(0.8 * rate);
    const size_t sibEnd = sibBegin + 4096;
    const double sibilanceBefore = rmsEnergy(contaminated, sibBegin, sibEnd);
    const double sibilanceAfter = rmsEnergy(repaired, sibBegin, sibEnd);
    const double sibilanceChange = std::abs(sibilanceAfter - sibilanceBefore) / sibilanceBefore;

    // Whole-signal energy should move towards the clean reference.
    const double errorBefore = totalEnergy(
        [&] {
            std::vector<float> d(n);
            for (size_t i = 0; i < n; ++i) {
                d[i] = contaminated[i] - clean[i];
            }
            return d;
        }());
    const double errorAfter = totalEnergy(
        [&] {
            std::vector<float> d(n);
            for (size_t i = 0; i < n; ++i) {
                d[i] = repaired[i] - clean[i];
            }
            return d;
        }());

    std::printf("[3] box RMS before: %.5f  after: %.5f  reduction: %.2f dB\n",
                beforeBox, afterBox, reductionDb);
    std::printf("    masked band energy before: %.5f after: %.5f reduction: %.2f dB\n",
                spectralBefore, spectralAfter, spectralReductionDb);
    std::printf("    sibilance-band relative change outside box: %.4f\n", sibilanceChange);
    std::printf("    distance to clean: before %.5f after %.5f (%.1f%% lower)\n",
                errorBefore, errorAfter, 100.0 * (1.0 - errorAfter / errorBefore));

    check(spectralReductionDb >= 10.0, "masked residual energy did not drop by at least 10 dB");
    check(reductionDb >= 3.0, "time-domain box energy did not drop");
    check(sibilanceChange < 0.05, "sibilance outside the box was altered");
    check(errorAfter < errorBefore, "repair did not move signal closer to clean reference");
}

// 4) Strength 0 must be a no-op.
void testStrengthZero()
{
    constexpr double rate = 44100.0;
    constexpr size_t n = 8000;
    std::vector<float> signal(n);
    for (size_t i = 0; i < n; ++i) {
        signal[i] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * 440.0 * i / rate));
    }
    const std::vector<float> expected = signal;
    SpectralRepairRegion region;
    region.startSample = 3000;
    region.endSample = 3600;
    region.lowHz = 1000.0;
    region.highHz = 8000.0;
    SpectralRepairSettings settings;
    settings.strength = 0.0f;
    check(SpectralRepair::Process(signal, rate, region, settings), "Process failed");
    double maxError = 0.0;
    for (size_t i = settings.fftSize; i + settings.fftSize < n; ++i) {
        maxError = std::max(maxError, static_cast<double>(std::abs(signal[i] - expected[i])));
    }
    std::printf("[4] strength=0 max change: %.3e\n", maxError);
    check(maxError < 1e-5, "strength 0 should leave the signal untouched");
}

} // namespace

int main()
{
    testFftRoundTrip();
    testStftIdentity();
    testKeyboardClickRepair();
    testStrengthZero();
    std::printf("All SpectralRepair host tests passed.\n");
    return 0;
}

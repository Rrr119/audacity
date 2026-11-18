/*
 * Audacity: A Digital Audio Editor
 */
#include "effects/builtin_collection/spectralrepair/spectralrepair.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

namespace au::effects {
namespace {

constexpr double kSampleRate = 44100.0;

double rmsEnergy(const std::vector<float>& x, size_t begin, size_t end)
{
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i) {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(sum / static_cast<double>(end - begin));
}

// Energy of the STFT bins that fall inside the repaired time-frequency box.
double maskedSpectralEnergy(
    const std::vector<float>& signal, const SpectralRepairRegion& region,
    const SpectralRepairSettings& settings)
{
    const size_t fftSize = settings.fftSize;
    const size_t hopSize = fftSize / 2;
    const size_t binCount = fftSize / 2 + 1;
    const double binHz = kSampleRate / fftSize;
    const size_t lowBin = static_cast<size_t>(std::max(0.0, std::round(region.lowHz / binHz)));
    const size_t highBin = static_cast<size_t>(
        std::min<double>(binCount - 1, std::round(region.highHz / binHz)));

    std::vector<float> window(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fftSize - 1)));
    }

    const size_t firstFrame
        = region.startSample > fftSize ? (region.startSample - fftSize) / hopSize + 1 : 0;
    const size_t lastFrame = (region.endSample - 1) / hopSize;

    double power = 0.0;
    size_t frameCount = 0;
    std::vector<float> re(fftSize), im(fftSize);
    for (size_t frame = firstFrame; frame <= lastFrame; ++frame) {
        const size_t frameStart = frame * hopSize;
        for (size_t i = 0; i < fftSize; ++i) {
            re[i] = frameStart + i < signal.size()
                    ? signal[frameStart + i] * window[i] : 0.0f;
            im[i] = 0.0f;
        }
        SpectralRepair::Fft(re, im, false);
        for (size_t bin = lowBin; bin <= highBin; ++bin) {
            const double magnitude = std::hypot(re[bin], im[bin]);
            power += magnitude * magnitude;
        }
        ++frameCount;
    }
    return std::sqrt(power / static_cast<double>(frameCount * (highBin - lowBin + 1)));
}

// Voice-like material plus a short, broadband keyboard click.
struct KeyboardFixture {
    std::vector<float> clean;
    std::vector<float> contaminated;
    SpectralRepairRegion region;

    explicit KeyboardFixture(size_t samples = static_cast<size_t>(kSampleRate))
    {
        std::mt19937 rng(7);
        std::normal_distribution<float> breath(0.0f, 0.02f);
        clean.resize(samples);
        for (size_t i = 0; i < samples; ++i) {
            const double t = i / kSampleRate;
            clean[i] = static_cast<float>(
                0.35 * std::sin(2.0 * M_PI * 180.0 * t)
                + 0.18 * std::sin(2.0 * M_PI * 900.0 * t)
                + 0.10 * std::sin(2.0 * M_PI * 7000.0 * t))
                       + breath(rng);
        }

        contaminated = clean;
        const size_t clickCenter = static_cast<size_t>(0.5 * kSampleRate);
        const size_t clickHalf = 256;
        for (size_t i = clickCenter - clickHalf; i < clickCenter + clickHalf; ++i) {
            const double envelope
                = std::sin(M_PI * static_cast<double>(i - (clickCenter - clickHalf))
                           / static_cast<double>(2 * clickHalf));
            contaminated[i] += static_cast<float>(
                0.9 * envelope * std::sin(2.0 * M_PI * 4500.0 * (i / kSampleRate)));
            contaminated[i] += static_cast<float>(
                0.7 * envelope * std::sin(2.0 * M_PI * 6200.0 * (i / kSampleRate)));
        }

        region.startSample = clickCenter - 512;
        region.endSample = clickCenter + 512;
        region.lowHz = 2000.0;
        region.highHz = 9000.0;
    }
};

} // namespace

TEST(SpectralRepairTests, fft_round_trip_reconstructs_input)
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
    EXPECT_LT(maxError, 1e-5);
}

TEST(SpectralRepairTests, out_of_band_mask_is_identity_in_interior)
{
    constexpr size_t n = 16000;
    std::vector<float> signal(n), expected(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / kSampleRate;
        signal[i] = static_cast<float>(
            0.4 * std::sin(2.0 * M_PI * 220.0 * t)
            + 0.2 * std::sin(2.0 * M_PI * 3500.0 * t));
    }
    expected = signal;

    SpectralRepairRegion region;
    region.startSample = 100;
    region.endSample = 200;
    region.lowHz = kSampleRate; // above Nyquist: nothing is touched
    region.highHz = 2.0 * kSampleRate;
    SpectralRepairSettings settings;

    ASSERT_TRUE(SpectralRepair::Process(signal, kSampleRate, region, settings));

    double maxError = 0.0;
    for (size_t i = settings.fftSize; i + settings.fftSize < n; ++i) {
        maxError = std::max(maxError, static_cast<double>(std::abs(signal[i] - expected[i])));
    }
    EXPECT_LT(maxError, 1e-5);
}

TEST(SpectralRepairTests, keyboard_click_residual_energy_is_reduced)
{
    KeyboardFixture fixture;
    SpectralRepairSettings settings;

    std::vector<float> repaired = fixture.contaminated;
    ASSERT_TRUE(SpectralRepair::Process(repaired, kSampleRate, fixture.region, settings));

    const double before
        = maskedSpectralEnergy(fixture.contaminated, fixture.region, settings);
    const double after
        = maskedSpectralEnergy(repaired, fixture.region, settings);
    const double reductionDb = 20.0 * std::log10(before / after);

    // The headline requirement: residual energy in the boxed band drops
    // substantially (well over 10 dB) after the texture reconstruction.
    EXPECT_GE(reductionDb, 10.0);

    // The time-domain box energy drops as well.
    const double timeBefore
        = rmsEnergy(fixture.contaminated, fixture.region.startSample, fixture.region.endSample);
    const double timeAfter
        = rmsEnergy(repaired, fixture.region.startSample, fixture.region.endSample);
    EXPECT_GT(timeBefore, timeAfter);

    // Sibilance measured well outside the click time box is preserved.
    const size_t sibBegin = static_cast<size_t>(0.8 * kSampleRate);
    const size_t sibEnd = sibBegin + 4096;
    const double sibBefore = rmsEnergy(fixture.contaminated, sibBegin, sibEnd);
    const double sibAfter = rmsEnergy(repaired, sibBegin, sibEnd);
    EXPECT_LT(std::abs(sibAfter - sibBefore) / sibBefore, 0.05);

    // The repaired signal is closer to the clean reference than the input.
    double errorBefore = 0.0;
    double errorAfter = 0.0;
    for (size_t i = 0; i < fixture.clean.size(); ++i) {
        errorBefore += static_cast<double>(fixture.contaminated[i] - fixture.clean[i])
                       * (fixture.contaminated[i] - fixture.clean[i]);
        errorAfter += static_cast<double>(repaired[i] - fixture.clean[i])
                      * (repaired[i] - fixture.clean[i]);
    }
    EXPECT_LT(errorAfter, errorBefore);
}

TEST(SpectralRepairTests, zero_strength_is_a_no_op)
{
    constexpr size_t n = 8000;
    std::vector<float> signal(n), expected(n);
    for (size_t i = 0; i < n; ++i) {
        signal[i] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * 440.0 * i / kSampleRate));
    }
    expected = signal;

    SpectralRepairRegion region;
    region.startSample = 3000;
    region.endSample = 3600;
    region.lowHz = 1000.0;
    region.highHz = 8000.0;
    SpectralRepairSettings settings;
    settings.strength = 0.0f;

    ASSERT_TRUE(SpectralRepair::Process(signal, kSampleRate, region, settings));

    double maxError = 0.0;
    for (size_t i = settings.fftSize; i + settings.fftSize < n; ++i) {
        maxError = std::max(maxError, static_cast<double>(std::abs(signal[i] - expected[i])));
    }
    EXPECT_LT(maxError, 1e-5);
}

TEST(SpectralRepairTests, invalid_request_returns_false)
{
    std::vector<float> empty;
    SpectralRepairRegion region;
    SpectralRepairSettings settings;
    EXPECT_FALSE(SpectralRepair::Process(empty, kSampleRate, region, settings));

    std::vector<float> signal(1024, 0.5f);
    settings.fftSize = 1000; // not a power of two
    EXPECT_FALSE(SpectralRepair::Process(signal, kSampleRate, region, settings));
}
}

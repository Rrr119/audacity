/**********************************************************************

  Audacity: A Digital Audio Editor

  SpectralRepairEffect.cpp

******************************************************************//**

\class SpectralRepairEffect
\brief Texture-based repair of a box-selected time-frequency region.

*//*******************************************************************/
#include "spectralrepaireffect.h"

#include <cmath>

#include "au3-effects/EffectOutputTracks.h"
#include "au3-command-parameters/ShuttleAutomation.h"
#include "au3-strings/TranslatableString.h"
#include "au3-wave-track/WaveTrack.h"

namespace au::effects {
namespace {
constexpr int kUndefinedFrequency = -1;
}

const ComponentInterfaceSymbol SpectralRepairEffect::Symbol {
    TranslatableString("effects-spectralrepair", "Spectral repair")
};

const EffectParameterMethods& SpectralRepairEffect::Parameters() const
{
    static CapturedParameters<
        SpectralRepairEffect, FftSizeLog2, TextureFrames, TextureBins, Strength, GateThresholdDb> parameters;
    return parameters;
}

SpectralRepairEffect::SpectralRepairEffect()
{
    Parameters().Reset(*this);
    SetLinearEffectFlag(true);
}

SpectralRepairEffect::~SpectralRepairEffect() = default;

ComponentInterfaceSymbol SpectralRepairEffect::GetSymbol() const
{
    return Symbol;
}

TranslatableString SpectralRepairEffect::GetDescription() const
{
    return TranslatableString(
        "effects-spectralrepair",
        "Reconstructs the selected time-frequency region from the surrounding texture, "
        "removing isolated keyboard clicks and mouth noises while preserving sibilance");
}

::EffectType SpectralRepairEffect::GetType() const
{
    return EffectTypeProcess;
}

bool SpectralRepairEffect::CheckWhetherSkipEffect(const EffectSettings&) const
{
    return mStrength <= 0.0f;
}

SpectralRepairSettings SpectralRepairEffect::repairSettings() const
{
    SpectralRepairSettings settings;
    settings.fftSize = static_cast<size_t>(1) << mFftSizeLog2;
    settings.textureFrames = mTextureFrames;
    settings.textureBins = mTextureBins;
    settings.strength = mStrength;
    settings.gateThresholdDb = mGateThresholdDb;
    return settings;
}

bool SpectralRepairEffect::Process(::EffectInstance&, EffectSettings&)
{
    EffectOutputTracks outputs { *mTracks, GetType(), { { mT0, mT1 } } };
    bool goodResult = true;

    int count = 0;
    for (auto track : outputs.Get().Selected<WaveTrack>()) {
        const double trackStart = track->GetStartTime();
        const double trackEnd = track->GetEndTime();
        const double t0 = std::max(mT0, trackStart);
        const double t1 = std::min(mT1, trackEnd);
        if (t1 <= t0) {
            continue;
        }

        const double rate = track->GetRate();
        const auto startSample = track->TimeToLongSamples(t0);
        const auto endSample = track->TimeToLongSamples(t1);

        SpectralRepairRegion region;
        region.startSample = 0;
        region.endSample = (endSample - startSample).as_size_t();
        // mF0/mF1 carry the box frequency bounds when a spectral selection
        // exists. A negative value means "open bound" and is passed through.
        region.lowHz = mF0 == kUndefinedFrequency ? -1.0 : mF0;
        region.highHz = mF1 == kUndefinedFrequency ? -1.0 : mF1;

        for (const auto pChannel : track->Channels()) {
            if (!ProcessOne(count++, *pChannel, startSample, endSample - startSample, rate, region)) {
                goodResult = false;
                goto done;
            }
        }
    }

done:
    if (goodResult) {
        outputs.Commit();
    }
    return goodResult;
}

bool SpectralRepairEffect::ProcessOne(
    int count, WaveChannel& channel, sampleCount start, sampleCount len,
    double sampleRate, const SpectralRepairRegion& region)
{
    if (len <= 0) {
        return true;
    }

    const size_t length = len.as_size_t();
    std::vector<float> buffer(length);
    channel.GetFloats(buffer.data(), start, length);

    SpectralRepairRegion localRegion = region;
    localRegion.endSample = std::min(localRegion.endSample, length);

    const bool changed = SpectralRepair::Process(buffer, sampleRate, localRegion, repairSettings());
    if (!changed) {
        return false;
    }

    if (!channel.SetFloats(buffer.data(), start, length, narrowestSampleFormat)) {
        return false;
    }

    return !TrackProgress(count, 1.0); // true only on cancellation
}
}

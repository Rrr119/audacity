/**********************************************************************

  Audacity: A Digital Audio Editor

  SpectralRepairEffect.h

  Reconstructs a box-selected time-frequency region from the surrounding
  spectro-temporal texture. Removes isolated keyboard clicks and mouth
  noises without dulling sibilance outside the selection.

**********************************************************************/
#pragma once

#include "au3-components/SettingsVisitor.h"
#include "au3-effects/StatefulEffect.h"

#include "spectralrepair.h"

class WaveChannel;

namespace au::effects {
class SpectralRepairEffect : public StatefulEffect
{
public:
    static inline SpectralRepairEffect*
    FetchParameters(SpectralRepairEffect& e, EffectSettings&)
    {
        return &e;
    }

    static const ComponentInterfaceSymbol Symbol;

    SpectralRepairEffect();
    ~SpectralRepairEffect() override;

    // ComponentInterface implementation

    ComponentInterfaceSymbol GetSymbol() const override;
    TranslatableString GetDescription() const override;

    // EffectDefinitionInterface implementation

    ::EffectType GetType() const override;
    EffectGroup GetGroup() const override { return EffectGroup::NoiseRemovalAndRepair; }

    // Effect implementation

    bool CheckWhetherSkipEffect(const EffectSettings& settings) const override;
    bool Process(::EffectInstance& instance, ::EffectSettings& settings) override;

    // User-facing parameters (mirrored into SpectralRepairSettings).
    int mFftSizeLog2 = 11; // 2^11 = 2048
    int mTextureFrames = 6;
    int mTextureBins = 4;
    float mStrength = 1.0f;
    float mGateThresholdDb = 6.0f;

    const EffectParameterMethods& Parameters() const override;

    static constexpr EffectParameter FftSizeLog2 {
        &SpectralRepairEffect::mFftSizeLog2, L"FftSizeLog2", 11, 9, 13, 1
    };
    static constexpr EffectParameter TextureFrames {
        &SpectralRepairEffect::mTextureFrames, L"TextureFrames", 6, 1, 16, 1
    };
    static constexpr EffectParameter TextureBins {
        &SpectralRepairEffect::mTextureBins, L"TextureBins", 4, 1, 32, 1
    };
    static constexpr EffectParameter Strength {
        &SpectralRepairEffect::mStrength, L"Strength", 1.0f, 0.0f, 1.0f, 0.05f
    };
    static constexpr EffectParameter GateThresholdDb {
        &SpectralRepairEffect::mGateThresholdDb, L"GateThresholdDb", 6.0f, 1.0f, 24.0f, 0.5f
    };

private:
    bool ProcessOne(
        int count, WaveChannel& channel, sampleCount start, sampleCount len,
        double sampleRate, const SpectralRepairRegion& region);

    SpectralRepairSettings repairSettings() const;
};
}

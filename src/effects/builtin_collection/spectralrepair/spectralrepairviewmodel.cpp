/**********************************************************************

  Audacity: A Digital Audio Editor

  SpectralRepairViewModel.cpp

**********************************************************************/
#include "spectralrepairviewmodel.h"
#include "spectralrepaireffect.h"

#include "global/translation.h"

namespace au::effects {

SpectralRepairViewModel::SpectralRepairViewModel(QObject* parent, int instanceId)
    : BuiltinEffectModel(parent, instanceId)
{
}

QString SpectralRepairViewModel::effectTitle() const
{
    return muse::qtrc("effects/spectralrepair", "Spectral repair");
}

int SpectralRepairViewModel::fftSizeValue() const
{
    return effect<SpectralRepairEffect>().mFftSizeLog2;
}

void SpectralRepairViewModel::setFftSizeValue(int value)
{
    auto& e = effect<SpectralRepairEffect>();
    if (e.mFftSizeLog2 != value) {
        e.mFftSizeLog2 = value;
        emit fftSizeValueChanged();
    }
}

int SpectralRepairViewModel::fftSizeMin() const
{
    return SpectralRepairEffect::FftSizeLog2.min;
}

int SpectralRepairViewModel::fftSizeMax() const
{
    return SpectralRepairEffect::FftSizeLog2.max;
}

int SpectralRepairViewModel::textureFramesValue() const
{
    return effect<SpectralRepairEffect>().mTextureFrames;
}

void SpectralRepairViewModel::setTextureFramesValue(int value)
{
    auto& e = effect<SpectralRepairEffect>();
    if (e.mTextureFrames != value) {
        e.mTextureFrames = value;
        emit textureFramesValueChanged();
    }
}

int SpectralRepairViewModel::textureBinsValue() const
{
    return effect<SpectralRepairEffect>().mTextureBins;
}

void SpectralRepairViewModel::setTextureBinsValue(int value)
{
    auto& e = effect<SpectralRepairEffect>();
    if (e.mTextureBins != value) {
        e.mTextureBins = value;
        emit textureBinsValueChanged();
    }
}

double SpectralRepairViewModel::strengthValue() const
{
    return effect<SpectralRepairEffect>().mStrength;
}

void SpectralRepairViewModel::setStrengthValue(double value)
{
    auto& e = effect<SpectralRepairEffect>();
    if (!muse::is_equal(e.mStrength, static_cast<float>(value))) {
        e.mStrength = static_cast<float>(value);
        emit strengthValueChanged();
    }
}

double SpectralRepairViewModel::gateThresholdValue() const
{
    return effect<SpectralRepairEffect>().mGateThresholdDb;
}

void SpectralRepairViewModel::setGateThresholdValue(double value)
{
    auto& e = effect<SpectralRepairEffect>();
    if (!muse::is_equal(e.mGateThresholdDb, static_cast<float>(value))) {
        e.mGateThresholdDb = static_cast<float>(value);
        emit gateThresholdValueChanged();
    }
}

void SpectralRepairViewModel::doReload()
{
    emit fftSizeValueChanged();
    emit textureFramesValueChanged();
    emit textureBinsValueChanged();
    emit strengthValueChanged();
    emit gateThresholdValueChanged();
}
}

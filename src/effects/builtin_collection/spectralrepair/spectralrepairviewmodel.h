/**********************************************************************

  Audacity: A Digital Audio Editor

  SpectralRepairViewModel.h

**********************************************************************/
#pragma once

#include "effects/builtin/qml/Audacity/BuiltinEffects/builtineffectmodel.h"

namespace au::effects {
class SpectralRepairEffect;

class SpectralRepairViewModel : public BuiltinEffectModel
{
    Q_OBJECT

    Q_PROPERTY(QString effectTitle READ effectTitle CONSTANT FINAL)

    Q_PROPERTY(int fftSizeValue READ fftSizeValue WRITE setFftSizeValue NOTIFY fftSizeValueChanged FINAL)
    Q_PROPERTY(int fftSizeMin READ fftSizeMin CONSTANT FINAL)
    Q_PROPERTY(int fftSizeMax READ fftSizeMax CONSTANT FINAL)

    Q_PROPERTY(int textureFramesValue READ textureFramesValue WRITE setTextureFramesValue
               NOTIFY textureFramesValueChanged FINAL)
    Q_PROPERTY(int textureBinsValue READ textureBinsValue WRITE setTextureBinsValue
               NOTIFY textureBinsValueChanged FINAL)

    Q_PROPERTY(double strengthValue READ strengthValue WRITE setStrengthValue NOTIFY strengthValueChanged FINAL)
    Q_PROPERTY(double gateThresholdValue READ gateThresholdValue WRITE setGateThresholdValue
               NOTIFY gateThresholdValueChanged FINAL)

public:
    SpectralRepairViewModel(QObject* parent, int instanceId);

    QString effectTitle() const;

    int fftSizeValue() const;
    void setFftSizeValue(int value);
    int fftSizeMin() const;
    int fftSizeMax() const;

    int textureFramesValue() const;
    void setTextureFramesValue(int value);
    int textureBinsValue() const;
    void setTextureBinsValue(int value);

    double strengthValue() const;
    void setStrengthValue(qreal value);
    double gateThresholdValue() const;
    void setGateThresholdValue(qreal value);

signals:
    void fftSizeValueChanged();
    void textureFramesValueChanged();
    void textureBinsValueChanged();
    void strengthValueChanged();
    void gateThresholdValueChanged();

private:
    void doReload() override;
};

class SpectralRepairViewModelFactory : public EffectViewModelFactory<SpectralRepairViewModel>
{
};
}

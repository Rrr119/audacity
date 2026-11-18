import QtQuick
import Muse.UiComponents
import Audacity.Effects
import Audacity.BuiltinEffects
import Audacity.BuiltinEffectsCollection

BuiltinEffectBase {
    id: root

    property string title: spectralRepair.effectTitle
    property bool isApplyAllowed: true

    width: 328
    implicitHeight: column.height

    builtinEffectModel: SpectralRepairViewModelFactory.createModel(root, root.instanceId)
    numNavigationPanels: 1
    property alias spectralRepair: root.builtinEffectModel
    property NavigationPanel spectralRepairNavigationPanel: NavigationPanel {
        name: "SpectralRepairSliders"
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Horizontal
        section: root.dialogView ? root.dialogView.navigationSection : null
        order: 1
    }

    Column {
        id: column

        height: implicitHeight
        width: parent.width
        spacing: 16

        SliderWithTextInput {
            id: fftSizeSlider

            width: parent.width

            navigationPanel: root.spectralRepairNavigationPanel
            navigationOrderStart: 0

            text: qsTr("FFT size (power of two, 2^n)")
            value: spectralRepair.fftSizeValue
            from: spectralRepair.fftSizeMin
            to: spectralRepair.fftSizeMax
            step: 1
            decimals: 0

            onNewValueRequested: function (newValue) {
                spectralRepair.fftSizeValue = newValue
            }
        }

        SliderWithTextInput {
            id: textureFramesSlider

            width: parent.width

            navigationPanel: root.spectralRepairNavigationPanel
            navigationOrderStart: fftSizeSlider.navigationOrderStart + 2

            text: qsTr("Texture frames (each side)")
            value: spectralRepair.textureFramesValue
            from: 1
            to: 16
            step: 1
            decimals: 0

            onNewValueRequested: function (newValue) {
                spectralRepair.textureFramesValue = newValue
            }
        }

        SliderWithTextInput {
            id: textureBinsSlider

            width: parent.width

            navigationPanel: root.spectralRepairNavigationPanel
            navigationOrderStart: textureFramesSlider.navigationOrderStart + 2

            text: qsTr("Texture frequency bands (each side)")
            value: spectralRepair.textureBinsValue
            from: 1
            to: 32
            step: 1
            decimals: 0

            onNewValueRequested: function (newValue) {
                spectralRepair.textureBinsValue = newValue
            }
        }

        SliderWithTextInput {
            id: strengthSlider

            width: parent.width

            navigationPanel: root.spectralRepairNavigationPanel
            navigationOrderStart: textureBinsSlider.navigationOrderStart + 2

            text: qsTr("Strength")
            value: spectralRepair.strengthValue
            from: 0.0
            to: 1.0
            step: 0.05
            decimals: 2

            onNewValueRequested: function (newValue) {
                spectralRepair.strengthValue = newValue
            }
        }

        SliderWithTextInput {
            id: gateThresholdSlider

            width: parent.width

            navigationPanel: root.spectralRepairNavigationPanel
            navigationOrderStart: strengthSlider.navigationOrderStart + 2

            text: qsTr("Sensitivity above texture (dB)")
            value: spectralRepair.gateThresholdValue
            from: 1.0
            to: 24.0
            step: 0.5
            decimals: 1

            onNewValueRequested: function (newValue) {
                spectralRepair.gateThresholdValue = newValue
            }
        }
    }
}

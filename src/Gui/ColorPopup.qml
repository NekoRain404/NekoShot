/* SPDX-FileCopyrightText: 2026 NekoRain
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import org.nekoshot.NekoShot.private
import org.kde.kquickimageeditor

/**
 * In-surface color picker. The capture overlay is a layer-shell surface on the
 * Overlay layer, so a Qt Widgets QColorDialog would be mapped underneath it and
 * look frozen. This popup lives inside the capture/viewer window instead.
 */
QQC.Popup {
    id: root

    property int option: -1
    property color selectedColor: "#0A84FF"
    property real hsvHue: 0.58
    property real hsvSaturation: 1.0
    property real hsvValue: 1.0

    signal colorSelected(color color)

    parent: QQC.Overlay.overlay
    modal: true
    focus: true
    padding: Theme.space16
    width: 296
    closePolicy: QQC.Popup.CloseOnEscape | QQC.Popup.CloseOnPressOutside
    x: Math.round(((parent ? parent.width : 0) - width) / 2)
    y: Math.max(Theme.space16, Math.round((parent ? parent.height : 0) - height - 96))

    readonly property var palette: [
        "#FF3B30", "#FF9500", "#FFCC00", "#34C759", "#00C7BE", "#30B0C7",
        "#32ADE6", "#007AFF", "#5856D6", "#AF52DE", "#FF2D55", "#A2845E",
        "#FFFFFF", "#F2F2F7", "#C7C7CC", "#8E8E93", "#636366", "#3A3A3C",
        "#1C1C1E", "#000000", "#FFD60A", "#64D2FF", "#BF5AF2", "#FF9F0A"
    ]

    function titleForOption(value) {
        switch (value) {
        case AnnotationTool.StrokeOption: return i18n("Stroke Color")
        case AnnotationTool.FillOption: return i18n("Fill Color")
        case AnnotationTool.FontOption: return i18n("Text Color")
        default: return i18n("Color")
        }
    }

    function openFor(value, color) {
        root.option = value
        root.selectedColor = color
        syncHsv(color)
        hexField.text = colorToHex(color)
        alphaSlider.value = Math.round(color.a * 255)
        open()
    }

    function syncHsv(color) {
        var h = color.hsvHue
        root.hsvHue = h >= 0 ? h : root.hsvHue
        root.hsvSaturation = color.hsvSaturation
        root.hsvValue = color.hsvValue
    }

    function hsvColor() {
        return Qt.hsva(root.hsvHue, root.hsvSaturation, root.hsvValue, alphaSlider.value / 255)
    }

    function updateFromHsv() {
        var color = hsvColor()
        root.selectedColor = color
        hexField.text = colorToHex(color)
    }

    function colorToHex(color) {
        return "#" + ("0" + Math.round(color.r * 255).toString(16)).slice(-2)
                   + ("0" + Math.round(color.g * 255).toString(16)).slice(-2)
                   + ("0" + Math.round(color.b * 255).toString(16)).slice(-2)
    }

    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    function commitColor(color) {
        root.selectedColor = color
        root.colorSelected(root.selectedColor)
        close()
    }

    background: FloatingBackground {
        color: Theme.panelSolid
        border.color: Theme.panelBorder
        radius: Theme.radiusPanel
    }

    contentItem: ColumnLayout {
        spacing: Theme.space12

        QQC.Label {
            text: root.titleForOption(root.option)
            color: Theme.textPrimary
            font.pixelSize: Theme.font13
            font.weight: Theme.weightSemibold
        }

        // Saturation/value field with a hue strip below it. The presets stay
        // for quick access, but any color can now be mixed without a Widgets
        // QColorDialog.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 144

            Rectangle {
                id: svField
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 112
                radius: Theme.radiusButton
                clip: true
                color: Qt.hsva(root.hsvHue, 1, 1, 1)

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#FFFFFFFF" }
                        GradientStop { position: 1.0; color: "#00FFFFFF" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: "#00000000" }
                        GradientStop { position: 1.0; color: "#FF000000" }
                    }
                }
                Rectangle {
                    width: 14
                    height: 14
                    radius: 7
                    x: root.hsvSaturation * (svField.width - width)
                    y: (1 - root.hsvValue) * (svField.height - height)
                    color: "transparent"
                    border.width: 2
                    border.color: "white"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.CrossCursor
                    function pick(mouse) {
                        root.hsvSaturation = Math.max(0, Math.min(1, mouse.x / width))
                        root.hsvValue = 1 - Math.max(0, Math.min(1, mouse.y / height))
                        root.updateFromHsv()
                    }
                    onPressed: pick(mouse)
                    onPositionChanged: if (pressed) pick(mouse)
                }
            }

            Rectangle {
                id: hueStrip
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: svField.bottom
                anchors.topMargin: Theme.space8
                height: 14
                radius: 7
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#FF3B30" }
                    GradientStop { position: 0.17; color: "#FFCC00" }
                    GradientStop { position: 0.33; color: "#34C759" }
                    GradientStop { position: 0.5; color: "#00C7BE" }
                    GradientStop { position: 0.67; color: "#007AFF" }
                    GradientStop { position: 0.83; color: "#AF52DE" }
                    GradientStop { position: 1.0; color: "#FF3B30" }
                }
                Rectangle {
                    width: 18
                    height: 18
                    radius: 9
                    x: Math.max(0, Math.min(hueStrip.width - width, root.hsvHue * hueStrip.width - width / 2))
                    y: (hueStrip.height - height) / 2
                    color: "transparent"
                    border.width: 2
                    border.color: "white"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    function pick(mouse) {
                        root.hsvHue = Math.max(0, Math.min(1, mouse.x / width))
                        root.updateFromHsv()
                    }
                    onPressed: pick(mouse)
                    onPositionChanged: if (pressed) pick(mouse)
                }
            }
        }

        GridLayout {
            columns: 6
            columnSpacing: Theme.space8
            rowSpacing: Theme.space8
            Repeater {
                model: root.palette
                delegate: Rectangle {
                    required property string modelData
                    width: 32
                    height: 32
                    radius: 16
                    color: modelData
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.18)
                    scale: swatchMouse.containsMouse ? 1.08 : 1.0
                    Behavior on scale {
                        NumberAnimation { duration: Theme.durationQuick; easing.type: Theme.easingCurve }
                    }
                    MouseArea {
                        id: swatchMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.syncHsv(Qt.color(modelData))
                            root.commitColor(root.withAlpha(Qt.color(modelData), alphaSlider.value / 255))
                        }
                    }
                }
            }
        }

        RowLayout {
            spacing: Theme.space8
            QQC.Label {
                text: i18n("Alpha")
                color: Theme.textSecondary
                font.pixelSize: Theme.font12
            }
            QQC.Slider {
                id: alphaSlider
                Layout.fillWidth: true
                from: 0
                to: 255
                stepSize: 1
            }
            QQC.Label {
                text: Math.round(alphaSlider.value)
                color: Theme.textSecondary
                font.pixelSize: Theme.font12
                Layout.preferredWidth: 28
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            spacing: Theme.space8
            QQC.Label {
                text: i18n("Hex")
                color: Theme.textSecondary
                font.pixelSize: Theme.font12
            }
            QQC.TextField {
                id: hexField
                Layout.fillWidth: true
                placeholderText: "#RRGGBB"
                color: Theme.textPrimary
                font.pixelSize: Theme.font12
                background: Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.06)
                    radius: Theme.radiusButton
                    border.color: Theme.panelBorder
                }
                validator: RegularExpressionValidator { regularExpression: /^#[0-9A-Fa-f]{6}$/ }
                onAccepted: {
                    if (acceptableInput) {
                        root.commitColor(root.withAlpha(Qt.color(text), alphaSlider.value / 255))
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.space8
            QQC.Button {
                text: i18n("Cancel")
                flat: true
                onClicked: root.close()
            }
            QQC.Button {
                text: i18n("Apply")
                highlighted: true
                onClicked: root.commitColor(root.hsvColor())
            }
        }
    }
}

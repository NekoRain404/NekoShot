/* SPDX-FileCopyrightText: 2026 NekoRain
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import org.nekoshot.NekoShot.private

/**
 * In-surface font picker for annotation text. Like ColorPopup, it avoids a
 * Qt Widgets dialog being mapped underneath the layer-shell capture overlay.
 */
QQC.Popup {
    id: root

    property string family
    property int pointSize: 12
    property bool bold: false
    property bool italic: false

    signal fontSelected(string family, int pointSize, bool bold, bool italic)

    parent: QQC.Overlay.overlay
    modal: true
    focus: true
    padding: Theme.space16
    width: 340
    closePolicy: QQC.Popup.CloseOnEscape | QQC.Popup.CloseOnPressOutside
    x: Math.round(((parent ? parent.width : 0) - width) / 2)
    y: Math.max(Theme.space16, Math.round((parent ? parent.height : 0) - height - 96))

    function openFor(font) {
        root.family = font.family
        root.pointSize = font.pointSize > 0 ? font.pointSize : 12
        root.bold = font.bold
        root.italic = font.italic
        familyBox.currentIndex = Math.max(0, familyBox.model.indexOf(root.family))
        sizeBox.value = root.pointSize
        boldBox.checked = root.bold
        italicBox.checked = root.italic
        open()
    }

    background: FloatingBackground {
        color: Theme.panelSolid
        border.color: Theme.panelBorder
        radius: Theme.radiusPanel
    }

    contentItem: ColumnLayout {
        spacing: Theme.space12

        QQC.Label {
            text: i18n("Font")
            color: Theme.textPrimary
            font.pixelSize: Theme.font13
            font.weight: Theme.weightSemibold
        }

        QQC.ComboBox {
            id: familyBox
            model: Qt.fontFamilies()
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Theme.space8
            QQC.Label {
                text: i18n("Size")
                color: Theme.textSecondary
                font.pixelSize: Theme.font12
            }
            QQC.SpinBox {
                id: sizeBox
                from: 6
                to: 96
                value: 12
                editable: true
            }
            QQC.CheckBox {
                id: boldBox
                text: i18n("Bold")
            }
            QQC.CheckBox {
                id: italicBox
                text: i18n("Italic")
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
                onClicked: {
                    root.fontSelected(familyBox.currentText, sizeBox.value, boldBox.checked, italicBox.checked)
                    close()
                }
            }
        }
    }
}

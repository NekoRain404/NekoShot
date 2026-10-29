/* SPDX-FileCopyrightText: 2025 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami
import org.nekoshot.NekoShot.private

QQC.ToolButton {
    id: root

    property bool showDefaultToolTip: true
    implicitHeight: Theme.controlHeight
    width: display === QQC.ToolButton.IconOnly ? height : implicitWidth
    QQC.ToolTip.text: showDefaultToolTip ? text : ""
    QQC.ToolTip.visible: showDefaultToolTip && (hovered || pressed) && display === QQC.ToolButton.IconOnly
    QQC.ToolTip.delay: Kirigami.Units.toolTipDelay

    palette.windowText: Theme.textPrimary
    palette.buttonText: Theme.textPrimary
    palette.text: Theme.textPrimary
    palette.window: Theme.panelSolid
    palette.base: Theme.panelSolid
    palette.highlight: Theme.accent
    palette.highlightedText: "#FFFFFF"

    icon.color: !enabled ? Theme.textTertiary
        : checked ? Theme.accent
        : Theme.textPrimary
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusButton
            color: !root.enabled ? "transparent"
                : root.pressed || root.down ? (root.checked ? Theme.accentFillPressed : Theme.pressed)
                : root.checked ? Theme.accentFill
                : root.hovered ? Theme.hover
                : "transparent"
            Behavior on color {
                ColorAnimation {
                    duration: Theme.duration
                    easing.type: Theme.easingCurve
                }
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusButton
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: root.visualFocus && !root.pressed
        }
    }
}

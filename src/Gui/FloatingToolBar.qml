/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.nekoshot.NekoShot.private

T.Pane {
    id: root
    property real radius: Theme.radiusPanel
    property real topLeftRadius: radius
    property real topRightRadius: radius
    property real bottomLeftRadius: radius
    property real bottomRightRadius: radius
    property real backgroundColorOpacity: 0.86

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: Theme.space8
    spacing: Theme.space4

    // Dark chrome keeps a predictable contrast ratio over any screenshot,
    // including when the scrim itself is light.
    palette.window: Theme.panelSolid
    palette.windowText: Theme.textPrimary
    palette.base: Theme.panelSolid
    palette.alternateBase: Theme.panelSolid
    palette.text: Theme.textPrimary
    palette.button: Theme.panelSolid
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: "#FFFFFF"
    palette.mid: Theme.separator
    palette.midlight: Theme.separator
    palette.dark: Theme.panelSolid
    palette.shadow: "#000000"
    palette.brightText: Theme.danger

    background: FloatingBackground {
        color: Qt.rgba(28 / 255, 28 / 255, 30 / 255, root.backgroundColorOpacity)
        border.color: Theme.panelBorder
        radius: root.radius
        corners.topLeftRadius: root.topLeftRadius
        corners.topRightRadius: root.topRightRadius
        corners.bottomLeftRadius: root.bottomLeftRadius
        corners.bottomRightRadius: root.bottomRightRadius
        border.width: contextWindow.dprRound(1)
    }
}

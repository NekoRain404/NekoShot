/* SPDX-FileCopyrightText: 2026 NekoRain
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

pragma Singleton

import QtQuick

/**
 * Single source of truth for the NekoShot capture UI.
 *
 * Dark-first: overlay chrome is always dark, regardless of the system color
 * scheme, so that it keeps a predictable contrast over arbitrary screenshots.
 * The material is a translucent tint plus a 1px border and a soft shadow. A
 * solid fallback is provided for surfaces that need an opaque backdrop.
 */
QtObject {
    id: root

    // Material
    readonly property color panel: Qt.rgba(28 / 255, 28 / 255, 30 / 255, 0.86)
    readonly property color panelSolid: "#1C1C1E"
    readonly property color panelFallback: Qt.rgba(28 / 255, 28 / 255, 30 / 255, 0.98)
    readonly property color panelBorder: Qt.rgba(1, 1, 1, 0.12)
    readonly property color separator: Qt.rgba(1, 1, 1, 0.08)

    // Text
    readonly property color textPrimary: Qt.rgba(1, 1, 1, 0.95)
    readonly property color textSecondary: Qt.rgba(1, 1, 1, 0.60)
    readonly property color textTertiary: Qt.rgba(1, 1, 1, 0.38)

    // Accent and state
    readonly property color accent: "#0A84FF"
    readonly property color accentFill: Qt.rgba(10 / 255, 132 / 255, 255 / 255, 0.18)
    readonly property color accentFillPressed: Qt.rgba(10 / 255, 132 / 255, 255 / 255, 0.28)
    readonly property color danger: "#FF453A"
    readonly property color hover: Qt.rgba(1, 1, 1, 0.08)
    readonly property color pressed: Qt.rgba(1, 1, 1, 0.14)

    // Selection
    readonly property color selection: "#FFFFFF"
    readonly property real selectionLineWidth: 1.5
    readonly property real selectionCornerRadius: 2
    readonly property color handleFill: "#FFFFFF"
    readonly property color handleBorder: Qt.rgba(0, 0, 0, 0.20)
    readonly property real handleDiameter: 10
    readonly property real handleHitSize: 18
    readonly property color shadowColor: Qt.rgba(0, 0, 0, 0.35)

    // Spacing (4/8/12/16/24)
    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space24: 24

    // Radius
    readonly property int radiusButton: 8
    readonly property int radiusPanel: 16
    readonly property int radiusPill: 999

    // Elevation
    readonly property int shadowY: 8
    readonly property int shadowBlur: 24
    readonly property real shadowAlpha: 0.35

    // Type
    readonly property string fontFamily: Qt.application.font.family
    readonly property int font11: 11
    readonly property int font12: 12
    readonly property int font13: 13
    readonly property int font15: 15
    readonly property int font17: 17
    readonly property int weightMedium: 500
    readonly property int weightSemibold: 600

    // Controls
    readonly property int controlHeight: 32
    readonly property int iconSize: 16

    // Motion
    readonly property int duration: 160
    readonly property int durationQuick: 120
    readonly property int easingCurve: Easing.OutCubic
}

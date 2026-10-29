/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.nekoshot.NekoShot.private

Shape {
    id: root
    property alias strokeWidth: shapePath.strokeWidth
    // The stroke color beneath the dash
    property alias strokeColor: shapePath.strokeColor
    property alias strokeStyle: shapePath.strokeStyle
    property alias capStyle: shapePath.capStyle
    property alias joinStyle: shapePath.joinStyle
    property alias svgPath: pathSvg.path
    property alias pathScale: shapePath.scale
    property alias pathHints: shapePath.pathHints
    property real cornerRadius: Theme.selectionCornerRadius

    // Get a rounded rectangular SVG path
    function rectanglePath(x, y, w, h) {
        const r = Math.max(0, Math.min(root.cornerRadius, Math.min(w, h) / 2))
        if (r === 0) {
            return `M ${x},${y}
                    l ${w},0
                    l 0,${h}
                    l ${-w},0
                    z`
        }
        return `M ${x + r},${y}
                L ${x + w - r},${y}
                Q ${x + w},${y} ${x + w},${y + r}
                L ${x + w},${y + h - r}
                Q ${x + w},${y + h} ${x + w - r},${y + h}
                L ${x + r},${y + h}
                Q ${x},${y + h} ${x},${y + h - r}
                L ${x},${y + r}
                Q ${x},${y} ${x + r},${y}
                z`
    }

    // Get a matrix4x4 that moves the stroke outside the bounds of the path
    function outerStrokeScaleValue(originalValue, strokeWidth = root.strokeWidth) {
        return QmlUtils.ratio(originalValue + strokeWidth * 2, originalValue + strokeWidth)
    }

    // Get a matrix4x4 that moves the stroke outside the bounds of the path
    function outerStrokeTranslateValue(originalValue, scale, strokeWidth = root.strokeWidth) {
        return QmlUtils.unTranslateScale(originalValue, scale) - strokeWidth / 2
    }

    preferredRendererType: Shape.CurveRenderer

    // Subtle shadow underneath the selection line.
    ShapePath {
        id: shadowPath
        fillColor: "transparent"
        strokeWidth: root.strokeWidth + contextWindow.dprRound(2)
        strokeColor: Qt.rgba(0, 0, 0, 0.22)
        strokeStyle: ShapePath.SolidLine
        joinStyle: root.joinStyle
        capStyle: root.capStyle
        PathSvg {
            path: pathSvg.path
        }
    }

    ShapePath {
        id: shapePath
        fillColor: "transparent"
        strokeWidth: Theme.selectionLineWidth
        strokeColor: Theme.selection
        // Solid line because it's easier to do the alternating color effect this way.
        strokeStyle: ShapePath.SolidLine
        joinStyle: ShapePath.MiterJoin
        PathSvg {
            id: pathSvg
            path: rectanglePath(strokeWidth / 2, strokeWidth / 2,
                                width - strokeWidth, height - strokeWidth)
        }
    }
}

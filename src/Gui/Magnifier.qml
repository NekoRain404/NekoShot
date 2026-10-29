/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import org.nekoshot.NekoShot.private
import org.kde.kquickimageeditor

Item {
    id: root
    required property AnnotationViewport viewport
    required property point targetPoint
    property int factor: 3

    readonly property rect sourceRect: Qt.rect((targetPoint.x - viewport.viewportRect.x) - 33,
                                               (targetPoint.y - viewport.viewportRect.y) - 33,
                                               67, 67)
    implicitWidth: sourceRect.width * factor
    implicitHeight: sourceRect.height * factor

    FloatingBackground {
        z: -1
        anchors.fill: parent
        radius: Theme.radiusPanel
        color: Theme.panel
        border.color: Theme.panelBorder
    }

    ShaderEffectSource {
        id: source
        anchors.fill: parent
        sourceItem: root.viewport
        sourceRect: root.sourceRect
        smooth: false
        visible: false
    }

    Item {
        id: mask
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusPanel
            color: "white"
        }
    }

    MultiEffect {
        anchors.fill: parent
        source: source
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 0.0
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPanel
        color: "transparent"
        border.width: contextWindow.dprRound(1)
        border.color: Theme.panelBorder
    }

    Item {
        id: center
        x: contextWindow.dprRound((parent.implicitWidth - width) / 2)
        y: contextWindow.dprRound((parent.implicitHeight - height) / 2)
        width: root.factor * 3
        height: root.factor * 3
    }

    Rectangle { // top
        anchors.top: parent.top
        anchors.bottom: center.top
        color: Theme.selection
        x: contextWindow.dprRound((parent.implicitWidth - width) / 2)
        width: contextWindow.dprRound(1)
    }
    Rectangle { // bottom
        anchors.bottom: parent.bottom
        anchors.top: center.bottom
        color: Theme.selection
        x: contextWindow.dprRound((parent.implicitWidth - width) / 2)
        width: contextWindow.dprRound(1)
    }
    Rectangle { // left
        anchors.left: parent.left
        anchors.right: center.left
        color: Theme.selection
        y: contextWindow.dprRound((parent.implicitHeight - height) / 2)
        height: contextWindow.dprRound(1)
    }
    Rectangle { // right
        anchors.right: parent.right
        anchors.left: center.right
        color: Theme.selection
        y: contextWindow.dprRound((parent.implicitHeight - height) / 2)
        height: contextWindow.dprRound(1)
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space4
        implicitWidth: zoomLabel.implicitWidth + Theme.space8
        implicitHeight: zoomLabel.implicitHeight + Theme.space4
        radius: Theme.radiusButton
        color: Theme.panelFallback
        border.width: contextWindow.dprRound(1)
        border.color: Theme.panelBorder

        Text {
            id: zoomLabel
            anchors.centerIn: parent
            text: root.factor + "×"
            color: Theme.textPrimary
            font.pixelSize: Theme.font11
            font.weight: Theme.weightMedium
            font.features: ({ "tnum": 1 })
        }
    }
}

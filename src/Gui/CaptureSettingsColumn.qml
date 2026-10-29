/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami
import org.nekoshot.NekoShot.private

ColumnLayout {
    Layout.minimumWidth: delayRow.implicitWidth
    spacing: Theme.space8
    QQC.CheckBox {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeight
        text: i18n("Include mouse pointer")
        enabled: ImagePlatform.supportsScreenshotPointer || SpectacleCore.videoMode
        QQC.ToolTip.text: ImagePlatform.supportsScreenshotPointer || SpectacleCore.videoMode
            ? i18n("Show the mouse cursor in the screenshot image.")
            : i18n("niri does not composite the cursor into screenshots.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        checked: Settings.includePointer
        onToggled: Settings.includePointer = checked
    }
    QQC.CheckBox {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeight
        text: i18n("Include window titlebar and borders")
        QQC.ToolTip.text: i18n("Show the window title bar and border when taking a screenshot of a window.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        checked: Settings.includeDecorations
        onToggled: Settings.includeDecorations = checked
    }
    QQC.CheckBox {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeight
        text: i18n("Include window shadow")
        QQC.ToolTip.text: i18n("Show the window shadow when taking a screenshot of a window.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        enabled: Settings.includeDecorations
        checked: Settings.includeShadow
        onToggled: Settings.includeShadow = checked
    }
    QQC.CheckBox {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeight
        text: i18n("Quit after manual Save or Copy")
        QQC.ToolTip.text: i18n("Quit Spectacle after manually saving or copying the image.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        checked: Settings.quitAfterSaveCopyExport
        onToggled: Settings.quitAfterSaveCopyExport = checked
    }
    QQC.CheckBox {
        id: captureOnClickCheckBox
        Layout.fillWidth: true
        text: i18n("Capture on click")
        visible: ImagePlatform.supportedShutterModes === (ImagePlatform.Immediate | ImagePlatform.OnClick)
        QQC.ToolTip.text: i18n("Wait for a mouse click before capturing the screenshot image.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered || pressed
        checked: ImagePlatform.supportedShutterModes & ImagePlatform.OnClick && Settings.captureOnClick
        onToggled: Settings.captureOnClick = checked
    }
    RowLayout {
        id: delayRow
        spacing: parent.spacing
        Layout.preferredHeight: Theme.controlHeight
        QQC.Label {
            text: i18n("Delay:")
        }
        DelaySpinBox {
            enabled: !captureOnClickCheckBox.checked
        }
    }
}

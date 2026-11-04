/* SPDX-FileCopyrightText: 2023 Aleix Pol Gonzalez <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami
import org.nekoshot.NekoShot.private

ColumnLayout {
    id: root
    spacing: Kirigami.Units.mediumSpacing

    // GIF and WebP can't carry an audio track, so the audio options are
    // disabled for those formats.
    readonly property bool audioSupported: SpectacleCore.videoPlatform.formatSupportsAudio(Settings.preferredVideoFormat)

    // Devices are read once when the recording settings are created; changes
    // made while the window is open need a reopen, like the rest of the list.
    readonly property var audioSources: SpectacleCore.availableAudioSources()

    // Keeps a saved device visible even when it is currently unplugged, so the
    // combo does not silently show "Default" while the setting still points at
    // the missing device.
    function sourcesWithDefault(defaultLabel, monitors, savedName) {
        const list = [{
            name: "",
            description: defaultLabel
        }].concat(root.audioSources.filter(source => source.isMonitor === monitors))
        if (savedName !== "" && list.findIndex(source => source.name === savedName) === -1) {
            list.push({
                name: savedName,
                description: i18nc("@item:inlistbox audio device that is currently unavailable", "%1 (unavailable)", savedName)
            })
        }
        return list
    }

    readonly property var monitorSources: root.sourcesWithDefault(i18nc("@item:inlistbox default system audio source", "Default output"),
                                                                  true,
                                                                  Settings.videoSystemAudioSource)
    readonly property var microphoneSources: root.sourcesWithDefault(i18nc("@item:inlistbox default microphone", "Default microphone"),
                                                                     false,
                                                                     Settings.videoMicrophoneSource)
    readonly property var applications: {
        const list = [{
            application: "",
            title: i18nc("@item:inlistbox record the whole output instead of one application", "All applications")
        }].concat(SpectacleCore.availableAudioApplications())
        const saved = Settings.videoAppAudio
        if (saved !== "" && list.findIndex(entry => entry.application === saved) === -1) {
            list.push({
                application: saved,
                title: i18nc("@item:inlistbox application is not currently playing audio", "%1 (not playing)", saved)
            })
        }
        return list
    }

    QQC.CheckBox {
        id: systemAudioCheck
        Layout.fillWidth: true
        text: i18nc("@option:check", "Record system audio")
        QQC.ToolTip.text: i18nc("@info:tooltip", "Include the audio that is currently playing on the system in the recording.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        // Explain why this is disabled, since a disabled control doesn't get
        // hover events and can't show its tooltip.
        Accessible.description: root.audioSupported ? "" : audioUnsupportedMessage.text
        checked: Settings.videoRecordSystemAudio
        onToggled: Settings.videoRecordSystemAudio = checked
    }
    QQC.ComboBox {
        id: systemSourceCombo
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.gridUnit
        visible: root.audioSupported
        enabled: systemAudioCheck.checked
        textRole: "description"
        valueRole: "name"
        model: root.monitorSources
        QQC.ToolTip.text: i18nc("@info:tooltip", "Output monitor that provides the system audio.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        Component.onCompleted: {
            const saved = Settings.videoSystemAudioSource
            currentIndex = Math.max(0, root.monitorSources.findIndex(source => source.name === saved))
        }
        onActivated: Settings.videoSystemAudioSource = currentValue
    }
    QQC.ComboBox {
        id: applicationCombo
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.gridUnit
        visible: root.audioSupported
        enabled: systemAudioCheck.checked && root.applications.length > 1
        textRole: "title"
        valueRole: "application"
        model: root.applications
        QQC.ToolTip.text: i18nc("@info:tooltip",
            "Record only this application's audio. It has to be playing when the recording starts.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        Component.onCompleted: {
            const saved = Settings.videoAppAudio
            currentIndex = Math.max(0, root.applications.findIndex(application => application.application === saved))
        }
        onActivated: {
            Settings.videoAppAudio = currentValue
            if (currentValue !== "") {
                Settings.videoRecordSystemAudio = true
            }
        }
    }
    QQC.CheckBox {
        id: microphoneCheck
        Layout.fillWidth: true
        enabled: root.audioSupported
        text: i18nc("@option:check", "Record microphone")
        QQC.ToolTip.text: i18nc("@info:tooltip", "Include audio from the selected microphone in the recording.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        Accessible.description: root.audioSupported ? "" : audioUnsupportedMessage.text
        checked: Settings.videoRecordMicrophone
        onToggled: Settings.videoRecordMicrophone = checked
    }
    QQC.ComboBox {
        id: microphoneSourceCombo
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.gridUnit
        visible: root.audioSupported
        enabled: microphoneCheck.checked
        textRole: "description"
        valueRole: "name"
        model: root.microphoneSources
        QQC.ToolTip.text: i18nc("@info:tooltip", "Input source that provides the microphone audio.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        Component.onCompleted: {
            const saved = Settings.videoMicrophoneSource
            currentIndex = Math.max(0, root.microphoneSources.findIndex(source => source.name === saved))
        }
        onActivated: Settings.videoMicrophoneSource = currentValue
    }
    Kirigami.InlineMessage {
        id: audioUnsupportedMessage
        Layout.fillWidth: true
        type: Kirigami.MessageType.Information
        text: i18nc("@info", "The selected video format does not support audio.")
        visible: !root.audioSupported
    }
}

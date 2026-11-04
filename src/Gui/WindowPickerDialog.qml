/*
 * SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami
import org.nekoshot.NekoShot.private

/**
 * Lists every open window so a recording can target a window that is not
 * visible (another workspace, covered by the overlay or hidden behind other
 * windows). niri's interactive picker is still available from the footer.
 *
 * The list is read once when the dialog opens; windows are not live-updated.
 */
QQC.Dialog {
    id: root

    /** id of the window the user chose. */
    signal windowChosen(var windowId)
    /** The user asked for the click-to-pick workflow instead. */
    signal pickWithMouse()

    property var windows: []

    title: i18nc("@title:window", "Choose a Window to Record")
    modal: true
    focus: true
    closePolicy: QQC.Popup.CloseOnEscape | QQC.Popup.CloseOnPressOutside
    width: Math.min(Kirigami.Units.gridUnit * 36, (parent ? parent.width : Kirigami.Units.gridUnit * 36) * 0.9)
    height: Kirigami.Units.gridUnit * 26
    anchors.centerIn: parent

    onOpened: {
        searchField.text = ""
        root.windows = SpectacleCore.availableWindows()
        searchField.forceActiveFocus()
    }

    function matches(entry) {
        const needle = searchField.text.trim().toLowerCase()
        if (needle.length === 0) {
            return true
        }
        const title = (entry.title || "").toLowerCase()
        const appId = (entry.appId || "").toLowerCase()
        return title.indexOf(needle) !== -1 || appId.indexOf(needle) !== -1
    }

    header: QQC.Label {
        text: root.title
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        leftPadding: Kirigami.Units.mediumSpacing
        rightPadding: Kirigami.Units.mediumSpacing
        topPadding: Kirigami.Units.smallSpacing
        bottomPadding: Kirigami.Units.smallSpacing
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC.TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: i18nc("@info:placeholder", "Search by title or application…")
            // Enter records the first entry that matches the current filter.
            onAccepted: {
                if (listView.count > 0) {
                    root.windowChosen(listView.model[0].id)
                    root.close()
                }
            }
        }

        QQC.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: listView
                clip: true
                model: root.windows.filter(root.matches)
                delegate: QQC.ItemDelegate {
                    width: ListView.view.width
                    text: modelData.appId.length > 0 ? i18nc("@item:inlistbox window title and app id", "%1 — %2", modelData.title, modelData.appId) : modelData.title
                    onClicked: {
                        root.windowChosen(modelData.id)
                        root.close()
                    }
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.windows.length === 0
            type: Kirigami.MessageType.Information
            text: i18nc("@info", "No windows are open.")
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.windows.length > 0 && listView.count === 0
            type: Kirigami.MessageType.Information
            text: i18nc("@info", "No window matches the search.")
        }
    }

    footer: QQC.DialogButtonBox {
        QQC.Button {
            text: i18nc("@action:button", "Pick with the Mouse Instead")
            QQC.DialogButtonBox.buttonRole: QQC.DialogButtonBox.ActionRole
            onClicked: {
                root.close()
                root.pickWithMouse()
            }
        }
        QQC.Button {
            text: i18nc("@action:button", "Cancel")
            QQC.DialogButtonBox.buttonRole: QQC.DialogButtonBox.RejectRole
            onClicked: root.close()
        }
    }
}

/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QRectF>
#include <QString>

#include <optional>

namespace NiriIpc
{
struct Output {
    QString name;
    QRectF logicalRect;
    // Physical pixels of the current mode, with 90/270 transforms applied.
    // Empty when niri did not report a current mode.
    QSize physicalSize;
    qreal scale = 1.0;
    QString transform;
};

struct Window {
    quint64 id = 0;
    QString title;
    QString appId;
    quint64 workspaceId = 0;
};

struct Workspace {
    quint64 id = 0;
    QString output;
    bool isActive = false;
    bool isFocused = false;
};

// True when a niri session is detected via NIRI_SOCKET or XDG_CURRENT_DESKTOP.
bool isAvailable();

QList<Output> parseOutputs(const QByteArray &json, QString *error = nullptr);
std::optional<Output> parseFocusedOutput(const QByteArray &json, QString *error = nullptr);
// A literal "null" document (cancelled pick) yields nullopt without an error.
std::optional<Window> parseWindow(const QByteArray &json, QString *error = nullptr);

QList<Workspace> parseWorkspaces(const QByteArray &json, QString *error = nullptr);
// The output name a workspace is assigned to, or an empty string.
QString outputNameForWorkspace(const QList<Workspace> &workspaces, quint64 workspaceId);

QList<Output> outputs(QString *error = nullptr);
std::optional<Output> focusedOutput(QString *error = nullptr);
// The output hosting the window's workspace. Returns nullopt when niri does
// not report the workspace/output; callers can fall back to the focused output.
std::optional<Output> outputForWindow(const Window &window, QString *error = nullptr);
std::optional<Window> focusedWindow(QString *error = nullptr);
std::optional<Window> pickWindow(QString *error = nullptr);
}

/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "NiriIpc.h"

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

namespace NiriCapture
{
// scale <= 0 lets grim pick its default (the greatest output scale). Callers
// pass the output's own scale so mixed-DPI layouts are captured natively, or
// 1 for a logical-resolution (scaled) capture.
QStringList grimArguments(const QString &outputName, bool includePointer, qreal scale = 0);
QImage captureOutput(const NiriIpc::Output &output, bool includePointer, QString *error = nullptr, bool scaled = false);
QImage captureAll(const QList<NiriIpc::Output> &outputs, bool includePointer, QString *error = nullptr, bool scaled = false);
QStringList windowScreenshotArguments(quint64 windowId, const QString &path, bool includePointer);
// niri's screenshot-window action also writes the image to the clipboard.
// When restoreClipboard is true the previous clipboard contents are snapshotted
// before the action runs and restored afterwards.
QImage captureWindow(quint64 windowId, bool includePointer, qreal scale, bool restoreClipboard, QString *error = nullptr);
}

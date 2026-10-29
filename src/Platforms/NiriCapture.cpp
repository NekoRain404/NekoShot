/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "NiriCapture.h"
#include "ClipboardGuard.h"
#include "ImageComposition.h"
#include "ImageMetaData.h"
#include "StartupProfile.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryFile>
#include <QThread>
#include <QtConcurrentRun>

#include <memory>

#include <optional>

namespace
{
constexpr int s_timeoutMsec = 5000;
// niri writes the screenshot file before it updates the clipboard (measured
// 6–150 ms apart), so restoring right after the image loads can still be
// overwritten. 150 ms covers the observed trailing write without adding a
// visible 300 ms to every window capture.
constexpr int s_clipboardSettleMsec = 150;

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

// Runs a program with an argument list (never a shell string). Every failure
// path writes a human readable message to error.
bool runProcess(const QString &program, const QStringList &arguments, QByteArray *standardOutput, QString *error)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();

    if (!process.waitForStarted(s_timeoutMsec)) {
        QString message = process.errorString();
        if (message.isEmpty()) {
            message = QStringLiteral("Could not start %1").arg(program);
        }
        setError(error, message);
        return false;
    }
    if (!process.waitForFinished(s_timeoutMsec)) {
        process.kill();
        process.waitForFinished();
        setError(error, QStringLiteral("%1 timed out after %2 ms").arg(program).arg(s_timeoutMsec));
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit) {
        setError(error, QStringLiteral("%1 crashed").arg(program));
        return false;
    }
    if (process.exitCode() != 0) {
        QString message = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (message.isEmpty()) {
            message = QStringLiteral("%1 exited with code %2").arg(program).arg(process.exitCode());
        }
        setError(error, message);
        return false;
    }

    const QByteArray output = process.readAllStandardOutput();
    if (standardOutput) {
        *standardOutput = output;
    }
    return true;
}
}

namespace NiriCapture
{
QStringList grimArguments(const QString &outputName, bool includePointer, qreal scale)
{
    QStringList arguments{QStringLiteral("-t"), QStringLiteral("ppm")};
    // grim defaults -s to the greatest output scale. Without an explicit
    // value a 100% monitor next to a 175% one is captured at 175% and then
    // upscaled, so every capture names its own factor.
    if (scale > 0) {
        arguments << QStringLiteral("-s") << QString::number(scale);
    }
    arguments << QStringLiteral("-o") << outputName;
    if (includePointer) {
        arguments.append(QStringLiteral("-c"));
    }
    arguments.append(QStringLiteral("-"));
    return arguments;
}

QImage captureOutput(const NiriIpc::Output &output, bool includePointer, QString *error, bool scaled)
{
    StartupProfile::mark("capture-output-enter");
    const qreal outputScale = output.scale > 0 ? output.scale : 1.0;
    const qreal captureScale = scaled ? 1.0 : outputScale;
    QByteArray standardOutput;
    if (!runProcess(QStringLiteral("grim"), grimArguments(output.name, includePointer, captureScale), &standardOutput, error)) {
        return {};
    }

    StartupProfile::mark("capture-output-bytes");
    QImage image = QImage::fromData(standardOutput, "PPM");
    if (image.isNull()) {
        setError(error, QStringLiteral("grim produced no image for output %1").arg(output.name));
        return {};
    }
    StartupProfile::mark("capture-output-decoded");

    image.setDevicePixelRatio(captureScale);
    ImageMetaData::setLogicalXY(image, output.logicalRect.x(), output.logicalRect.y());
    ImageMetaData::setScreen(image, output.name);
    return image;
}

QImage captureAll(const QList<NiriIpc::Output> &outputs, bool includePointer, QString *error, bool scaled)
{
    if (outputs.isEmpty()) {
        setError(error, QStringLiteral("No outputs"));
        return {};
    }

    QList<QImage> images;
    images.reserve(outputs.size());
    for (const auto &output : outputs) {
        const QImage image = captureOutput(output, includePointer, error, scaled);
        if (image.isNull()) {
            return {};
        }
        images.append(image);
    }

    StartupProfile::mark("capture-all-composed");
    return ImageComposition::combinedImage(images);
}

// Returns the complete argv for the niri executable, including the required
// "msg" subcommand.
QStringList windowScreenshotArguments(quint64 windowId, const QString &path, bool includePointer)
{
    return {QStringLiteral("msg"),
            QStringLiteral("action"),
            QStringLiteral("screenshot-window"),
            QStringLiteral("--id"),
            QString::number(windowId),
            QStringLiteral("--path"),
            path,
            QStringLiteral("-d"),
            QStringLiteral("true"),
            QStringLiteral("-p"),
            includePointer ? QStringLiteral("true") : QStringLiteral("false")};
}

QImage captureWindow(quint64 windowId, bool includePointer, qreal scale, bool restoreClipboard, QString *error)
{
    // niri requires an absolute path; QTemporaryFile creates the file with a
    // unique name so concurrent captures cannot clobber each other.
    QTemporaryFile file(QStringLiteral("/tmp/nekoshot-XXXXXX.png"));
    if (!file.open()) {
        setError(error, file.errorString());
        return {};
    }
    const QString path = file.fileName();
    file.close();

    // The screenshot-window action also copies the image to the clipboard and
    // writes it to the given path; we only consume the file. Snapshot the old
    // clipboard first so the user's contents survive the capture, and restore
    // it explicitly once the capture is known to have completed.
    std::optional<ClipboardGuard> clipboardGuard;
    if (restoreClipboard) {
        clipboardGuard.emplace();
    }

    QString processError;
    const bool ran = runProcess(QStringLiteral("niri"), windowScreenshotArguments(windowId, path, includePointer), nullptr, &processError);
    QImage image;
    if (ran) {
        // `niri msg action screenshot-window` replies before the compositor has
        // finished the capture, so the file (and the clipboard side effect)
        // appear a moment later. Wait for a loadable image before consuming it.
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (!image.load(path) && waitTimer.elapsed() < s_timeoutMsec) {
            QThread::msleep(5);
        }
    }
    file.remove();

    // A capture that never produced an image did not complete, so the
    // clipboard may not have been touched at all; leave it alone.
    if (!ran) {
        if (clipboardGuard) {
            clipboardGuard->discard();
        }
        setError(error, processError);
        return {};
    }
    if (image.isNull()) {
        if (clipboardGuard) {
            clipboardGuard->discard();
        }
        setError(error, QStringLiteral("niri did not write a screenshot to %1").arg(path));
        return {};
    }

    if (clipboardGuard) {
        // The file is on disk but niri's clipboard write can still be in
        // flight. Do not block the caller (and the viewer) for the settle
        // window: hand the snapshot to a short background task that restores
        // it after niri has finished. Main waits for the pool before exiting,
        // so a one-shot `-b` capture still restores the clipboard.
        if (clipboardGuard->hasSnapshot()) {
            auto guard = std::make_shared<ClipboardGuard>(std::move(*clipboardGuard));
            QtConcurrent::run([guard] {
                ClipboardGuard::restoreAfterDelay(guard, s_clipboardSettleMsec);
            });
        } else {
            clipboardGuard->discard();
        }
    }

    if (scale > 0) {
        image.setDevicePixelRatio(scale);
    }
    return image;
}
}

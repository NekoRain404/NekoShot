/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ImagePlatformNiri.h"
#include "NiriCapture.h"
#include "NiriIpc.h"
#include "StartupProfile.h"
#include "settings.h"

#include <KLocalizedString>

#include <QFuture>
#include <QGuiApplication>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QScreen>
#include <QStandardPaths>
#include <QString>

#include <optional>

#include <QtConcurrentRun>

namespace
{
bool isAllScreensMode(ImagePlatform::GrabMode grabMode)
{
    return grabMode == ImagePlatform::AllScreens || grabMode == ImagePlatform::AllScreensScaled || grabMode == ImagePlatform::PerScreenImageNative;
}

// Signals must be emitted from the object's thread, so the worker hands the
// outcome back to the event loop instead of emitting directly.
void postFailure(const QPointer<ImagePlatformNiri> &platform, const QString &message)
{
    if (!platform) {
        return;
    }
    QMetaObject::invokeMethod(
        qApp,
        [platform, message] {
            if (platform) {
                Q_EMIT platform->newScreenshotFailed(message);
            }
        },
        Qt::QueuedConnection);
}

void postCancel(const QPointer<ImagePlatformNiri> &platform)
{
    if (!platform) {
        return;
    }
    QMetaObject::invokeMethod(
        qApp,
        [platform] {
            if (platform) {
                Q_EMIT platform->newScreenshotCanceled();
            }
        },
        Qt::QueuedConnection);
}

void postImage(const QPointer<ImagePlatformNiri> &platform, const QImage &image, bool croppable)
{
    if (!platform) {
        return;
    }
    QMetaObject::invokeMethod(
        qApp,
        [platform, image, croppable] {
            if (!platform) {
                return;
            }
            if (croppable) {
                Q_EMIT platform->newCroppableScreenshotTaken(image);
            } else {
                Q_EMIT platform->newScreenshotTaken(image);
            }
        },
        Qt::QueuedConnection);
}

QString errorOr(const QString &error, const QString &fallback)
{
    return error.isEmpty() ? fallback : error;
}

// niri captures at physical resolution; use the scale of the output that
// hosts the window so the resulting image has the correct logical size. The
// focused output is only a fallback: with mixed-DPI monitors it may be a
// different screen than the window.
void captureWindowAndPost(const QPointer<ImagePlatformNiri> &platform,
                          const NiriIpc::Window &window,
                          bool includePointer,
                          bool restoreClipboard,
                          QString &error)
{
    qreal scale = 0;
    if (qGuiApp && qGuiApp->screens().size() == 1 && qGuiApp->primaryScreen()) {
        // A single output has one scale; avoid two niri IPC round-trips when
        // the answer is already in Qt's screen model.
        scale = qGuiApp->primaryScreen()->devicePixelRatio();
    } else {
        QString lookupError;
        if (const auto output = NiriIpc::outputForWindow(window, &lookupError)) {
            scale = output->scale;
        } else if (const auto output = NiriIpc::focusedOutput(&lookupError)) {
            scale = output->scale;
        } else if (qGuiApp && qGuiApp->primaryScreen()) {
            scale = qGuiApp->primaryScreen()->devicePixelRatio();
        }
    }
    if (scale <= 0) {
        scale = 1.0;
    }
    error.clear();

    const QImage image = NiriCapture::captureWindow(window.id, includePointer, scale, restoreClipboard, &error);
    if (image.isNull()) {
        postFailure(platform, errorOr(error, i18n("Failed to capture the window")));
        return;
    }
    postImage(platform, image, false);
}
}

ImagePlatformNiri::ImagePlatformNiri(QObject *parent)
    : ImagePlatform(parent)
{
    // The scaled all-screens mode only makes sense when there is more than one
    // output to combine, matching the KWin backend.
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, &ImagePlatformNiri::supportedGrabModesChanged);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &ImagePlatformNiri::supportedGrabModesChanged);
    }
}

ImagePlatform::GrabModes ImagePlatformNiri::supportedGrabModes() const
{
    GrabModes modes = GrabMode::AllScreens | GrabMode::CurrentScreen | GrabMode::ActiveWindow | GrabMode::WindowUnderCursor | GrabMode::PerScreenImageNative;
    if (qGuiApp && qGuiApp->screens().size() > 1) {
        modes |= GrabMode::AllScreensScaled;
    }
    return modes;
}

ImagePlatform::ShutterModes ImagePlatformNiri::supportedShutterModes() const
{
    return {ShutterMode::Immediate};
}

void ImagePlatformNiri::doGrab(ShutterMode shutterMode, GrabMode grabMode, bool includePointer, bool includeDecorations, bool includeShadow)
{
    StartupProfile::mark("doGrab-enter");
    Q_UNUSED(shutterMode)
    Q_UNUSED(includeDecorations)
    Q_UNUSED(includeShadow)

    const bool allScreens = isAllScreensMode(grabMode);
    const bool allScreensScaled = grabMode == GrabMode::AllScreensScaled;
    const bool currentScreen = grabMode == GrabMode::CurrentScreen;
    const bool activeWindow = grabMode == GrabMode::ActiveWindow;
    const bool windowUnderCursor = grabMode == GrabMode::WindowUnderCursor;
    if (!allScreens && !currentScreen && !activeWindow && !windowUnderCursor) {
        Q_EMIT newScreenshotFailed(i18n("Unsupported capture mode"));
        return;
    }

    // Window capture goes through niri's screenshot-window action, so grim is
    // only required for the screen-based modes.
    if (!activeWindow && !windowUnderCursor && QStandardPaths::findExecutable(QStringLiteral("grim")).isEmpty()) {
        // Never start a process when a required executable is missing.
        Q_EMIT newScreenshotFailed(i18n("grim is not installed"));
        return;
    }

    const bool croppable = grabMode == GrabMode::PerScreenImageNative;
    // Settings must be read on this (the main) thread, not in the worker.
    const bool restoreClipboard = Settings::restoreClipboardAfterWindowCapture();
    const QPointer<ImagePlatformNiri> platform(this);

    QFuture<void> future =
        QtConcurrent::run([platform, allScreens, allScreensScaled, activeWindow, windowUnderCursor, includePointer, restoreClipboard, croppable] {
            QString error;

            if (activeWindow || windowUnderCursor) {
                std::optional<NiriIpc::Window> window;
                if (activeWindow) {
                    window = NiriIpc::focusedWindow(&error);
                } else {
                    window = NiriIpc::pickWindow(&error);
                }

                if (!window) {
                    if (activeWindow) {
                        postFailure(platform, errorOr(error, i18n("No focused window")));
                    } else if (error.isEmpty()) {
                        // Pressing Esc makes niri print a literal null document
                        // without an error; that is a user cancellation.
                        postCancel(platform);
                    } else {
                        postFailure(platform, error);
                    }
                    return;
                }

                captureWindowAndPost(platform, *window, includePointer, restoreClipboard, error);
                return;
            }

            QList<NiriIpc::Output> outputs;
            if (allScreens) {
                outputs = NiriIpc::outputs(&error);
                if (outputs.isEmpty()) {
                    postFailure(platform, errorOr(error, i18n("No outputs found")));
                    return;
                }
            } else {
                const auto output = NiriIpc::focusedOutput(&error);
                if (!output) {
                    postFailure(platform, errorOr(error, i18n("No focused output found")));
                    return;
                }
                outputs.append(*output);
            }

            QImage image;
            if (allScreens) {
                image = NiriCapture::captureAll(outputs, includePointer, &error, allScreensScaled);
            } else {
                image = NiriCapture::captureOutput(outputs.constFirst(), includePointer, &error);
            }

            if (image.isNull()) {
                postFailure(platform, errorOr(error, i18n("Failed to capture the screen")));
                return;
            }

            postImage(platform, image, croppable);
        });
    // The grab finishes asynchronously; the future is intentionally not waited on.
    Q_UNUSED(future)
}

#include "moc_ImagePlatformNiri.cpp"

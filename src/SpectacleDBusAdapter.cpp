/* This file is part of Spectacle, the KDE screenshot utility
 * SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SpectacleDBusAdapter.h"
#include "Platforms/ImagePlatform.h"
#include "settings.h"

SpectacleDBusAdapter::SpectacleDBusAdapter(SpectacleCore *parent)
    : QDBusAbstractAdaptor(parent)
{
    setAutoRelaySignals(false);
}

inline SpectacleCore *SpectacleDBusAdapter::parent() const
{
    return static_cast<SpectacleCore *>(QObject::parent());
}

namespace
{
// niri key bindings and the CLI use KDBusService activation/forwarding, not
// this adapter, so disabling this only closes the third-party capture API.
bool remoteCaptureAllowed()
{
    if (Settings::dbusRemoteCapture()) {
        return true;
    }
    qWarning() << "Ignoring D-Bus capture request: dbusRemoteCapture is disabled";
    return false;
}
}

void SpectacleDBusAdapter::FullScreen(int includeMousePointer)
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->takeNewScreenshot(CaptureModeModel::AllScreens, 0, (includeMousePointer == -1) ? Settings::includePointer() : includeMousePointer, true);
}

void SpectacleDBusAdapter::CurrentScreen(int includeMousePointer)
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->takeNewScreenshot(CaptureModeModel::CurrentScreen, 0, (includeMousePointer == -1) ? Settings::includePointer() : includeMousePointer, true);
}

void SpectacleDBusAdapter::ActiveWindow(int includeWindowDecorations, int includeMousePointer, int includeWindowShadow)
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->takeNewScreenshot(CaptureModeModel::ActiveWindow,
                                0,
                                (includeMousePointer == -1) ? Settings::includePointer() : includeMousePointer,
                                includeWindowDecorations == -1 ? Settings::includeDecorations() : includeWindowDecorations,
                                includeWindowShadow == -1 ? Settings::includeShadow() : includeWindowShadow);
}

void SpectacleDBusAdapter::WindowUnderCursor(int includeWindowDecorations, int includeMousePointer, int includeWindowShadow)
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->takeNewScreenshot(CaptureModeModel::WindowUnderCursor,
                                0,
                                (includeMousePointer == -1) ? Settings::includePointer() : includeMousePointer,
                                includeWindowDecorations == -1 ? Settings::includeDecorations() : includeWindowDecorations,
                                includeWindowShadow == -1 ? Settings::includeShadow() : includeWindowShadow);
}

void SpectacleDBusAdapter::RectangularRegion(int includeMousePointer)
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->takeNewScreenshot(CaptureModeModel::RectangularRegion, 0, (includeMousePointer == -1) ? Settings::includePointer() : includeMousePointer, false);
}

void SpectacleDBusAdapter::RecordRegion(int includeMousePointer)
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->startRecording(VideoPlatform::Region, includeMousePointer == -1 ? Settings::videoIncludePointer() : includeMousePointer);
}

void SpectacleDBusAdapter::RecordScreen(int includeMousePointer)
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->startRecording(VideoPlatform::Screen, includeMousePointer == -1 ? Settings::videoIncludePointer() : includeMousePointer);
}

void SpectacleDBusAdapter::RecordWindow(int includeMousePointer)
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->startRecording(VideoPlatform::Window, includeMousePointer == -1 ? Settings::videoIncludePointer() : includeMousePointer);
}

void SpectacleDBusAdapter::StopRecording()
{
    if (!remoteCaptureAllowed()) {
        return;
    }
    // No beginDbusRequest(): stopping reads no request state, and the final
    // export URL is evaluated from the in-flight recording when it finishes.
    // Resetting it here would silently drop an explicit -o/--output target.
    if (parent()->videoPlatform()->isRecording()) {
        parent()->finishRecording();
    }
}

void SpectacleDBusAdapter::OpenWithoutScreenshot()
{
    if (!remoteCaptureAllowed() || !parent()->beginDbusRequest()) {
        return;
    }
    parent()->initGuiNoScreenshot();
}

#include "moc_SpectacleDBusAdapter.cpp"

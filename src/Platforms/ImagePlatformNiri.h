/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "ImagePlatform.h"

// Screenshot backend for the niri compositor. Screen contents come from grim;
// output geometry and focused output come from `niri msg`.
class ImagePlatformNiri final : public ImagePlatform
{
    Q_OBJECT

public:
    explicit ImagePlatformNiri(QObject *parent = nullptr);
    ~ImagePlatformNiri() override = default;

    GrabModes supportedGrabModes() const override;
    ShutterModes supportedShutterModes() const override;
    // niri's screencopy does not composite the cursor, even with grim -c.
    bool supportsScreenshotPointer() const override
    {
        return false;
    }

public Q_SLOTS:
    void
    doGrab(ImagePlatform::ShutterMode shutterMode, ImagePlatform::GrabMode grabMode, bool includePointer, bool includeDecorations, bool includeShadow) override;
};

/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

/**
 * Manual-scroll long screenshots.
 *
 * niri has no way to inject scroll events, so the user scrolls the content and
 * asks NekoShot to capture a frame between scrolls. Frames are written to a
 * runtime session directory and stitched into one tall image when the user
 * finishes, which keeps every command a short-lived process.
 */
namespace LongScreenshot
{
/// Stitches frames that share a vertical overlap into one image.
class Stitcher
{
public:
    struct Result {
        bool ok = false;
        /// The frame was already contained in the stitched image (no scroll).
        bool duplicate = false;
        int appendedRows = 0;
        QString error;
    };

    void reset();
    Result addFrame(const QImage &frame);
    QImage image() const;
    int frameRows() const;

    /// Largest image height that is still accepted, to keep memory bounded.
    static constexpr int s_maxHeight = 40000;

private:
    QImage m_image;
};

/// Session helpers. Only one session exists at a time; a new start replaces
/// it.
QString sessionRoot();
QString createSession(QString *error);
/// Path of the current session directory, or nullopt when none is active.
QString activeSession(QString *error = nullptr);
QStringList framePaths(const QString &session);
QString appendFramePath(const QString &session);
bool removeSession(const QString &session);

/// Saves a stitched image; returns the written file or an empty string.
QString saveImage(const QImage &image, const QString &outputPath, QString *error);
}

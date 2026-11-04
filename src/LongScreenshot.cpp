/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "LongScreenshot.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <cmath>

#include <cstring>

namespace
{
constexpr int s_minOverlapRows = 32;
// Frames are compared after a horizontal box downscale and a grayscale
// conversion. That keeps glyph rasterisation differences caused by fractional
// HiDPI scrolling from turning every row into a mismatch, while still leaving
// enough detail to tell different content apart.
constexpr int s_matchDownscale = 4;
constexpr double s_maxOverlapScore = 6.0;
// The best candidate has to be clearly better than anything outside its
// neighbourhood, otherwise the result is ambiguous.
constexpr double s_uniquenessFactor = 2.0;

QImage matchImage(const QImage &image, int verticalDownscale)
{
    const int width = qMax(1, image.width() / s_matchDownscale);
    const int height = qMax(1, image.height() / verticalDownscale);
    // Colour, not grayscale: a smooth vertical colour gradient can look
    // identical in luminance while different rows are still distinguishable.
    return image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
}

double overlapScore(const QImage &previous, int previousHeight, const QImage &frame, int overlap, int rowStep, int columnStep)
{
    const int start = previousHeight - overlap;
    long long difference = 0;
    int samples = 0;
    for (int row = 0; row < overlap; row += rowStep) {
        const QRgb *previousLine = reinterpret_cast<const QRgb *>(previous.constScanLine(start + row));
        const QRgb *frameLine = reinterpret_cast<const QRgb *>(frame.constScanLine(row));
        for (int x = 0; x < frame.width(); x += columnStep) {
            const QRgb a = previousLine[x];
            const QRgb b = frameLine[x];
            difference += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
            ++samples;
        }
    }
    if (samples == 0) {
        return 1e9;
    }
    // Mean absolute difference over the three channels.
    return static_cast<double>(difference) / (samples * 3);
}

struct Match {
    int overlap = 0;
    double score = 1e9;
    bool ambiguous = false;
};

Match findOverlap(const QImage &previous, const QImage &frame)
{
    Match match;
    const int previousHeight = previous.height();
    const int frameHeight = frame.height();
    const int maximum = qMin(previousHeight, frameHeight);
    // Requiring a quarter of the frame as overlap keeps small windows from
    // winning by comparing only a handful of uniform rows.
    const int minimum = qMin(maximum, qMax(s_minOverlapRows, frameHeight / 4));
    if (maximum <= minimum) {
        return match;
    }

    // Coarse pass on a 4x4 downscale: the candidate grid scales with the
    // content, so a sharp minimum cannot fall between two coarse candidates.
    const QImage previousCoarse = matchImage(previous, s_matchDownscale);
    const QImage frameCoarse = matchImage(frame, s_matchDownscale);
    const int coarsePreviousHeight = previousCoarse.height();
    const int coarseFrameHeight = frameCoarse.height();
    const int coarseMaximum = qMin(coarsePreviousHeight, coarseFrameHeight);
    const int coarseMinimum = qMax(2, minimum / s_matchDownscale);
    int coarseBest = 0;
    double coarseScore = 1e9;
    double coarseOutside = 1e9;
    for (int candidate = coarseMaximum; candidate >= coarseMinimum; --candidate) {
        const double score = overlapScore(previousCoarse, coarsePreviousHeight, frameCoarse, candidate, 1, 2);
        if (score < coarseScore) {
            coarseScore = score;
            coarseBest = candidate;
        }
    }
    if (coarseBest == 0) {
        return match;
    }
    for (int candidate = coarseMaximum; candidate >= coarseMinimum; --candidate) {
        if (std::abs(candidate - coarseBest) <= 3) {
            continue;
        }
        coarseOutside = qMin(coarseOutside, overlapScore(previousCoarse, coarsePreviousHeight, frameCoarse, candidate, 1, 2));
    }

    // Refine on the full row grid, still averaging four pixels horizontally.
    const QImage previousFine = matchImage(previous, 1);
    const QImage frameFine = matchImage(frame, 1);
    match.overlap = qBound(minimum, coarseBest * s_matchDownscale, maximum);
    match.score = overlapScore(previousFine, previousHeight, frameFine, match.overlap, 2, 1);
    const int refineLow = qMax(minimum, match.overlap - s_matchDownscale - 2);
    const int refineHigh = qMin(maximum, match.overlap + s_matchDownscale + 2);
    for (int candidate = refineLow; candidate <= refineHigh; ++candidate) {
        const double score = overlapScore(previousFine, previousHeight, frameFine, candidate, 2, 1);
        if (score < match.score) {
            match.score = score;
            match.overlap = candidate;
        }
    }

    match.ambiguous = coarseOutside < match.score * s_uniquenessFactor;
    return match;
}
}

namespace LongScreenshot
{
void Stitcher::reset()
{
    m_image = QImage();
}

QImage Stitcher::image() const
{
    return m_image;
}

int Stitcher::frameRows() const
{
    return m_image.isNull() ? 0 : m_image.height();
}

Stitcher::Result Stitcher::addFrame(const QImage &frame)
{
    Result result;
    if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0) {
        result.error = QStringLiteral("empty frame");
        return result;
    }

    const QImage normalized = frame.format() == QImage::Format_ARGB32 ? frame : frame.convertToFormat(QImage::Format_ARGB32);
    if (m_image.isNull()) {
        m_image = normalized.copy();
        result.ok = true;
        result.appendedRows = m_image.height();
        return result;
    }
    if (normalized.width() != m_image.width()) {
        result.error = QStringLiteral("frame width changed");
        return result;
    }

    const int previousHeight = m_image.height();
    const int frameHeight = normalized.height();

    const Match match = findOverlap(m_image, normalized);
    if (match.overlap == 0) {
        result.error = QStringLiteral("the frame does not overlap the previous one");
        return result;
    }
    if (match.score > s_maxOverlapScore || match.ambiguous) {
        result.error = QStringLiteral("the frame does not overlap the previous one");
        return result;
    }
    const int overlap = match.overlap;
    if (overlap >= frameHeight) {
        result.ok = true;
        result.duplicate = true;
        result.appendedRows = 0;
        return result;
    }

    const int appended = frameHeight - overlap;
    if (previousHeight + appended > s_maxHeight) {
        result.error = QStringLiteral("the stitched image would be too tall");
        return result;
    }

    QImage next(m_image.width(), previousHeight + appended, QImage::Format_ARGB32);
    if (next.isNull()) {
        result.error = QStringLiteral("not enough memory for the stitched image");
        return result;
    }
    memcpy(next.scanLine(0), m_image.constScanLine(0), static_cast<size_t>(m_image.sizeInBytes()));
    memcpy(next.scanLine(previousHeight), normalized.constScanLine(overlap), static_cast<size_t>(appended) * static_cast<size_t>(normalized.bytesPerLine()));
    m_image = next;
    result.ok = true;
    result.appendedRows = appended;
    return result;
}

QString sessionRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    return base + QStringLiteral("/nekoshot-long");
}

QString createSession(QString *error)
{
    QDir root(sessionRoot());
    if (!root.exists() && !QDir().mkpath(root.absolutePath())) {
        if (error) {
            *error = QStringLiteral("could not create %1").arg(root.absolutePath());
        }
        return {};
    }
    // Only one session is active; a fresh start replaces the leftovers.
    const QStringList stale = root.entryList({QStringLiteral("session-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : stale) {
        QDir(root.filePath(entry)).removeRecursively();
    }
    const QString name = QStringLiteral("session-%1").arg(QDateTime::currentMSecsSinceEpoch());
    if (!root.mkpath(name)) {
        if (error) {
            *error = QStringLiteral("could not create the session folder");
        }
        return {};
    }
    const QString session = root.filePath(name);
    if (!QDir(session).mkpath(QStringLiteral("frames"))) {
        if (error) {
            *error = QStringLiteral("could not create the frame folder");
        }
        return {};
    }
    return session;
}

QString activeSession(QString *error)
{
    QDir root(sessionRoot());
    if (!root.exists()) {
        if (error) {
            *error = QStringLiteral("no long screenshot session is active");
        }
        return {};
    }
    const QStringList sessions = root.entryList({QStringLiteral("session-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    if (sessions.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no long screenshot session is active");
        }
        return {};
    }
    return root.filePath(sessions.constFirst());
}

QStringList framePaths(const QString &session)
{
    QDir frames(QDir(session).filePath(QStringLiteral("frames")));
    const QStringList names = frames.entryList({QStringLiteral("frame-*.png")}, QDir::Files, QDir::Name);
    QStringList paths;
    paths.reserve(names.size());
    for (const QString &name : names) {
        paths.append(frames.filePath(name));
    }
    return paths;
}

QString appendFramePath(const QString &session)
{
    const QString folder = QDir(session).filePath(QStringLiteral("frames"));
    QDir().mkpath(folder);
    int index = framePaths(session).size() + 1;
    return QDir(folder).filePath(QStringLiteral("frame-%1.png").arg(index, 4, 10, QLatin1Char('0')));
}

bool removeSession(const QString &session)
{
    if (session.isEmpty()) {
        return false;
    }
    return QDir(session).removeRecursively();
}

QString saveImage(const QImage &image, const QString &outputPath, QString *error)
{
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("nothing to save");
        }
        return {};
    }
    QString path = outputPath;
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
            + QStringLiteral("/Screenshots/nekoshot_long_%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    }
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) {
            *error = QStringLiteral("could not create %1").arg(info.absolutePath());
        }
        return {};
    }
    if (!image.save(path, "PNG")) {
        if (error) {
            *error = QStringLiteral("could not write %1").arg(path);
        }
        return {};
    }
    return path;
}
}

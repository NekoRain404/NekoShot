/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "LongScreenshot.h"

#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace
{
// Random-looking content: every pixel is unique, like a photograph or a page
// of text at native resolution. A smooth gradient would alias against itself
// at wrong offsets and make the test meaningless.
QImage makePage(int width, int height)
{
    QImage page(width, height, QImage::Format_ARGB32);
    quint32 state = 0x12345678u;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            state = state * 1664525u + 1013904223u;
            const int value = static_cast<int>((state >> 16) & 0xff);
            page.setPixel(x, y, qRgb(value, (value * 7) & 0xff, (value * 13) & 0xff));
        }
    }
    return page;
}

QImage frameAt(const QImage &page, int offset, int height)
{
    return page.copy(0, offset, page.width(), height);
}
}

class LongScreenshotTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testStitchScroll();
    void testDuplicateFrame();
    void testNoOverlap();
    void testWidthChange();
    void testSinglePixelScroll();
    void testSaveImage();
    void testSparseMarkers();
};

void LongScreenshotTest::testStitchScroll()
{
    const QImage page = makePage(120, 900);
    LongScreenshot::Stitcher stitcher;

    auto first = stitcher.addFrame(frameAt(page, 0, 300));
    QVERIFY(first.ok);
    QCOMPARE(first.appendedRows, 300);

    // Scrolling by 200 px leaves a 100 px overlap, so 200 new rows arrive.
    auto second = stitcher.addFrame(frameAt(page, 200, 300));
    QVERIFY(second.ok);
    QCOMPARE(second.appendedRows, 200);
    QCOMPARE(stitcher.image().height(), 500);

    auto third = stitcher.addFrame(frameAt(page, 400, 300));
    QVERIFY(third.ok);
    QCOMPARE(third.appendedRows, 200);
    QCOMPARE(stitcher.image().height(), 700);

    // Every stitched row is the matching row of the original page.
    for (int y = 0; y < 700; ++y) {
        QCOMPARE(stitcher.image().pixel(0, y), page.pixel(0, y));
    }
}

void LongScreenshotTest::testDuplicateFrame()
{
    const QImage page = makePage(120, 600);
    LongScreenshot::Stitcher stitcher;
    QVERIFY(stitcher.addFrame(frameAt(page, 0, 300)).ok);

    const auto duplicate = stitcher.addFrame(frameAt(page, 0, 300));
    QVERIFY(duplicate.ok);
    QVERIFY(duplicate.duplicate);
    QCOMPARE(duplicate.appendedRows, 0);
    QCOMPARE(stitcher.image().height(), 300);
}

void LongScreenshotTest::testNoOverlap()
{
    const QImage page = makePage(120, 900);
    LongScreenshot::Stitcher stitcher;
    QVERIFY(stitcher.addFrame(frameAt(page, 0, 300)).ok);

    // Scrolled by more than a full frame: nothing overlaps.
    const auto result = stitcher.addFrame(frameAt(page, 400, 300));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(stitcher.image().height(), 300);
}

void LongScreenshotTest::testWidthChange()
{
    LongScreenshot::Stitcher stitcher;
    QVERIFY(stitcher.addFrame(makePage(120, 200)).ok);
    const auto result = stitcher.addFrame(makePage(100, 200));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void LongScreenshotTest::testSinglePixelScroll()
{
    const QImage page = makePage(80, 400);
    LongScreenshot::Stitcher stitcher;
    QVERIFY(stitcher.addFrame(frameAt(page, 0, 300)).ok);
    const auto result = stitcher.addFrame(frameAt(page, 1, 300));
    QVERIFY(result.ok);
    QCOMPARE(result.appendedRows, 1);
    QCOMPARE(stitcher.image().height(), 301);
}

void LongScreenshotTest::testSaveImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QImage image = makePage(40, 50);
    QString error;
    const QString path = LongScreenshot::saveImage(image, dir.filePath(u"out.png"_s), &error);
    QVERIFY2(!path.isEmpty(), qPrintable(error));
    QImage loaded(path);
    QCOMPARE(loaded.size(), image.size());

    error.clear();
    QVERIFY(LongScreenshot::saveImage(QImage(), dir.filePath(u"empty.png"_s), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void LongScreenshotTest::testSparseMarkers()
{
    // Mostly uniform rows whose only difference is a single marker that moves
    // with the row. A sampled-column comparison can miss the marker; full-row
    // matching must not.
    const int width = 120;
    const int height = 600;
    QImage page(width, height, QImage::Format_ARGB32);
    page.fill(Qt::black);
    for (int y = 0; y < height; ++y) {
        // The marker position repeats every `width` rows, so the colour also
        // encodes the row to keep every row unique.
        page.setPixel(y % width, y, qRgb(y & 0xff, (y >> 8) & 0xff, 128));
    }

    LongScreenshot::Stitcher stitcher;
    QVERIFY(stitcher.addFrame(page.copy(0, 0, width, 300)).ok);
    const auto second = stitcher.addFrame(page.copy(0, 200, width, 300));
    QVERIFY(second.ok);
    QCOMPARE(second.appendedRows, 200);
    QCOMPARE(stitcher.image().height(), 500);
    for (int y = 0; y < 500; ++y) {
        QCOMPARE(stitcher.image().pixel(0, y), page.pixel(0, y));
    }
}

QTEST_GUILESS_MAIN(LongScreenshotTest)
#include "LongScreenshotTest.moc"

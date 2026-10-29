#include "Platforms/NiriCapture.h"
#include "ImageMetaData.h"
#include "Platforms/ImageComposition.h"
#include "Platforms/ImagePlatformNiri.h"
#include <QColor>
#include <QImage>
#include <QTest>

using namespace Qt::StringLiterals;

class NiriCaptureTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testGrimArguments();
    void testWindowArguments();
    void testCaptureAllNoOutputs();
    void testCombineSameDpr();
    void testCombineMixedDpr();
    void testCombineNegativeOrigin();
    void testCombineEmpty();
    void testSupportedModes();
    void testImageLocalRect();
    void testCombineMixedDprNegativeOrigin();
};

static QImage solid(int w, int h, qreal dpr, const QPointF &logical, QColor color)
{
    QImage image(w, h, QImage::Format_RGBA8888_Premultiplied);
    image.fill(color);
    image.setDevicePixelRatio(dpr);
    ImageMetaData::setLogicalXY(image, logical.x(), logical.y());
    return image;
}

void NiriCaptureTest::testGrimArguments()
{
    QCOMPARE(NiriCapture::grimArguments(u"eDP-1"_s, false), QStringList({u"-t"_s, u"ppm"_s, u"-o"_s, u"eDP-1"_s, u"-"_s}));
    QCOMPARE(NiriCapture::grimArguments(u"eDP-1"_s, true), QStringList({u"-t"_s, u"ppm"_s, u"-o"_s, u"eDP-1"_s, u"-c"_s, u"-"_s}));
    // Native capture must name the output's scale; grim would otherwise use
    // the greatest scale of the whole layout and upscale a 100% monitor.
    QCOMPARE(NiriCapture::grimArguments(u"eDP-1"_s, false, 1.75), QStringList({u"-t"_s, u"ppm"_s, u"-s"_s, u"1.75"_s, u"-o"_s, u"eDP-1"_s, u"-"_s}));
    QCOMPARE(NiriCapture::grimArguments(u"HDMI-A-1"_s, true, 1.0), QStringList({u"-t"_s, u"ppm"_s, u"-s"_s, u"1"_s, u"-o"_s, u"HDMI-A-1"_s, u"-c"_s, u"-"_s}));
}

void NiriCaptureTest::testWindowArguments()
{
    // This is the complete argv passed to QProcess: niri requires the "msg"
    // subcommand before "action", so it must be the first element.
    const QStringList arguments = NiriCapture::windowScreenshotArguments(42, u"/tmp/x.png"_s, true);
    QCOMPARE(arguments.first(), u"msg"_s);
    QVERIFY(arguments.contains(u"msg"_s));
    QCOMPARE(
        arguments,
        QStringList({u"msg"_s, u"action"_s, u"screenshot-window"_s, u"--id"_s, u"42"_s, u"--path"_s, u"/tmp/x.png"_s, u"-d"_s, u"true"_s, u"-p"_s, u"true"_s}));
}

void NiriCaptureTest::testCaptureAllNoOutputs()
{
    QString error;
    const QImage out = NiriCapture::captureAll({}, false, &error);
    QVERIFY(out.isNull());
    QVERIFY(!error.isEmpty());
}

void NiriCaptureTest::testCombineSameDpr()
{
    QImage a = solid(800, 600, 1.0, {0, 0}, Qt::red);
    QImage b = solid(800, 600, 1.0, {800, 0}, Qt::blue);
    const QImage out = ImageComposition::combinedImage({a, b});
    QCOMPARE(out.size(), QSize(1600, 600));
    QCOMPARE(out.devicePixelRatio(), 1.0);
    QCOMPARE(ImageMetaData::logicalXY(out), QPointF(0, 0));
    QCOMPARE(ImageMetaData::subGeometryList(out).size(), 2);
    QCOMPARE(out.pixelColor(10, 10), QColor(Qt::red));
    QCOMPARE(out.pixelColor(810, 10), QColor(Qt::blue));
}

void NiriCaptureTest::testCombineMixedDpr()
{
    QImage a = solid(800, 600, 1.0, {0, 0}, Qt::red);
    QImage b = solid(800, 600, 1.75, {800, 0}, Qt::blue);
    const QImage out = ImageComposition::combinedImage({a, b});
    QCOMPARE(out.devicePixelRatio(), 2.0); // ceil(1.75)
    QCOMPARE(ImageMetaData::subGeometryList(out).size(), 2);
    QVERIFY(qAbs(out.width() - 2514) <= 2);
    QCOMPARE(out.pixelColor(10, 10), QColor(Qt::red));
    QCOMPARE(out.pixelColor(1700, 10), QColor(Qt::blue));
}

void NiriCaptureTest::testCombineNegativeOrigin()
{
    QImage a = solid(800, 600, 1.0, {-800, 0}, Qt::red);
    QImage b = solid(800, 600, 1.0, {0, 0}, Qt::blue);
    const QImage out = ImageComposition::combinedImage({a, b});
    QCOMPARE(out.size(), QSize(1600, 600));
    QCOMPARE(ImageMetaData::logicalXY(out), QPointF(-800, 0));
    QCOMPARE(out.pixelColor(10, 10), QColor(Qt::red));
    QCOMPARE(out.pixelColor(810, 10), QColor(Qt::blue));
}

void NiriCaptureTest::testCombineEmpty()
{
    QVERIFY(ImageComposition::combinedImage({}).isNull());
}

void NiriCaptureTest::testSupportedModes()
{
    ImagePlatformNiri platform;
    const auto modes = platform.supportedGrabModes();
    // Pin the exact set: Spectacle falls back to AllScreens for anything else.
    const ImagePlatform::GrabModes expected = ImagePlatform::AllScreens | ImagePlatform::CurrentScreen | ImagePlatform::ActiveWindow
        | ImagePlatform::WindowUnderCursor | ImagePlatform::PerScreenImageNative;
    QCOMPARE(modes, expected);
    QVERIFY(!modes.testFlag(ImagePlatform::AllScreensScaled));
    QVERIFY(!modes.testFlag(ImagePlatform::TransientWithParent));
    QCOMPARE(platform.supportedShutterModes(), ImagePlatform::ShutterModes(ImagePlatform::Immediate));
}

void NiriCaptureTest::testImageLocalRect()
{
    QImage image = solid(800, 600, 1.0, {-800, 100}, Qt::red).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QVERIFY(ImageMetaData::hasLogicalXY(image));
    QCOMPARE(ImageMetaData::logicalRect(image), QRectF(-800, 100, 800, 600));
    // A selection drawn at the very top-left of the left monitor maps to the
    // image origin instead of a negative rectangle.
    QCOMPARE(ImageMetaData::imageLocalRect(image, QRectF(-800, 100, 100, 50)), QRectF(0, 0, 100, 50));
    QCOMPARE(ImageMetaData::imageLocalRect(image, QRectF(-750, 150, 100, 50)), QRectF(50, 50, 100, 50));

    QImage noMetadata(10, 10, QImage::Format_RGBA8888_Premultiplied);
    QVERIFY(!ImageMetaData::hasLogicalXY(noMetadata));
}

void NiriCaptureTest::testCombineMixedDprNegativeOrigin()
{
    // Left monitor at 100% left of the primary, primary at 175%. The union
    // starts at -800 and must be translated correctly while the 100% image
    // is scaled up to the final 2x canvas.
    QImage a = solid(800, 600, 1.0, {-800, 0}, Qt::red);
    QImage b = solid(1400, 600, 1.75, {0, 0}, Qt::blue);
    const QImage out = ImageComposition::combinedImage({a, b});
    QCOMPARE(ImageMetaData::logicalXY(out), QPointF(-800, 0));
    QCOMPARE(out.devicePixelRatio(), 2.0);
    QCOMPARE(out.pixelColor(10, 10), QColor(Qt::red));
    QVERIFY(out.pixelColor(1790, 10) == QColor(Qt::blue) || out.pixelColor(1800, 10) == QColor(Qt::blue));
}

QTEST_GUILESS_MAIN(NiriCaptureTest)
#include "NiriCaptureTest.moc"

/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "Platforms/NiriIpc.h"
#include "Platforms/VideoPlatformNiri.h"

#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

class WfRecorderArgsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testRegionArguments();
    void testScreenArgumentsWithAudio();
    void testMicrophoneArguments();
    void testNoFormat();
    void testEffectiveFormatExplicitOutput();
    void testEffectiveFormatPreferredOutput();
    void testEffectiveFormatRejectsUnsupported();
    void testRegionNeedsSelection();
    void testRegionRequestedWithoutRectangle();
    void testSupportedRecordingModes();
    void testGstVp9Arguments();
    void testGstVp9AudioArguments();
    void testGstH264Arguments();
    void testGstH264PrefersVaApi();
    void testGstHevcArguments();
    void testGstHevcAudioArguments();
    void testGstAv1Arguments();
    void testGstAv1AudioArguments();
    void testGstCapabilityGates();
    void testGstRejectsUnsupportedFormats();
    void testVaProfileParsing();
    void testWfRecorderCodecAvailability();
    void testBitratePresets();
    void testRegionCaptureMargins();
    void testRegionCropArguments();
    void testWindowFallbackFormat();
    void testWfRecorderHevcAv1Arguments();
    void testEffectiveFormatContainerPreference();
    void testScreencastStartGuard();
    void testScreencastPendingGeneration();
};

void WfRecorderArgsTest::testRegionArguments()
{
    const auto args =
        VideoPlatformNiri::argumentsFor(VideoPlatform::Region, u"/tmp/out.webm"_s, QRect(10, 20, 300, 200), VideoPlatform::WebM_VP9, false, QString());
    QCOMPARE(args, QStringList({u"-f"_s, u"/tmp/out.webm"_s, u"-g"_s, u"10,20 300x200"_s, u"-c"_s, u"libvpx-vp9"_s, u"-p"_s, u"b=10000000"_s}));
}

void WfRecorderArgsTest::testScreenArgumentsWithAudio()
{
    const auto args =
        VideoPlatformNiri::argumentsFor(VideoPlatform::Screen, u"/tmp/out.mp4"_s, QRect(0, 0, 1645, 1028), VideoPlatform::MP4_H264, true, QString());
    QCOMPARE(args, QStringList({u"-f"_s, u"/tmp/out.mp4"_s, u"-g"_s, u"0,0 1645x1028"_s, u"-c"_s, u"libx264"_s, u"-p"_s, u"b=10000000"_s, u"--audio"_s}));
}

void WfRecorderArgsTest::testMicrophoneArguments()
{
    const auto args = VideoPlatformNiri::argumentsFor(VideoPlatform::Region,
                                                      u"/tmp/out.webm"_s,
                                                      QRect(-10, 5, 1, 2),
                                                      VideoPlatform::WebM_VP9,
                                                      false,
                                                      u"alsa_input.usb-mic"_s);
    QCOMPARE(args,
             QStringList(
                 {u"-f"_s, u"/tmp/out.webm"_s, u"-g"_s, u"-10,5 1x2"_s, u"-c"_s, u"libvpx-vp9"_s, u"-p"_s, u"b=10000000"_s, u"--audio=alsa_input.usb-mic"_s}));
}

void WfRecorderArgsTest::testNoFormat()
{
    const auto args = VideoPlatformNiri::argumentsFor(VideoPlatform::Region, u"/tmp/out.mkv"_s, QRect(0, 0, 10, 10), VideoPlatform::NoFormat, false, QString());
    QCOMPARE(args, QStringList({u"-f"_s, u"/tmp/out.mkv"_s, u"-g"_s, u"0,0 10x10"_s}));
}

void WfRecorderArgsTest::testEffectiveFormatExplicitOutput()
{
    const auto supported = VideoPlatform::WebM_VP9 | VideoPlatform::MP4_H264;

    // The extension of an explicit output file wins over the user's preference.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.webm"_s), VideoPlatform::MP4_H264, supported), VideoPlatform::WebM_VP9);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mp4"_s), VideoPlatform::WebM_VP9, supported), VideoPlatform::MP4_H264);
}

void WfRecorderArgsTest::testEffectiveFormatPreferredOutput()
{
    const auto supported = VideoPlatform::WebM_VP9 | VideoPlatform::MP4_H264;

    // No usable extension: fall back to the preferred format.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl(), VideoPlatform::WebM_VP9, supported), VideoPlatform::WebM_VP9);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl(), VideoPlatform::MP4_H264, supported), VideoPlatform::MP4_H264);
    // An explicit unknown extension is an error, not an invitation to write an
    // MP4 stream under a .mkv name.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mkv"_s), VideoPlatform::MP4_H264, supported), VideoPlatform::NoFormat);
}

void WfRecorderArgsTest::testEffectiveFormatRejectsUnsupported()
{
    const auto supported = VideoPlatform::WebM_VP9 | VideoPlatform::MP4_H264;

    // GIF and WebP are not offered by supportedFormats(), so they are rejected
    // instead of producing a file without an explicit codec.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl(), VideoPlatform::Gif, supported), VideoPlatform::NoFormat);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl(), VideoPlatform::WebP, supported), VideoPlatform::NoFormat);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.webp"_s), VideoPlatform::WebM_VP9, supported), VideoPlatform::NoFormat);

    // An explicit extension that cannot be encoded is rejected as well.
    const auto vp9Only = VideoPlatform::Formats(VideoPlatform::WebM_VP9);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mp4"_s), VideoPlatform::MP4_H264, vp9Only), VideoPlatform::NoFormat);
}

void WfRecorderArgsTest::testRegionNeedsSelection()
{
    QVERIFY(VideoPlatformNiri::regionNeedsSelection({}));
    QVERIFY(VideoPlatformNiri::regionNeedsSelection({{u"rect"_s, QRect()}}));
    QVERIFY(!VideoPlatformNiri::regionNeedsSelection({{u"rect"_s, QRect(10, 20, 300, 200)}}));
    // SpectacleCore passes the selection as a QRectF.
    QVERIFY(!VideoPlatformNiri::regionNeedsSelection({{u"rect"_s, QRectF(10.5, 20.5, 300, 200)}}));
}

void WfRecorderArgsTest::testRegionRequestedWithoutRectangle()
{
    VideoPlatformNiri platform;
    QSignalSpy regionRequested(&platform, &VideoPlatform::regionRequested);
    QSignalSpy failed(&platform, &VideoPlatform::recordingFailed);

    platform.startRecording(QUrl(), VideoPlatform::Region, {}, false);

    QCOMPARE(regionRequested.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(platform.recordingState(), VideoPlatform::RecordingState::NotRecording);
}

namespace
{
VideoPlatformNiri::GstCapabilities fullGstCapabilities()
{
    VideoPlatformNiri::GstCapabilities capabilities;
    capabilities.vp9enc = true;
    capabilities.webmmux = true;
    capabilities.x264enc = true;
    capabilities.vah264enc = false;
    capabilities.h264parse = true;
    capabilities.vah265enc = true;
    capabilities.h265parse = true;
    capabilities.vaav1enc = true;
    capabilities.av1parse = true;
    capabilities.mp4mux = true;
    capabilities.opusenc = true;
    capabilities.avencAac = true;
    capabilities.pulsesrc = true;
    capabilities.audiomixer = true;
    return capabilities;
}

QStringList expectedVp9VideoNoAudio()
{
    return {u"-e"_s,
            u"compositor"_s,
            u"name=comp"_s,
            u"background=black"_s,
            u"!"_s,
            u"videoconvert"_s,
            u"!"_s,
            u"videorate"_s,
            u"!"_s,
            u"video/x-raw,framerate=30/1"_s,
            u"!"_s,
            u"vp9enc"_s,
            u"deadline=1"_s,
            u"target-bitrate=10000000"_s,
            u"!"_s,
            u"webmmux"_s,
            u"!"_s,
            u"filesink"_s,
            u"location=/tmp/rec.webm"_s,
            u"pipewiresrc"_s,
            u"path=49"_s,
            u"do-timestamp=true"_s,
            u"!"_s,
            u"videoconvert"_s,
            u"!"_s,
            u"comp.sink_0"_s};
}

QStringList expectedVp9MuxHead()
{
    return {u"-e"_s, u"webmmux"_s, u"name=mux"_s, u"!"_s, u"filesink"_s, u"location=/tmp/rec.webm"_s};
}

QStringList expectedVp9VideoToMux()
{
    return {
        u"compositor"_s,
        u"name=comp"_s,
        u"background=black"_s,
        u"!"_s,
        u"videoconvert"_s,
        u"!"_s,
        u"videorate"_s,
        u"!"_s,
        u"video/x-raw,framerate=30/1"_s,
        u"!"_s,
        u"vp9enc"_s,
        u"deadline=1"_s,
        u"target-bitrate=10000000"_s,
        u"!"_s,
        u"mux."_s,
    };
}

QStringList expectedH264VideoNoAudio()
{
    return {u"-e"_s,
            u"compositor"_s,
            u"name=comp"_s,
            u"background=black"_s,
            u"!"_s,
            u"videoconvert"_s,
            u"!"_s,
            u"videorate"_s,
            u"!"_s,
            u"video/x-raw,framerate=30/1"_s,
            u"!"_s,
            u"x264enc"_s,
            u"tune=zerolatency"_s,
            u"speed-preset=ultrafast"_s,
            u"bitrate=10000"_s,
            u"!"_s,
            u"mp4mux"_s,
            u"fragment-duration=1000"_s,
            u"!"_s,
            u"filesink"_s,
            u"location=/tmp/rec.mp4"_s,
            u"pipewiresrc"_s,
            u"path=49"_s,
            u"do-timestamp=true"_s,
            u"!"_s,
            u"videoconvert"_s,
            u"!"_s,
            u"comp.sink_0"_s};
}

QStringList expectedH264MuxHead()
{
    return {u"-e"_s, u"mp4mux"_s, u"name=mux"_s, u"fragment-duration=1000"_s, u"!"_s, u"filesink"_s, u"location=/tmp/rec.mp4"_s};
}

QStringList expectedH264VideoToMux()
{
    return {
        u"compositor"_s,
        u"name=comp"_s,
        u"background=black"_s,
        u"!"_s,
        u"videoconvert"_s,
        u"!"_s,
        u"videorate"_s,
        u"!"_s,
        u"video/x-raw,framerate=30/1"_s,
        u"!"_s,
        u"x264enc"_s,
        u"tune=zerolatency"_s,
        u"speed-preset=ultrafast"_s,
        u"bitrate=10000"_s,
        u"!"_s,
        u"mux."_s,
    };
}

QStringList expectedHevcVideoNoAudio()
{
    return {u"-e"_s,
            u"compositor"_s,
            u"name=comp"_s,
            u"background=black"_s,
            u"!"_s,
            u"videoconvert"_s,
            u"!"_s,
            u"videorate"_s,
            u"!"_s,
            u"video/x-raw,framerate=30/1"_s,
            u"!"_s,
            u"vah265enc"_s,
            u"bitrate=10000"_s,
            u"!"_s,
            u"h265parse"_s,
            u"!"_s,
            u"mp4mux"_s,
            u"fragment-duration=1000"_s,
            u"!"_s,
            u"filesink"_s,
            u"location=/tmp/rec.mp4"_s,
            u"pipewiresrc"_s,
            u"path=49"_s,
            u"do-timestamp=true"_s,
            u"!"_s,
            u"videoconvert"_s,
            u"!"_s,
            u"comp.sink_0"_s};
}

QStringList expectedHevcMuxHead()
{
    return {u"-e"_s, u"mp4mux"_s, u"name=mux"_s, u"fragment-duration=1000"_s, u"!"_s, u"filesink"_s, u"location=/tmp/rec.mp4"_s};
}

QStringList expectedHevcVideoToMux()
{
    return {
        u"compositor"_s,
        u"name=comp"_s,
        u"background=black"_s,
        u"!"_s,
        u"videoconvert"_s,
        u"!"_s,
        u"videorate"_s,
        u"!"_s,
        u"video/x-raw,framerate=30/1"_s,
        u"!"_s,
        u"vah265enc"_s,
        u"bitrate=10000"_s,
        u"!"_s,
        u"h265parse"_s,
        u"!"_s,
        u"mux."_s,
    };
}

QStringList expectedAv1VideoNoAudio()
{
    return {u"-e"_s,
            u"compositor"_s,
            u"name=comp"_s,
            u"background=black"_s,
            u"!"_s,
            u"videoconvert"_s,
            u"!"_s,
            u"videorate"_s,
            u"!"_s,
            u"video/x-raw,framerate=30/1"_s,
            u"!"_s,
            u"vaav1enc"_s,
            u"bitrate=10000"_s,
            u"!"_s,
            u"av1parse"_s,
            u"!"_s,
            u"mp4mux"_s,
            u"fragment-duration=1000"_s,
            u"!"_s,
            u"filesink"_s,
            u"location=/tmp/rec.mp4"_s,
            u"pipewiresrc"_s,
            u"path=49"_s,
            u"do-timestamp=true"_s,
            u"!"_s,
            u"videoconvert"_s,
            u"!"_s,
            u"comp.sink_0"_s};
}

QStringList expectedAv1MuxHead()
{
    return {u"-e"_s, u"mp4mux"_s, u"name=mux"_s, u"fragment-duration=1000"_s, u"!"_s, u"filesink"_s, u"location=/tmp/rec.mp4"_s};
}

QStringList expectedAv1VideoToMux()
{
    return {
        u"compositor"_s,
        u"name=comp"_s,
        u"background=black"_s,
        u"!"_s,
        u"videoconvert"_s,
        u"!"_s,
        u"videorate"_s,
        u"!"_s,
        u"video/x-raw,framerate=30/1"_s,
        u"!"_s,
        u"vaav1enc"_s,
        u"bitrate=10000"_s,
        u"!"_s,
        u"av1parse"_s,
        u"!"_s,
        u"mux."_s,
    };
}

QStringList expectedPipeWireSource()
{
    return {u"pipewiresrc"_s, u"path=49"_s, u"do-timestamp=true"_s, u"!"_s, u"videoconvert"_s, u"!"_s, u"comp.sink_0"_s};
}

QStringList expectedPulseToMux(const QString &device, const QString &audioEncoder)
{
    QStringList expected{u"pulsesrc"_s};
    if (!device.isEmpty()) {
        expected << QStringLiteral("device=%1").arg(device);
    }
    expected << u"!"_s << u"audioconvert"_s << u"!"_s << u"audioresample"_s << u"!"_s << audioEncoder << u"!"_s << u"mux."_s;
    return expected;
}

QStringList expectedPulseToMixer(const QString &device)
{
    QStringList expected{u"pulsesrc"_s};
    if (!device.isEmpty()) {
        expected << QStringLiteral("device=%1").arg(device);
    }
    expected << u"!"_s << u"audioconvert"_s << u"!"_s << u"amix."_s;
    return expected;
}
}

void WfRecorderArgsTest::testSupportedRecordingModes()
{
    VideoPlatformNiri platform;
    const auto modes = platform.supportedRecordingModes();
    QVERIFY(modes.testFlag(VideoPlatform::Screen));
    QVERIFY(modes.testFlag(VideoPlatform::Region));

    // Window needs niri's Mutter ScreenCast plus the GStreamer consumer used by
    // both Screen and Window, mirroring the production capability probe.
    const bool hasGstLaunch = !QStandardPaths::findExecutable(u"gst-launch-1.0"_s).isEmpty();
    const bool hasGstInspect = !QStandardPaths::findExecutable(u"gst-inspect-1.0"_s).isEmpty();
    const auto hasElement = [](const QString &element) {
        return QProcess::execute(u"gst-inspect-1.0"_s, {u"--exists"_s, element}) == 0;
    };
    const bool hasGstPlugins = hasGstInspect && hasElement(u"pipewiresrc"_s) && hasElement(u"videoconvert"_s) && hasElement(u"videorate"_s)
        && hasElement(u"vp9enc"_s) && hasElement(u"webmmux"_s);
    QCOMPARE(modes.testFlag(VideoPlatform::Window), hasGstLaunch && hasGstPlugins);
}

void WfRecorderArgsTest::testGstVp9Arguments()
{
    const auto args = VideoPlatformNiri::gstArguments(VideoPlatform::WebM_VP9, 49, u"/tmp/rec.webm"_s, fullGstCapabilities());
    QCOMPARE(args, expectedVp9VideoNoAudio());
}

void WfRecorderArgsTest::testGstVp9AudioArguments()
{
    const auto capabilities = fullGstCapabilities();

    auto expectedSystem = expectedVp9MuxHead();
    expectedSystem << expectedVp9VideoToMux() << expectedPulseToMux(u"@DEFAULT_MONITOR@"_s, u"opusenc"_s);
    expectedSystem << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::WebM_VP9, 49, u"/tmp/rec.webm"_s, capabilities, VideoPlatformNiri::AudioMode::System),
             expectedSystem);

    auto expectedMicrophone = expectedVp9MuxHead();
    expectedMicrophone << expectedVp9VideoToMux() << expectedPulseToMux(QString(), u"opusenc"_s);
    expectedMicrophone << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::WebM_VP9, 49, u"/tmp/rec.webm"_s, capabilities, VideoPlatformNiri::AudioMode::Microphone),
             expectedMicrophone);

    auto expectedBoth = expectedVp9MuxHead();
    expectedBoth << expectedVp9VideoToMux()
                 << QStringList(
                        {u"audiomixer"_s, u"name=amix"_s, u"!"_s, u"audioconvert"_s, u"!"_s, u"audioresample"_s, u"!"_s, u"opusenc"_s, u"!"_s, u"mux."_s})
                 << expectedPulseToMixer(u"@DEFAULT_MONITOR@"_s) << expectedPulseToMixer(QString());
    expectedBoth << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::WebM_VP9, 49, u"/tmp/rec.webm"_s, capabilities, VideoPlatformNiri::AudioMode::Both), expectedBoth);
}

void WfRecorderArgsTest::testGstH264Arguments()
{
    const auto capabilities = fullGstCapabilities();

    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities), expectedH264VideoNoAudio());

    auto expectedSystem = expectedH264MuxHead();
    expectedSystem << expectedH264VideoToMux() << expectedPulseToMux(u"@DEFAULT_MONITOR@"_s, u"avenc_aac"_s);
    expectedSystem << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::System),
             expectedSystem);

    auto expectedMicrophone = expectedH264MuxHead();
    expectedMicrophone << expectedH264VideoToMux() << expectedPulseToMux(QString(), u"avenc_aac"_s);
    expectedMicrophone << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::Microphone),
             expectedMicrophone);

    auto expectedBoth = expectedH264MuxHead();
    expectedBoth << expectedH264VideoToMux()
                 << QStringList(
                        {u"audiomixer"_s, u"name=amix"_s, u"!"_s, u"audioconvert"_s, u"!"_s, u"audioresample"_s, u"!"_s, u"avenc_aac"_s, u"!"_s, u"mux."_s})
                 << expectedPulseToMixer(u"@DEFAULT_MONITOR@"_s) << expectedPulseToMixer(QString());
    expectedBoth << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::Both), expectedBoth);

    // Explicit microphone device is forwarded to pulsesrc.
    auto expectedMicDevice = expectedH264MuxHead();
    expectedMicDevice << expectedH264VideoToMux() << expectedPulseToMux(u"alsa_input.usb-mic"_s, u"avenc_aac"_s);
    expectedMicDevice << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264,
                                             49,
                                             u"/tmp/rec.mp4"_s,
                                             capabilities,
                                             VideoPlatformNiri::AudioMode::Microphone,
                                             u"alsa_input.usb-mic"_s),
             expectedMicDevice);
}

void WfRecorderArgsTest::testGstH264PrefersVaApi()
{
    auto capabilities = fullGstCapabilities();
    capabilities.vah264enc = true;
    const auto args = VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities);
    QCOMPARE(args,
             QStringList({u"-e"_s,
                          u"compositor"_s,
                          u"name=comp"_s,
                          u"background=black"_s,
                          u"!"_s,
                          u"videoconvert"_s,
                          u"!"_s,
                          u"videorate"_s,
                          u"!"_s,
                          u"video/x-raw,framerate=30/1"_s,
                          u"!"_s,
                          u"vah264enc"_s,
                          u"bitrate=10000"_s,
                          u"!"_s,
                          u"h264parse"_s,
                          u"!"_s,
                          u"mp4mux"_s,
                          u"fragment-duration=1000"_s,
                          u"!"_s,
                          u"filesink"_s,
                          u"location=/tmp/rec.mp4"_s,
                          u"pipewiresrc"_s,
                          u"path=49"_s,
                          u"do-timestamp=true"_s,
                          u"!"_s,
                          u"videoconvert"_s,
                          u"!"_s,
                          u"comp.sink_0"_s}));
}

void WfRecorderArgsTest::testGstHevcArguments()
{
    const auto capabilities = fullGstCapabilities();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities), expectedHevcVideoNoAudio());
}

void WfRecorderArgsTest::testGstHevcAudioArguments()
{
    const auto capabilities = fullGstCapabilities();

    auto expectedSystem = expectedHevcMuxHead();
    expectedSystem << expectedHevcVideoToMux() << expectedPulseToMux(u"@DEFAULT_MONITOR@"_s, u"avenc_aac"_s);
    expectedSystem << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::System),
             expectedSystem);

    auto expectedMicrophone = expectedHevcMuxHead();
    expectedMicrophone << expectedHevcVideoToMux() << expectedPulseToMux(QString(), u"avenc_aac"_s);
    expectedMicrophone << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::Microphone),
             expectedMicrophone);

    auto expectedBoth = expectedHevcMuxHead();
    expectedBoth << expectedHevcVideoToMux()
                 << QStringList(
                        {u"audiomixer"_s, u"name=amix"_s, u"!"_s, u"audioconvert"_s, u"!"_s, u"audioresample"_s, u"!"_s, u"avenc_aac"_s, u"!"_s, u"mux."_s})
                 << expectedPulseToMixer(u"@DEFAULT_MONITOR@"_s) << expectedPulseToMixer(QString());
    expectedBoth << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::Both), expectedBoth);
}

void WfRecorderArgsTest::testGstAv1Arguments()
{
    const auto capabilities = fullGstCapabilities();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_AV1, 49, u"/tmp/rec.mp4"_s, capabilities), expectedAv1VideoNoAudio());
}

void WfRecorderArgsTest::testGstAv1AudioArguments()
{
    const auto capabilities = fullGstCapabilities();

    auto expectedSystem = expectedAv1MuxHead();
    expectedSystem << expectedAv1VideoToMux() << expectedPulseToMux(u"@DEFAULT_MONITOR@"_s, u"avenc_aac"_s);
    expectedSystem << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_AV1, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::System),
             expectedSystem);

    auto expectedMicrophone = expectedAv1MuxHead();
    expectedMicrophone << expectedAv1VideoToMux() << expectedPulseToMux(QString(), u"avenc_aac"_s);
    expectedMicrophone << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_AV1, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::Microphone),
             expectedMicrophone);

    auto expectedBoth = expectedAv1MuxHead();
    expectedBoth << expectedAv1VideoToMux()
                 << QStringList(
                        {u"audiomixer"_s, u"name=amix"_s, u"!"_s, u"audioconvert"_s, u"!"_s, u"audioresample"_s, u"!"_s, u"avenc_aac"_s, u"!"_s, u"mux."_s})
                 << expectedPulseToMixer(u"@DEFAULT_MONITOR@"_s) << expectedPulseToMixer(QString());
    expectedBoth << expectedPipeWireSource();
    QCOMPARE(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_AV1, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::Both), expectedBoth);
}

void WfRecorderArgsTest::testGstCapabilityGates()
{
    auto capabilities = fullGstCapabilities();
    capabilities.x264enc = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.mp4mux = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.opusenc = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::WebM_VP9, 49, u"/tmp/rec.webm"_s, capabilities, VideoPlatformNiri::AudioMode::System).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.avencAac = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::Microphone).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.pulsesrc = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::WebM_VP9, 49, u"/tmp/rec.webm"_s, capabilities, VideoPlatformNiri::AudioMode::System).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.audiomixer = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::WebM_VP9, 49, u"/tmp/rec.webm"_s, capabilities, VideoPlatformNiri::AudioMode::Both).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.mp4mux = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_AV1, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.avencAac = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::System).isEmpty());
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_AV1, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::Microphone).isEmpty());

    // H.264 still works through vah264enc+h264parse when x264enc is missing.
    capabilities = fullGstCapabilities();
    capabilities.x264enc = false;
    capabilities.vah264enc = true;
    QVERIFY(!VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());

    // vah264enc without h264parse cannot feed mp4mux; fall back to x264enc.
    capabilities = fullGstCapabilities();
    capabilities.h264parse = false;
    capabilities.vah264enc = true;
    const auto x264Fallback = VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities);
    QVERIFY(x264Fallback.contains(u"x264enc"_s));
    QVERIFY(!x264Fallback.contains(u"vah264enc"_s));

    capabilities.x264enc = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.vah265enc = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.h265parse = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.vaav1enc = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_AV1, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());

    capabilities = fullGstCapabilities();
    capabilities.av1parse = false;
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::MP4_AV1, 49, u"/tmp/rec.mp4"_s, capabilities).isEmpty());
}

void WfRecorderArgsTest::testGstRejectsUnsupportedFormats()
{
    const auto capabilities = fullGstCapabilities();
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::NoFormat, 49, u"/tmp/rec"_s, capabilities).isEmpty());
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::Gif, 49, u"/tmp/rec.gif"_s, capabilities).isEmpty());
    QVERIFY(VideoPlatformNiri::gstArguments(VideoPlatform::WebP, 49, u"/tmp/rec.webp"_s, capabilities).isEmpty());
}

void WfRecorderArgsTest::testWfRecorderHevcAv1Arguments()
{
    QCOMPARE(VideoPlatformNiri::argumentsFor(VideoPlatform::Region, u"/tmp/out.mp4"_s, QRect(0, 0, 10, 10), VideoPlatform::MP4_HEVC, false, QString()),
             QStringList({u"-f"_s, u"/tmp/out.mp4"_s, u"-g"_s, u"0,0 10x10"_s, u"-c"_s, u"libx265"_s, u"-p"_s, u"b=10000000"_s}));
    QCOMPARE(VideoPlatformNiri::argumentsFor(VideoPlatform::Region, u"/tmp/out.mp4"_s, QRect(0, 0, 10, 10), VideoPlatform::MP4_AV1, false, QString()),
             QStringList({u"-f"_s, u"/tmp/out.mp4"_s, u"-g"_s, u"0,0 10x10"_s, u"-c"_s, u"libaom-av1"_s, u"-p"_s, u"b=10000000"_s, u"-p"_s, u"cpu-used=8"_s}));
}

void WfRecorderArgsTest::testEffectiveFormatContainerPreference()
{
    const auto supported = VideoPlatform::WebM_VP9 | VideoPlatform::MP4_H264 | VideoPlatform::MP4_HEVC | VideoPlatform::MP4_AV1;

    // An explicit .mp4 path prefers the preferred MP4 codec, because the
    // extension alone cannot distinguish H.264, H.265 and AV1.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mp4"_s), VideoPlatform::MP4_HEVC, supported), VideoPlatform::MP4_HEVC);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mp4"_s), VideoPlatform::MP4_AV1, supported), VideoPlatform::MP4_AV1);
    // Unknown explicit extensions must not silently receive an MP4 stream.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mkv"_s), VideoPlatform::MP4_H264, supported), VideoPlatform::NoFormat);
    // An implicit temp path has no extension and keeps the preference.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/nekoshot-XXXX/Screencast_1"_s), VideoPlatform::MP4_H264, supported),
             VideoPlatform::MP4_H264);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mp4"_s), VideoPlatform::MP4_H264, supported), VideoPlatform::MP4_H264);

    // A different container falls back to the extension.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.webm"_s), VideoPlatform::MP4_HEVC, supported), VideoPlatform::WebM_VP9);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mp4"_s), VideoPlatform::WebM_VP9, supported), VideoPlatform::MP4_H264);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.webm"_s), VideoPlatform::MP4_AV1, supported), VideoPlatform::WebM_VP9);

    // No explicit path: use the preference; no preference: the extension default.
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl(), VideoPlatform::MP4_HEVC, supported), VideoPlatform::MP4_HEVC);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mp4"_s), VideoPlatform::NoFormat, supported), VideoPlatform::MP4_H264);

    // Unsupported resolutions still fail.
    const auto h264Only = VideoPlatform::Formats(VideoPlatform::MP4_H264);
    QCOMPARE(VideoPlatformNiri::effectiveFormat(QUrl::fromLocalFile(u"/tmp/out.mp4"_s), VideoPlatform::MP4_HEVC, h264Only), VideoPlatform::NoFormat);
}

void WfRecorderArgsTest::testVaProfileParsing()
{
    const QString full = QStringLiteral(
        "      VAProfileH264Main               :\tVAEntrypointVLD\n"
        "      VAProfileH264High               :\tVAEntrypointEncSlice\n"
        "      VAProfileHEVCMain               :\tVAEntrypointEncSlice\n"
        "      VAProfileAV1Profile0            :\tVAEntrypointEncSlice\n");
    const auto profiles = VideoPlatformNiri::parseVaProfiles(full);
    QVERIFY(profiles.h264);
    QVERIFY(profiles.hevc);
    QVERIFY(profiles.av1);

    // Main10 must not be accepted as HEVC Main; missing profiles stay false.
    const auto main10Only =
        VideoPlatformNiri::parseVaProfiles(QStringLiteral("      VAProfileH264ConstrainedBaseline :\tVAEntrypointEncSlice\n"
                                                          "      VAProfileHEVCMain10             :\tVAEntrypointEncSlice\n"));
    QVERIFY(!main10Only.h264);
    QVERIFY(!main10Only.hevc);
    QVERIFY(!main10Only.av1);
    QVERIFY(!VideoPlatformNiri::parseVaProfiles(QString()).h264);
}

void WfRecorderArgsTest::testWfRecorderCodecAvailability()
{
    QCOMPARE(VideoPlatformNiri::wfRecorderCodec(VideoPlatform::MP4_HEVC), u"libx265"_s);
    QCOMPARE(VideoPlatformNiri::wfRecorderCodec(VideoPlatform::MP4_AV1), u"libaom-av1"_s);
    QVERIFY(VideoPlatformNiri::wfRecorderCodec(VideoPlatform::Gif).isEmpty());

    QVERIFY(VideoPlatformNiri::wfRecorderCodecAvailable(VideoPlatform::MP4_AV1, true, true, true, true));
    QVERIFY(!VideoPlatformNiri::wfRecorderCodecAvailable(VideoPlatform::MP4_AV1, true, true, true, false));
    QVERIFY(!VideoPlatformNiri::wfRecorderCodecAvailable(VideoPlatform::MP4_HEVC, true, true, false, true));
    QVERIFY(!VideoPlatformNiri::wfRecorderCodecAvailable(VideoPlatform::NoFormat, true, true, true, true));
    QVERIFY(VideoPlatformNiri::wfRecorderCodecAvailable(VideoPlatform::WebM_VP9, true, false, false, false));
}

void WfRecorderArgsTest::testScreencastStartGuard()
{
    // A start is only allowed when no screencast start is pending, no session
    // is alive and no encoder process is running.
    QVERIFY(VideoPlatformNiri::screencastStartAllowed(false, QString(), false));
    QVERIFY(!VideoPlatformNiri::screencastStartAllowed(true, QString(), false));
    QVERIFY(!VideoPlatformNiri::screencastStartAllowed(false, u"/org/gnome/Mutter/ScreenCast/Session/u1"_s, false));
    QVERIFY(!VideoPlatformNiri::screencastStartAllowed(false, QString(), true));
}

void WfRecorderArgsTest::testScreencastPendingGeneration()
{
    // The 3 s stream timeout and any delayed callback only act on the current
    // pending attempt; cancel/restart increments the generation.
    QVERIFY(VideoPlatformNiri::screencastPendingGenerationIsCurrent(true, 3, 3));
    QVERIFY(!VideoPlatformNiri::screencastPendingGenerationIsCurrent(false, 3, 3));
    QVERIFY(!VideoPlatformNiri::screencastPendingGenerationIsCurrent(true, 3, 4));
}

void WfRecorderArgsTest::testBitratePresets()
{
    QCOMPARE(VideoPlatformNiri::bitrateKbpsForQuality(0), 4000);
    QCOMPARE(VideoPlatformNiri::bitrateKbpsForQuality(1), 10000);
    QCOMPARE(VideoPlatformNiri::bitrateKbpsForQuality(2), 18000);
    QCOMPARE(VideoPlatformNiri::bitrateKbpsForQuality(99), 10000);

    // The preset reaches every encoder form: VA/software MP4 use kbit/s while
    // VP9 uses bit/s.
    const auto capabilities = fullGstCapabilities();
    const auto high =
        VideoPlatformNiri::gstArguments(VideoPlatform::MP4_HEVC, 49, u"/tmp/rec.mp4"_s, capabilities, VideoPlatformNiri::AudioMode::None, {}, 18000);
    QVERIFY(high.contains(u"bitrate=18000"_s));
    const auto vp9 =
        VideoPlatformNiri::gstArguments(VideoPlatform::WebM_VP9, 49, u"/tmp/rec.webm"_s, capabilities, VideoPlatformNiri::AudioMode::None, {}, 4000);
    QVERIFY(vp9.contains(u"target-bitrate=4000000"_s));

    const auto region =
        VideoPlatformNiri::argumentsFor(VideoPlatform::Region, u"/tmp/out.mp4"_s, QRect(0, 0, 10, 10), VideoPlatform::MP4_H264, false, {}, 18000);
    QVERIFY(region.contains(u"b=18000000"_s));
}

void WfRecorderArgsTest::testRegionCaptureMargins()
{
    NiriIpc::Output edp;
    edp.name = u"eDP-1"_s;
    edp.logicalRect = QRectF(0, 0, 1645, 1028);
    edp.physicalSize = QSize(2880, 1800);
    edp.scale = 1.75;
    NiriIpc::Output dp;
    dp.name = u"DP-1"_s;
    dp.logicalRect = QRectF(-1920, 0, 1920, 1080);
    dp.scale = 1.0;
    const QList<NiriIpc::Output> outputs{edp, dp};

    const auto primary = VideoPlatformNiri::regionCaptureFor(QRect(100, 100, 400, 300), outputs);
    QVERIFY(primary.has_value());
    QCOMPARE(primary->output, u"eDP-1"_s);
    QCOMPARE(primary->streamSize, QSize(2880, 1800));
    QCOMPARE(primary->margins, QMargins(175, 175, 2005, 1100));

    // Negative-origin output: the region is translated into the output's own
    // pixel space, not kept in global coordinates.
    const auto left = VideoPlatformNiri::regionCaptureFor(QRect(-1000, 100, 400, 300), outputs);
    QVERIFY(left.has_value());
    QCOMPARE(left->output, u"DP-1"_s);
    QCOMPARE(left->margins, QMargins(920, 100, 600, 680));

    // A region spanning two outputs is left to the wf-recorder fallback.
    QVERIFY(!VideoPlatformNiri::regionCaptureFor(QRect(-100, 100, 400, 300), outputs).has_value());
    QVERIFY(!VideoPlatformNiri::regionCaptureFor(QRect(), outputs).has_value());

    // Full-output selection must not lose the last physical column/row: niri's
    // logical 1645 * 1.75 rounds to 2879 while the stream is 2880 pixels.
    const auto full = VideoPlatformNiri::regionCaptureFor(QRect(0, 0, 1645, 1028), outputs);
    QVERIFY(full.has_value());
    QCOMPARE(full->streamSize, QSize(2880, 1800));
    QCOMPARE(full->margins, QMargins(0, 0, 0, 0));
}

void WfRecorderArgsTest::testRegionCropArguments()
{
    const auto capabilities = fullGstCapabilities();
    const auto args = VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264,
                                                      49,
                                                      u"/tmp/rec.mp4"_s,
                                                      capabilities,
                                                      VideoPlatformNiri::AudioMode::None,
                                                      {},
                                                      10000,
                                                      QMargins(175, 175, 2005, 1100));
    const int index = args.indexOf(u"videocrop"_s);
    QVERIFY(index > 0);
    QCOMPARE(args.mid(index + 1, 4), QStringList({u"left=175"_s, u"top=175"_s, u"right=2005"_s, u"bottom=1100"_s}));

    const auto uncropped = VideoPlatformNiri::gstArguments(VideoPlatform::MP4_H264, 49, u"/tmp/rec.mp4"_s, capabilities);
    QVERIFY(!uncropped.contains(u"videocrop"_s));
}

void WfRecorderArgsTest::testWindowFallbackFormat()
{
    using Formats = VideoPlatform::Formats;
    const Formats gst = VideoPlatform::MP4_H264 | VideoPlatform::WebM_VP9;

    QCOMPARE(VideoPlatformNiri::windowFallbackFormat(QUrl::fromLocalFile(u"/tmp/a.mp4"_s), gst), VideoPlatform::MP4_H264);
    QCOMPARE(VideoPlatformNiri::windowFallbackFormat(QUrl::fromLocalFile(u"/tmp/a.webm"_s), gst), VideoPlatform::WebM_VP9);
    // .webm cannot be satisfied by an MP4-only gst set.
    QCOMPARE(VideoPlatformNiri::windowFallbackFormat(QUrl::fromLocalFile(u"/tmp/a.webm"_s), VideoPlatform::MP4_H264), VideoPlatform::NoFormat);
    // Compatibility order: H.264, then H.265, then AV1.
    QCOMPARE(VideoPlatformNiri::windowFallbackFormat(QUrl::fromLocalFile(u"/tmp/a.mp4"_s), VideoPlatform::MP4_AV1 | VideoPlatform::MP4_HEVC),
             VideoPlatform::MP4_HEVC);
    QCOMPARE(VideoPlatformNiri::windowFallbackFormat(QUrl::fromLocalFile(u"/tmp/a.mp4"_s), VideoPlatform::MP4_AV1), VideoPlatform::MP4_AV1);
    QCOMPARE(VideoPlatformNiri::windowFallbackFormat(QUrl::fromLocalFile(u"/tmp/a.mp4"_s), VideoPlatform::NoFormat), VideoPlatform::NoFormat);
    QCOMPARE(VideoPlatformNiri::windowFallbackFormat(QUrl::fromLocalFile(u"/tmp/a.mkv"_s), gst), VideoPlatform::NoFormat);
    // Implicit output (no path) may choose an MP4 fallback.
    QCOMPARE(VideoPlatformNiri::windowFallbackFormat(QUrl(), gst), VideoPlatform::MP4_H264);
}

QTEST_GUILESS_MAIN(WfRecorderArgsTest)

#include "WfRecorderArgsTest.moc"

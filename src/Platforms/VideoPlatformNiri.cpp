/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "VideoPlatformNiri.h"

#include "ExportManager.h"
#include "NiriIpc.h"
#include "settings.h"
#include "spectacle_debug.h"

#include <KLocalizedString>
#include <KNotification>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>

#include <csignal>
#include <optional>
#include <unistd.h>

using namespace Qt::StringLiterals;

using Format = VideoPlatform::Format;
using Formats = VideoPlatform::Formats;
using AudioMode = VideoPlatformNiri::AudioMode;
using GstCapabilities = VideoPlatformNiri::GstCapabilities;

namespace
{
constexpr int s_encoderProbeTimeoutMsec = 3000;
constexpr int s_startTimeoutMsec = 3000;
constexpr int s_stopTimeoutMsec = 10000;
constexpr int s_killTimeoutMsec = 2000;
constexpr int s_shutdownTimeoutMsec = 120000;
// Finalizing a long MP4 rewrites the index at EOS; measured ~6 s for a 20 s
// clip, so the watchdog must not scale with the old 10 s blocking timeout.
constexpr int s_stopWatchdogMsec = 120000;
constexpr qsizetype s_maxStderrBytes = 64 * 1024;
constexpr qsizetype s_maxErrorMessageChars = 2000;
constexpr int s_dbusTimeoutMsec = 1500;
constexpr int s_dbusStopTimeoutMsec = 1500;
constexpr int s_windowStartTimeoutMsec = 3000;
constexpr int s_windowPickTimeoutMsec = 10 * 60 * 1000;

const QString s_screencastService = QStringLiteral("org.gnome.Mutter.ScreenCast");
const QString s_screencastPath = QStringLiteral("/org/gnome/Mutter/ScreenCast");
const QString s_screencastInterface = QStringLiteral("org.gnome.Mutter.ScreenCast");
const QString s_sessionInterface = QStringLiteral("org.gnome.Mutter.ScreenCast.Session");
const QString s_streamInterface = QStringLiteral("org.gnome.Mutter.ScreenCast.Stream");

struct EncoderAvailability {
    bool vp9 = true;
    bool h264 = true;
    bool hevc = true;
    bool av1 = true;
};

// wf-recorder links against libav* directly, so the ffmpeg command line tool is
// only used to narrow down the advertised formats. When it is missing keep
// advertising both formats instead of pretending that nothing is supported.
EncoderAvailability probeFfmpegEncoders()
{
    EncoderAvailability available;
    const QString ffmpeg = QStandardPaths::findExecutable(u"ffmpeg"_s);
    if (ffmpeg.isEmpty()) {
        return available;
    }

    QProcess process;
    process.start(ffmpeg, {u"-hide_banner"_s, u"-encoders"_s});
    if (!process.waitForFinished(s_encoderProbeTimeoutMsec)) {
        process.kill();
        process.waitForFinished(s_killTimeoutMsec);
        return available;
    }
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    available.vp9 = output.contains(u"libvpx-vp9"_s);
    available.h264 = output.contains(u"libx264"_s);
    available.hevc = output.contains(u"libx265"_s);
    available.av1 = output.contains(u"libaom-av1"_s);
    return available;
}

const EncoderAvailability &encoderAvailability()
{
    static const EncoderAvailability available = probeFfmpegEncoders();
    return available;
}

// niri's RecordWindow is only usable through a GStreamer pipeline.
const QString &gstLaunchPath()
{
    static const QString path = QStandardPaths::findExecutable(u"gst-launch-1.0"_s);
    return path;
}

const QString &gstInspectPath()
{
    static const QString path = QStandardPaths::findExecutable(u"gst-inspect-1.0"_s);
    return path;
}

bool probeGstElement(const QString &element)
{
    const QString inspect = gstInspectPath();
    if (inspect.isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(inspect, {u"--exists"_s, element});
    if (!process.waitForFinished(s_encoderProbeTimeoutMsec)) {
        process.kill();
        process.waitForFinished(s_killTimeoutMsec);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

constexpr int s_vaProbeTimeoutMsec = 1500;

VideoPlatformNiri::VaProfiles probeVaProfiles()
{
    const QString vainfo = QStandardPaths::findExecutable(u"vainfo"_s);
    if (vainfo.isEmpty()) {
        return {};
    }

    QProcess process;
    process.start(vainfo, {});
    if (!process.waitForStarted(s_vaProbeTimeoutMsec)) {
        return {};
    }
    if (!process.waitForFinished(s_vaProbeTimeoutMsec)) {
        process.kill();
        process.waitForFinished(s_killTimeoutMsec);
        return {};
    }
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput() + process.readAllStandardError());
    return VideoPlatformNiri::parseVaProfiles(output);
}

const VideoPlatformNiri::VaProfiles &vaProfiles()
{
    static const VideoPlatformNiri::VaProfiles profiles = probeVaProfiles();
    return profiles;
}

struct GstScreencastSupport {
    bool gstLaunch = false;
    bool gstInspect = false;
    bool pipewiresrc = false;
    bool videoconvert = false;
    bool videorate = false;
    bool vp9enc = false;
    bool webmmux = false;

    bool complete() const
    {
        return gstLaunch && gstInspect && pipewiresrc && videoconvert && videorate && vp9enc && webmmux;
    }
};

const GstScreencastSupport &gstScreencastSupport()
{
    static const GstScreencastSupport support = [] {
        GstScreencastSupport result;
        result.gstLaunch = !gstLaunchPath().isEmpty();
        result.gstInspect = !gstInspectPath().isEmpty();
        if (result.gstInspect) {
            result.pipewiresrc = probeGstElement(u"pipewiresrc"_s);
            result.videoconvert = probeGstElement(u"videoconvert"_s);
            result.videorate = probeGstElement(u"videorate"_s);
            result.vp9enc = probeGstElement(u"vp9enc"_s);
            result.webmmux = probeGstElement(u"webmmux"_s);
        }
        return result;
    }();
    return support;
}

bool gstScreencastAvailable()
{
    return gstScreencastSupport().complete();
}

QString gstScreencastSupportError()
{
    const auto &support = gstScreencastSupport();
    if (!support.gstLaunch) {
        return i18n("gst-launch-1.0 is not installed");
    }
    if (!support.gstInspect) {
        return i18n("gst-inspect-1.0 is not installed");
    }
    QStringList missing;
    if (!support.pipewiresrc) {
        missing << u"pipewiresrc"_s;
    }
    if (!support.videoconvert) {
        missing << u"videoconvert"_s;
    }
    if (!support.videorate) {
        missing << u"videorate"_s;
    }
    if (!support.vp9enc) {
        missing << u"vp9enc"_s;
    }
    if (!support.webmmux) {
        missing << u"webmmux"_s;
    }
    return i18n("missing GStreamer elements: %1", missing.join(u", "_s));
}

bool probeGstX264()
{
    return probeGstElement(u"x264enc"_s);
}

bool gstX264Available()
{
    static const bool available = probeGstX264();
    return available;
}

bool gstMp4MuxAvailable()
{
    static const bool available = probeGstElement(u"mp4mux"_s);
    return available;
}

bool gstVah264Available()
{
    // Element presence is not enough: the VA driver must expose an H.264 profile.
    static const bool available = probeGstElement(u"vah264enc"_s) && vaProfiles().h264;
    return available;
}

bool gstH264ParseAvailable()
{
    static const bool available = probeGstElement(u"h264parse"_s);
    return available;
}

bool gstVah265Available()
{
    static const bool available = probeGstElement(u"vah265enc"_s) && vaProfiles().hevc;
    return available;
}

bool gstH265ParseAvailable()
{
    static const bool available = probeGstElement(u"h265parse"_s);
    return available;
}

bool gstVaav1Available()
{
    static const bool available = probeGstElement(u"vaav1enc"_s) && vaProfiles().av1;
    return available;
}

bool gstAv1ParseAvailable()
{
    static const bool available = probeGstElement(u"av1parse"_s);
    return available;
}

bool isMp4Format(Format format)
{
    return format == VideoPlatform::MP4_H264 || format == VideoPlatform::MP4_HEVC || format == VideoPlatform::MP4_AV1;
}

QString gstVideoFormatSupportError(Format format)
{
    switch (format) {
    case VideoPlatform::WebM_VP9:
        return {};
    case VideoPlatform::MP4_H264: {
        if (!gstMp4MuxAvailable()) {
            return i18n("missing GStreamer elements: mp4mux");
        }
        const bool hardware = gstVah264Available() && gstH264ParseAvailable();
        if (!hardware && !gstX264Available()) {
            return i18n("missing GStreamer elements: vah264enc+h264parse or x264enc");
        }
        return {};
    }
    case VideoPlatform::MP4_HEVC:
        if (!gstVah265Available() || !gstH265ParseAvailable() || !gstMp4MuxAvailable()) {
            return i18n("H.265/HEVC requires vah265enc, h265parse and mp4mux");
        }
        return {};
    case VideoPlatform::MP4_AV1:
        if (!gstVaav1Available() || !gstAv1ParseAvailable() || !gstMp4MuxAvailable()) {
            return i18n("AV1 requires vaav1enc, av1parse and mp4mux");
        }
        return {};
    default:
        return i18n("the video format is not supported");
    }
}

bool gstOpusencAvailable()
{
    static const bool available = probeGstElement(u"opusenc"_s);
    return available;
}

bool gstAvencAacAvailable()
{
    static const bool available = probeGstElement(u"avenc_aac"_s);
    return available;
}

bool gstPulsesrcAvailable()
{
    static const bool available = probeGstElement(u"pulsesrc"_s);
    return available;
}

bool gstAudiomixerAvailable()
{
    static const bool available = probeGstElement(u"audiomixer"_s);
    return available;
}

QString gstAudioSupportError(Format format, AudioMode audio)
{
    if (audio == AudioMode::None) {
        return {};
    }

    QStringList missing;
    if (!gstPulsesrcAvailable()) {
        missing << u"pulsesrc"_s;
    }
    if (format == VideoPlatform::WebM_VP9 && !gstOpusencAvailable()) {
        missing << u"opusenc"_s;
    }
    if (isMp4Format(format) && !gstAvencAacAvailable()) {
        missing << u"avenc_aac"_s;
    }
    if (audio == AudioMode::Both && !gstAudiomixerAvailable()) {
        missing << u"audiomixer"_s;
    }
    if (missing.isEmpty()) {
        return {};
    }
    return i18n("missing GStreamer audio elements: %1", missing.join(u", "_s));
}

AudioMode audioModeFromSettings()
{
    const bool systemAudio = Settings::videoRecordSystemAudio();
    const bool microphone = Settings::videoRecordMicrophone();
    if (systemAudio && microphone) {
        return AudioMode::Both;
    }
    if (systemAudio) {
        return AudioMode::System;
    }
    if (microphone) {
        return AudioMode::Microphone;
    }
    return AudioMode::None;
}

GstCapabilities gstCapabilitiesFor(Format format, AudioMode audio)
{
    GstCapabilities capabilities;
    const auto &support = gstScreencastSupport();
    capabilities.vp9enc = support.vp9enc;
    capabilities.webmmux = support.webmmux;
    switch (format) {
    case VideoPlatform::MP4_H264:
        capabilities.vah264enc = gstVah264Available();
        capabilities.h264parse = gstH264ParseAvailable();
        capabilities.x264enc = gstX264Available();
        capabilities.mp4mux = gstMp4MuxAvailable();
        break;
    case VideoPlatform::MP4_HEVC:
        capabilities.vah265enc = gstVah265Available();
        capabilities.h265parse = gstH265ParseAvailable();
        capabilities.mp4mux = gstMp4MuxAvailable();
        break;
    case VideoPlatform::MP4_AV1:
        capabilities.vaav1enc = gstVaav1Available();
        capabilities.av1parse = gstAv1ParseAvailable();
        capabilities.mp4mux = gstMp4MuxAvailable();
        break;
    default:
        break;
    }
    if (audio != AudioMode::None) {
        capabilities.pulsesrc = gstPulsesrcAvailable();
        if (format == VideoPlatform::WebM_VP9) {
            capabilities.opusenc = gstOpusencAvailable();
        } else if (isMp4Format(format)) {
            capabilities.avencAac = gstAvencAacAvailable();
        }
        if (audio == AudioMode::Both) {
            capabilities.audiomixer = gstAudiomixerAvailable();
        }
    }
    return capabilities;
}

QString objectPathFrom(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>()) {
        return value.value<QDBusObjectPath>().path();
    }
    return value.toString();
}

std::optional<QDBusMessage> callScreencast(const QString &path,
                                           const QString &interface,
                                           const QString &method,
                                           const QVariantList &arguments,
                                           QString *error,
                                           int timeoutMsec = s_dbusTimeoutMsec)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        *error = i18n("the D-Bus session bus is not available");
        return std::nullopt;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(s_screencastService, path, interface, method);
    message.setArguments(arguments);
    const QDBusMessage reply = bus.call(message, QDBus::Block, timeoutMsec);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = reply.errorMessage();
        return std::nullopt;
    }
    return reply;
}

bool createParentDirectory(const QUrl &fileUrl, QString *error)
{
    const QDir dir(fileUrl.adjusted(QUrl::RemoveFilename).toLocalFile());
    if (dir.exists() || dir.mkpath(u"."_s)) {
        return true;
    }
    *error = dir.path();
    return false;
}
}

VideoPlatformNiri::VaProfiles VideoPlatformNiri::parseVaProfiles(const QString &vainfoOutput)
{
    // Match whole profile tokens: "VAProfileHEVCMain10" must not be mistaken
    // for "VAProfileHEVCMain".
    const auto hasProfile = [&vainfoOutput](const QString &profile) {
        const auto lines = vainfoOutput.split(u'\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(profile) && trimmed.size() > profile.size()) {
                const QChar next = trimmed.at(profile.size());
                if (next.isSpace() || next == u':') {
                    return true;
                }
            }
        }
        return false;
    };

    VaProfiles profiles;
    profiles.h264 = hasProfile(u"VAProfileH264High"_s) || hasProfile(u"VAProfileH264Main"_s);
    profiles.hevc = hasProfile(u"VAProfileHEVCMain"_s);
    profiles.av1 = hasProfile(u"VAProfileAV1Profile0"_s);
    return profiles;
}

VideoPlatformNiri::VideoPlatformNiri(QObject *parent)
    : VideoPlatform(parent)
{
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process.readAllStandardError();
        if (m_stderr.size() > s_maxStderrBytes) {
            m_stderr = m_stderr.right(s_maxStderrBytes / 2);
        }
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        // wf-recorder reports progress on stderr; drop stdout so the pipe cannot fill up.
        m_process.readAllStandardOutput();
    });
    connect(this, &VideoPlatform::recordingStateChanged, this, [this](RecordingState state) {
        if (state == RecordingState::Recording) {
            // Keep the output awake so a long static recording is not cut off
            // by DPMS/session idle; Rendering keeps the inhibitor until the
            // file is finalised.
            m_idleInhibitor.start();
        } else if (state == RecordingState::NotRecording || state == RecordingState::Finished) {
            m_idleInhibitor.stop();
        }
    });
    connect(&m_windowPickProcess, &QProcess::finished, this, &VideoPlatformNiri::handleWindowPickFinished);
    connect(&m_windowPickProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_windowPickPending || error != QProcess::FailedToStart) {
            return;
        }
        cancelWindowPick(
            i18nc("@info:shell, %1 is an error message", "Failed to record: could not start the window picker: %1", m_windowPickProcess.errorString()));
    });
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        const QString errorOutput = QString::fromLocal8Bit(m_stderr).trimmed().right(s_maxErrorMessageChars);
        const bool wasGst = m_gstRecording;
        qCDebug(SPECTACLE_LOG) << "encoder finished: exitCode" << exitCode << "status" << status << "wasGst" << wasGst << "stderr" << errorOutput;
        m_gstRecording = false;
        if (wasGst) {
            stopScreencastSession();
        }
        if (status == QProcess::NormalExit && exitCode == 0) {
            const QFileInfo fileInfo(m_fileUrl.toLocalFile());
            if (fileInfo.size() <= 0) {
                setRecordingState(RecordingState::NotRecording);
                Q_EMIT recordingFailed(i18n("The encoder exited successfully but wrote no data."));
                return;
            }
            setRecordingState(RecordingState::Finished);
            Q_EMIT recordingSaved(m_fileUrl);
        } else {
            setRecordingState(RecordingState::NotRecording);
            if (!errorOutput.isEmpty()) {
                Q_EMIT recordingFailed(errorOutput);
            } else if (wasGst) {
                Q_EMIT recordingFailed(i18nc("@info:shell, %1 is a process exit code", "gst-launch-1.0 exited with code %1.", exitCode));
            } else {
                Q_EMIT recordingFailed(i18nc("@info:shell, %1 is a process exit code", "wf-recorder exited with code %1.", exitCode));
            }
        }
    });
}

VideoPlatformNiri::~VideoPlatformNiri()
{
    ++m_windowPickGeneration;
    m_windowPickPending = false;
    if (m_windowPickProcess.state() != QProcess::NotRunning) {
        disconnect(&m_windowPickProcess, nullptr, this, nullptr);
        m_windowPickProcess.kill();
        m_windowPickProcess.waitForFinished(s_killTimeoutMsec);
    }
    if (m_process.state() != QProcess::NotRunning) {
        // Do not let the finished handler run while the rest of the application
        // is being torn down.
        disconnect(&m_process, nullptr, this, nullptr);
        const qint64 pid = m_process.processId();
        if (pid > 0) {
            ::kill(static_cast<pid_t>(pid), SIGINT);
        }
        if (!m_process.waitForFinished(s_shutdownTimeoutMsec)) {
            m_process.kill();
            m_process.waitForFinished(s_killTimeoutMsec);
        }
    }
    stopScreencastSession();
}

VideoPlatform::RecordingModes VideoPlatformNiri::supportedRecordingModes() const
{
    RecordingModes modes = Screen | Region;
    if (gstScreencastAvailable()) {
        return modes | Window;
    }

    // Without a usable GStreamer consumer there is no way to read niri's
    // PipeWire stream; hide Window instead of offering a mode that always
    // fails, and say exactly which piece is missing once.
    static bool s_missingGstLogged = false;
    if (!s_missingGstLogged) {
        qCInfo(SPECTACLE_LOG) << "Window recording disabled:" << gstScreencastSupportError();
        s_missingGstLogged = true;
    }
    return modes;
}

VideoPlatform::Formats VideoPlatformNiri::supportedFormats() const
{
    const auto &available = encoderAvailability();
    Formats formats;
    if (available.vp9) {
        formats |= WebM_VP9;
    }
    // A format is advertised when either wf-recorder's ffmpeg encoder or a
    // complete VA-API chain is available. vah*Available() already checks that
    // vainfo reports a real hardware profile, not just the GStreamer element.
    if (available.h264 || gstX264Available() || (gstVah264Available() && gstH264ParseAvailable() && gstMp4MuxAvailable())) {
        formats |= MP4_H264;
    }
    if (available.hevc || (gstVah265Available() && gstH265ParseAvailable() && gstMp4MuxAvailable())) {
        formats |= MP4_HEVC;
    }
    if (available.av1 || (gstVaav1Available() && gstAv1ParseAvailable() && gstMp4MuxAvailable())) {
        formats |= MP4_AV1;
    }
    return formats;
}

VideoPlatform::Formats VideoPlatformNiri::supportedFormatsForMode(RecordingMode mode) const
{
    if (mode == Window) {
        if (!gstScreencastAvailable()) {
            return NoFormat;
        }
        Formats formats;
        const Format candidates[] = {WebM_VP9, MP4_H264, MP4_HEVC, MP4_AV1};
        for (Format format : candidates) {
            if (gstVideoFormatSupportError(format).isEmpty()) {
                formats |= format;
            }
        }
        return formats;
    }
    // Screen and Region can use ScreenCast or the wf-recorder fallback, so
    // every globally supported format is usable.
    return supportedFormats();
}

VideoPlatform::Format VideoPlatformNiri::windowFallbackFormat(const QUrl &fileUrl, Formats gstFormats)
{
    const QString path = fileUrl.toLocalFile();
    if (!path.isEmpty()) {
        if (path.endsWith(u".webm"_s, Qt::CaseInsensitive)) {
            return gstFormats.testFlag(WebM_VP9) ? WebM_VP9 : NoFormat;
        }
        if (!path.endsWith(u".mp4"_s, Qt::CaseInsensitive)) {
            // Do not silently write an MP4 stream into an unknown container
            // name such as .mkv or .mov.
            return NoFormat;
        }
    }
    // Prefer compatibility over compression when substituting a format.
    if (gstFormats.testFlag(MP4_H264)) {
        return MP4_H264;
    }
    if (gstFormats.testFlag(MP4_HEVC)) {
        return MP4_HEVC;
    }
    if (gstFormats.testFlag(MP4_AV1)) {
        return MP4_AV1;
    }
    return gstFormats.testFlag(WebM_VP9) ? WebM_VP9 : NoFormat;
}

bool VideoPlatformNiri::isRecordingOrStarting() const
{
    return isRecording() || m_windowPickPending || m_screencastPending;
}

bool VideoPlatformNiri::isRecordingAudio() const
{
    const bool audioEnabled = Settings::videoRecordSystemAudio() || Settings::videoRecordMicrophone();
    // Screen and Window use the GStreamer screencast pipeline, which supports
    // system audio and microphone input.
    if (recordingMode() == Screen || recordingMode() == Window) {
        return audioEnabled;
    }
    const auto format = static_cast<Format>(Settings::preferredVideoFormat());
    return formatSupportsAudio(format) && audioEnabled;
}

bool VideoPlatformNiri::regionNeedsSelection(const QVariantMap &options)
{
    return options.value(u"rect"_s).toRectF().toRect().isEmpty();
}

std::optional<VideoPlatformNiri::RegionCapture> VideoPlatformNiri::regionCaptureFor(const QRect &region, const QList<NiriIpc::Output> &outputs)
{
    if (region.isEmpty()) {
        return std::nullopt;
    }
    const QRectF logicalRegion(region);
    for (const auto &output : outputs) {
        if (!output.logicalRect.contains(logicalRegion)) {
            continue;
        }
        const qreal scale = output.scale > 0 ? output.scale : 1.0;
        const QSize physical = output.physicalSize.isEmpty() ? QSize(qRound(output.logicalRect.width() * scale), qRound(output.logicalRect.height() * scale))
                                                             : output.physicalSize;
        const int streamWidth = physical.width();
        const int streamHeight = physical.height();
        int left = qRound((logicalRegion.x() - output.logicalRect.x()) * scale);
        int top = qRound((logicalRegion.y() - output.logicalRect.y()) * scale);
        int right = qRound((logicalRegion.right() - output.logicalRect.x()) * scale);
        int bottom = qRound((logicalRegion.bottom() - output.logicalRect.y()) * scale);
        // Snap selection edges that touch the output edge: the logical size is
        // rounded by niri, so multiplying it by the scale can be one pixel
        // short of the physical stream. Snapping keeps a full-output region
        // (the common "press Enter" case) from losing a column or row.
        if (logicalRegion.left() <= output.logicalRect.left() + 0.01) {
            left = 0;
        }
        if (logicalRegion.top() <= output.logicalRect.top() + 0.01) {
            top = 0;
        }
        if (logicalRegion.right() >= output.logicalRect.right() - 0.01) {
            right = streamWidth;
        }
        if (logicalRegion.bottom() >= output.logicalRect.bottom() - 0.01) {
            bottom = streamHeight;
        }
        const int width = right - left;
        const int height = bottom - top;
        if (width <= 0 || height <= 0 || left < 0 || top < 0 || right > streamWidth || bottom > streamHeight) {
            continue;
        }
        RegionCapture capture;
        capture.output = output.name;
        capture.streamSize = QSize(streamWidth, streamHeight);
        capture.margins = QMargins(left, top, streamWidth - right, streamHeight - bottom);
        return capture;
    }
    return std::nullopt;
}

VideoPlatform::Format VideoPlatformNiri::effectiveFormat(const QUrl &fileUrl, Format preferred, Formats supported)
{
    Format candidate = preferred;
    if (fileUrl.isLocalFile() && !fileUrl.toLocalFile().isEmpty()) {
        const QString path = fileUrl.toLocalFile();
        const QString name = path.mid(path.lastIndexOf(u'/') + 1);
        // An explicit extension we do not know must fail instead of writing a
        // container that does not match the filename.
        if (name.contains(u'.') && formatForPath(path) == NoFormat) {
            return NoFormat;
        }
        const auto fromPath = formatForPath(path);
        if (fromPath != NoFormat) {
            // .mp4 is ambiguous between H.264, H.265 and AV1: when the user's
            // preferred format uses the same container, honour the preference
            // instead of falling back to the extension's default (H.264).
            if (preferred != NoFormat && extensionForFormat(preferred) == extensionForFormat(fromPath)) {
                candidate = preferred;
            } else {
                candidate = fromPath;
            }
        }
    }

    if (candidate == NoFormat || !supported.testFlag(candidate)) {
        return NoFormat;
    }
    return candidate;
}

QString VideoPlatformNiri::wfRecorderCodec(Format format)
{
    switch (format) {
    case WebM_VP9:
        return u"libvpx-vp9"_s;
    case MP4_H264:
        return u"libx264"_s;
    case MP4_HEVC:
        return u"libx265"_s;
    case MP4_AV1:
        return u"libaom-av1"_s;
    default:
        return {};
    }
}

bool VideoPlatformNiri::wfRecorderCodecAvailable(Format format, bool hasVp9, bool hasH264, bool hasHevc, bool hasAv1)
{
    switch (format) {
    case WebM_VP9:
        return hasVp9;
    case MP4_H264:
        return hasH264;
    case MP4_HEVC:
        return hasHevc;
    case MP4_AV1:
        return hasAv1;
    default:
        return false;
    }
}

int VideoPlatformNiri::bitrateKbpsForQuality(int quality)
{
    switch (quality) {
    case 0: // Settings::EnumVideoQuality::Low
        return 4000;
    case 2: // High
        return 18000;
    case 1: // Medium
    default:
        return 10000;
    }
}

QStringList VideoPlatformNiri::argumentsFor(RecordingMode mode,
                                            const QString &outputPath,
                                            const QRect &region,
                                            Format format,
                                            bool systemAudio,
                                            const QString &microphone,
                                            int bitrateKbps)
{
    Q_UNUSED(mode)

    QStringList args{u"-f"_s, outputPath};
    // wf-recorder expects the geometry in logical coordinates: "x,y WxH".
    args << u"-g"_s << QStringLiteral("%1,%2 %3x%4").arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height());

    const QString codec = wfRecorderCodec(format);
    if (!codec.isEmpty()) {
        args << u"-c"_s << codec;
        if (bitrateKbps > 0) {
            // wf-recorder forwards -p key=value to the chosen ffmpeg encoder.
            args << u"-p"_s << QStringLiteral("b=%1").arg(bitrateKbps * 1000);
        }
    }
    if (format == MP4_AV1) {
        // libsvtav1 fails with wf-recorder on the reference host; libaom-av1
        // produces a valid AV1 stream (slower, hence cpu-used=8).
        args << u"-p"_s << u"cpu-used=8"_s;
    }

    if (systemAudio) {
        args << u"--audio"_s;
    }
    if (!microphone.isEmpty()) {
        args << u"--audio=%1"_s.arg(microphone);
    }
    return args;
}

QStringList VideoPlatformNiri::gstArguments(Format format,
                                            quint32 nodeId,
                                            const QString &outputPath,
                                            const GstCapabilities &capabilities,
                                            AudioMode audio,
                                            const QString &microphone,
                                            int bitrateKbps,
                                            const QMargins &cropMargins)
{
    if (audio != AudioMode::None && !capabilities.pulsesrc) {
        return {};
    }
    if (audio == AudioMode::Both && !capabilities.audiomixer) {
        return {};
    }

    QStringList videoChain;
    QString audioEncoder;
    QString muxer;
    const bool mp4 = isMp4Format(format);
    if (audio != AudioMode::None && mp4 && !capabilities.avencAac) {
        return {};
    }
    switch (format) {
    case WebM_VP9:
        if (!capabilities.vp9enc || !capabilities.webmmux) {
            return {};
        }
        if (audio != AudioMode::None && !capabilities.opusenc) {
            return {};
        }
        videoChain << u"vp9enc"_s << u"deadline=1"_s << QStringLiteral("target-bitrate=%1").arg(bitrateKbps * 1000);
        audioEncoder = u"opusenc"_s;
        muxer = u"webmmux"_s;
        break;
    case MP4_H264:
        if (!capabilities.mp4mux) {
            return {};
        }
        // Prefer the VA-API encoder when it is available; x264enc is the
        // software fallback from gst-plugins-ugly.
        if (capabilities.vah264enc && capabilities.h264parse) {
            videoChain << u"vah264enc"_s << QStringLiteral("bitrate=%1").arg(bitrateKbps) << u"!"_s << u"h264parse"_s;
        } else if (capabilities.x264enc) {
            videoChain << u"x264enc"_s << u"tune=zerolatency"_s << u"speed-preset=ultrafast"_s << QStringLiteral("bitrate=%1").arg(bitrateKbps);
        } else {
            return {};
        }
        audioEncoder = u"avenc_aac"_s;
        muxer = u"mp4mux"_s;
        break;
    case MP4_HEVC:
        if (!capabilities.vah265enc || !capabilities.h265parse || !capabilities.mp4mux) {
            return {};
        }
        videoChain << u"vah265enc"_s << QStringLiteral("bitrate=%1").arg(bitrateKbps) << u"!"_s << u"h265parse"_s;
        audioEncoder = u"avenc_aac"_s;
        muxer = u"mp4mux"_s;
        break;
    case MP4_AV1:
        if (!capabilities.vaav1enc || !capabilities.av1parse || !capabilities.mp4mux) {
            return {};
        }
        videoChain << u"vaav1enc"_s << QStringLiteral("bitrate=%1").arg(bitrateKbps) << u"!"_s << u"av1parse"_s;
        audioEncoder = u"avenc_aac"_s;
        muxer = u"mp4mux"_s;
        break;
    default:
        return {};
    }

    // do-timestamp=true is required: niri's PipeWire buffers do not carry a
    // usable presentation timestamp on their own. niri only pushes a screencast
    // frame when something inside the recorded window changes, so a static
    // window would otherwise produce a one-frame file. Compositor is a live
    // aggregator: it keeps the last frame of each pad and re-emits it at the
    // negotiated rate while the pad is idle, which turns a static window into a
    // valid constant-frame-rate stream. videorate pins the output to 30 fps.
    QStringList args{u"-e"_s};

    const bool withAudio = audio != AudioMode::None;
    const bool fragmentedMp4 = mp4;
    if (withAudio) {
        // Named muxer first so the video/audio branches can link with mux.
        args << muxer << u"name=mux"_s;
        if (fragmentedMp4) {
            // A fragmented MP4 keeps a playable index while recording, so a
            // crash, SIGKILL, lock-screen stream error or full disk leaves the
            // already-written seconds usable instead of a moov-less file.
            args << u"fragment-duration=1000"_s;
        }
        args << u"!"_s << u"filesink"_s << QStringLiteral("location=%1").arg(outputPath);
    }

    args << u"compositor"_s << u"name=comp"_s << u"background=black"_s << u"!"_s << u"videoconvert"_s;
    if (!cropMargins.isNull()) {
        args << u"!"_s << u"videocrop"_s << QStringLiteral("left=%1").arg(cropMargins.left()) << QStringLiteral("top=%1").arg(cropMargins.top())
             << QStringLiteral("right=%1").arg(cropMargins.right()) << QStringLiteral("bottom=%1").arg(cropMargins.bottom());
    }
    args << u"!"_s << u"videorate"_s << u"!"_s << u"video/x-raw,framerate=30/1"_s << u"!"_s << videoChain;

    if (!withAudio) {
        args << u"!"_s << muxer;
        if (fragmentedMp4) {
            args << u"fragment-duration=1000"_s;
        }
        args << u"!"_s << u"filesink"_s << QStringLiteral("location=%1").arg(outputPath);
        args << u"pipewiresrc"_s << QStringLiteral("path=%1").arg(nodeId) << u"do-timestamp=true"_s << u"!"_s << u"videoconvert"_s << u"!"_s
             << u"comp.sink_0"_s;
        return args;
    }

    args << u"!"_s << u"mux."_s;

    const QString micDevice = microphone.isEmpty() ? QString() : microphone;
    auto appendPulseSource = [&](const QString &device, bool toMixer) {
        args << u"pulsesrc"_s;
        if (!device.isEmpty()) {
            args << QStringLiteral("device=%1").arg(device);
        }
        args << u"!"_s << u"audioconvert"_s;
        if (toMixer) {
            args << u"!"_s << u"amix."_s;
        } else {
            args << u"!"_s << u"audioresample"_s << u"!"_s << audioEncoder << u"!"_s << u"mux."_s;
        }
    };

    if (audio == AudioMode::Both) {
        args << u"audiomixer"_s << u"name=amix"_s << u"!"_s << u"audioconvert"_s << u"!"_s << u"audioresample"_s << u"!"_s << audioEncoder << u"!"_s
             << u"mux."_s;
        appendPulseSource(u"@DEFAULT_MONITOR@"_s, true);
        appendPulseSource(micDevice, true);
    } else if (audio == AudioMode::System) {
        appendPulseSource(u"@DEFAULT_MONITOR@"_s, false);
    } else if (audio == AudioMode::Microphone) {
        appendPulseSource(micDevice, false);
    }

    args << u"pipewiresrc"_s << QStringLiteral("path=%1").arg(nodeId) << u"do-timestamp=true"_s << u"!"_s << u"videoconvert"_s << u"!"_s << u"comp.sink_0"_s;
    return args;
}

void VideoPlatformNiri::startRecording(const QUrl &fileUrl, RecordingMode mode, const QVariantMap &options, bool includePointer)
{
    if (mode == NoRecordingModes) {
        Q_EMIT recordingCanceled(i18nc("@info:shell", "Recording canceled: no recording mode"));
        return;
    }
    if (mode != Screen && mode != Region && mode != Window) {
        Q_EMIT recordingFailed(i18nc("@info:shell", "Failed to record: unsupported recording mode."));
        return;
    }
    if (m_windowPickPending) {
        Q_EMIT recordingFailed(i18n("A recording is already starting"));
        return;
    }
    if (!screencastStartAllowed(m_screencastPending, m_screencastSessionPath, m_process.state() != QProcess::NotRunning)) {
        Q_EMIT recordingFailed(i18n("A recording is already starting"));
        return;
    }
    // Window recording is only possible through the GStreamer screencast
    // consumer. Reject before asking the user to pick a window.
    if (mode == Window && !gstScreencastAvailable()) {
        Q_EMIT recordingFailed(
            i18nc("@info:shell, %1 is an error message", "Failed to record: window recording is unavailable: %1.", gstScreencastSupportError()));
        return;
    }
    if (mode == Region && regionNeedsSelection(options)) {
        // Let SpectacleCore show the selection overlay; it calls startRecording
        // again with the selected rectangle once the user accepted it.
        Q_EMIT regionRequested();
        return;
    }
    if (!fileUrl.isEmpty() && !fileUrl.isLocalFile()) {
        Q_EMIT recordingFailed(i18nc("@info:shell", "Failed to record: the output file URL is not a local file."));
        return;
    }

    const auto preferredFormat = static_cast<Format>(Settings::preferredVideoFormat());
    const Formats modeFormats = supportedFormatsForMode(mode);
    Format format = effectiveFormat(fileUrl, preferredFormat, modeFormats);
    bool substitutedFormat = false;
    if (format == NoFormat && mode == Window) {
        format = windowFallbackFormat(fileUrl, modeFormats);
        substitutedFormat = format != NoFormat && preferredFormat != NoFormat;
    }
    const int bitrateKbps = bitrateKbpsForQuality(Settings::videoQuality());
    if (format == NoFormat) {
        if (fileUrl.isLocalFile()) {
            Q_EMIT recordingFailed(
                i18nc("@info:shell, %1 is a file path", "Failed to record: the video format of %1 is not supported.", fileUrl.toLocalFile()));
        } else {
            Q_EMIT recordingFailed(i18nc("@info:shell", "Failed to record: the selected video format is not supported."));
        }
        return;
    }

    if (substitutedFormat) {
        const auto notification = new KNotification(u"recordingFormatChanged"_s, KNotification::CloseOnTimeout, this);
        notification->setTitle(i18n("Recording format changed"));
        notification->setText(i18n("The selected format is not available for window recording; using %1 instead.",
                                   format == WebM_VP9       ? i18n("WebM/VP9")
                                       : format == MP4_HEVC ? i18n("MP4/H.265")
                                       : format == MP4_AV1  ? i18n("MP4/AV1")
                                                            : i18n("MP4/H.264")));
        notification->sendEvent();
    }

    // SpectacleCore passes an invalid URL unless the user picked a target file;
    // in that case record into ExportManager's temporary directory and let the
    // usual save/export flow move the file to the user's video folder.
    QUrl outputUrl = fileUrl;
    if (!outputUrl.isValid()) {
        ExportManager::instance()->updateTimestamp();
        const QString filename = ExportManager::formattedFilename(Settings::videoFilenameTemplate(),
                                                                  ExportManager::instance()->timestamp(),
                                                                  options.value(u"caption"_s).toString(),
                                                                  Settings::videoSaveLocation());
        outputUrl = ExportManager::instance()->tempVideoUrl(filename);
        if (!outputUrl.isLocalFile() || outputUrl.toLocalFile().isEmpty()) {
            Q_EMIT recordingFailed(i18nc("@info:shell", "Failed to record: unable to create a temporary output file."));
            return;
        }
    }
    QString directoryError;
    if (!createParentDirectory(outputUrl, &directoryError)) {
        Q_EMIT recordingFailed(i18nc("@info:shell, %1 is a directory path", "Failed to record: unable to create folder (%1).", directoryError));
        return;
    }

    const QStorageInfo storage(QFileInfo(outputUrl.toLocalFile()).absolutePath());
    if (storage.isValid() && storage.isReadOnly()) {
        Q_EMIT recordingFailed(i18nc("@info:shell, %1 is a directory path", "Failed to record: the output folder (%1) is read-only.", storage.rootPath()));
        return;
    }
    // Enough for roughly half a minute at the highest preset; fail before a
    // long recording runs the filesystem out of space.
    constexpr qint64 s_minimumFreeBytes = 64 * 1024 * 1024;
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < s_minimumFreeBytes) {
        Q_EMIT recordingFailed(i18nc("@info:shell, %1 is a free-space amount",
                                     "Failed to record: not enough free space in the output folder (%1 MB available).",
                                     storage.bytesAvailable() / (1024 * 1024)));
        return;
    }

    m_fileUrl = outputUrl;

    // Window recording needs a window id. Without one, let the user click the
    // window instead of requiring it to be focused beforehand; the pick is
    // asynchronous so the UI stays responsive and finishRecording() can cancel
    // it. The picked id is passed back through the same options map.
    const quint64 windowId = options.value(u"window_id"_s).toULongLong();
    if (mode == Window && windowId == 0) {
        startWindowPick(fileUrl, options, includePointer);
        return;
    }

    // Region can use the same RecordMonitor consumer as Screen with a
    // videocrop rectangle, which makes the pointer, audio and VA-API encoders
    // available to region recordings too. If the region is not fully inside a
    // single output, or the pipeline cannot be built, fall through to
    // wf-recorder as before.
    std::optional<RegionCapture> regionCapture;
    if (mode == Region) {
        QRect region = options.value(u"rect"_s).toRectF().toRect();
        if (!region.isEmpty()) {
            QString outputError;
            const auto outputs = NiriIpc::outputs(&outputError);
            regionCapture = regionCaptureFor(region, outputs);
        }
    }

    // Screen and Window use niri's Mutter ScreenCast plus GStreamer. If the
    // GStreamer consumer or the elements for the selected format/audio are
    // unavailable, Screen falls back to wf-recorder below; Region always stays
    // on wf-recorder.
    const AudioMode screencastAudio = audioModeFromSettings();
    const bool screencastBuildable = mode == Window
        || ((mode == Screen || (mode == Region && regionCapture.has_value()))
            && !gstArguments(format,
                             0,
                             u"/dev/null"_s,
                             gstCapabilitiesFor(format, screencastAudio),
                             screencastAudio,
                             options.value(u"microphone"_s).toString(),
                             bitrateKbps,
                             regionCapture ? regionCapture->margins : QMargins())
                    .isEmpty());
    if ((mode == Screen || mode == Window || (mode == Region && regionCapture.has_value())) && gstScreencastAvailable() && screencastBuildable) {
        const AudioMode audio = audioModeFromSettings();
        QString connector;
        if (mode == Region && regionCapture) {
            connector = regionCapture->output;
        } else if (mode == Screen) {
            connector = options.value(u"screen"_s).toString();
            if (connector.isEmpty()) {
                QString error;
                const auto output = NiriIpc::focusedOutput(&error);
                if (!output) {
                    if (error.isEmpty()) {
                        Q_EMIT recordingFailed(i18nc("@info:shell", "Failed to record: niri did not report a focused output."));
                    } else {
                        Q_EMIT recordingFailed(i18nc("@info:shell, %1 is an error message", "Failed to record: %1", error));
                    }
                    return;
                }
                connector = output->name;
            }
            if (connector.isEmpty()) {
                Q_EMIT recordingFailed(i18n("Failed to record: the screen connector is empty."));
                return;
            }
        }
        startScreencastRecording(mode,
                                 format,
                                 includePointer,
                                 connector,
                                 windowId,
                                 audio,
                                 options.value(u"microphone"_s).toString(),
                                 bitrateKbps,
                                 regionCapture ? regionCapture->margins : QMargins());
        return;
    }
    if (mode == Window) {
        Q_EMIT recordingFailed(
            i18nc("@info:shell, %1 is an error message", "Failed to record: window recording is unavailable: %1.", gstScreencastSupportError()));
        return;
    }

    // wf-recorder fallback for Screen and the Region path. wf-recorder has no
    // cursor option, so includePointer cannot be applied here.
    static bool s_includePointerLogged = false;
    if (!s_includePointerLogged) {
        qCInfo(SPECTACLE_LOG) << "wf-recorder cannot toggle cursor capture; niri's cursor setting is used instead of includePointer.";
        s_includePointerLogged = true;
    }

    QRect region;
    if (mode == Screen) {
        QString error;
        const auto output = NiriIpc::focusedOutput(&error);
        if (!output) {
            if (error.isEmpty()) {
                Q_EMIT recordingFailed(i18nc("@info:shell", "Failed to record: niri did not report a focused output."));
            } else {
                Q_EMIT recordingFailed(i18nc("@info:shell, %1 is an error message", "Failed to record: %1", error));
            }
            return;
        }
        region = output->logicalRect.toRect();
    } else {
        // SpectacleCore stores the selection as a QRectF.
        region = options.value(u"rect"_s).toRectF().toRect();
    }
    if (region.isEmpty()) {
        Q_EMIT recordingFailed(i18nc("@info:shell", "Failed to record: the recording region is empty."));
        return;
    }

    const bool audioSupported = formatSupportsAudio(format);

    bool systemAudio = false;
    QString microphone;
    if (audioSupported) {
        systemAudio = Settings::videoRecordSystemAudio();
        if (Settings::videoRecordMicrophone()) {
            microphone = options.value(u"microphone"_s).toString();
            // wf-recorder needs a PulseAudio/PipeWire source name; "default"
            // selects the default source, which is normally the microphone.
            if (microphone.isEmpty()) {
                microphone = u"default"_s;
            }
        }
    }

    // wf-recorder (Screen fallback and Region) only knows these encoders.
    const bool wfFormatSupported = format == WebM_VP9 || format == MP4_H264 || format == MP4_HEVC || format == MP4_AV1;
    if (!wfFormatSupported) {
        Q_EMIT recordingFailed(i18n("Failed to record: wf-recorder does not support the selected video format."));
        return;
    }
    const auto &ffmpegEncoders = encoderAvailability();
    if (format == MP4_HEVC && !ffmpegEncoders.hevc) {
        Q_EMIT recordingFailed(i18n("Failed to record: HEVC region recording requires the libx265 encoder in wf-recorder."));
        return;
    }
    if (format == MP4_AV1 && !ffmpegEncoders.av1) {
        Q_EMIT recordingFailed(i18n("Failed to record: AV1 region recording requires the libaom-av1 encoder in wf-recorder."));
        return;
    }
    if (format == MP4_H264 && !ffmpegEncoders.h264) {
        Q_EMIT recordingFailed(i18n("Failed to record: H.264 region recording requires the libx264 encoder in wf-recorder."));
        return;
    }
    if (format == WebM_VP9 && !ffmpegEncoders.vp9) {
        Q_EMIT recordingFailed(i18n("Failed to record: VP9 region recording requires the libvpx-vp9 encoder in wf-recorder."));
        return;
    }

    m_stderr.clear();
    m_process.start(u"wf-recorder"_s, argumentsFor(mode, outputUrl.toLocalFile(), region, format, systemAudio, microphone, bitrateKbps));
    if (!m_process.waitForStarted(s_startTimeoutMsec)) {
        m_process.kill();
        m_process.waitForFinished(s_killTimeoutMsec);
        Q_EMIT recordingFailed(i18nc("@info:shell, %1 is an error message", "Failed to start wf-recorder: %1", m_process.errorString()));
        return;
    }

    setRecordingMode(mode);
    setRecordingState(RecordingState::Recording);
}

bool VideoPlatformNiri::screencastStartAllowed(bool pending, const QString &sessionPath, bool processRunning)
{
    return !pending && sessionPath.isEmpty() && !processRunning;
}

bool VideoPlatformNiri::screencastPendingGenerationIsCurrent(bool pending, quint64 expectedGeneration, quint64 currentGeneration)
{
    return pending && expectedGeneration == currentGeneration;
}

void VideoPlatformNiri::finishRecording()
{
    if (m_windowPickPending) {
        cancelWindowPick(i18n("Recording canceled: no window selected."));
        return;
    }
    if (m_screencastPending) {
        // Start() succeeded but the PipeWire stream has not arrived yet. Cancel
        // the attempt without launching an encoder or writing a file.
        stopScreencastSession();
        setRecordingState(RecordingState::NotRecording);
        Q_EMIT recordingCanceled(i18n("Recording canceled: the screencast stream did not start."));
        return;
    }

    if (m_process.state() != QProcess::NotRunning) {
        if (recordingState() == RecordingState::Rendering) {
            return;
        }
        // Finalizing a long recording can take far longer than the recording
        // itself suggests (the MP4 index is written at EOS). Report Rendering
        // and let the finished handler complete the file instead of blocking
        // the GUI thread and then SIGKILLing a still-flushing muxer.
        setRecordingState(RecordingState::Rendering);
        const qint64 pid = m_process.processId();
        if (pid > 0) {
            // SIGINT makes wf-recorder finalize the container and gst-launch
            // flush its muxer (with -e) before exiting.
            ::kill(static_cast<pid_t>(pid), SIGINT);
        } else {
            m_process.terminate();
        }
        QTimer::singleShot(s_stopWatchdogMsec, this, [this, pid] {
            if (m_process.state() == QProcess::NotRunning || m_process.processId() != pid) {
                return;
            }
            m_process.kill();
            if (!m_process.waitForFinished(s_killTimeoutMsec)) {
                qCWarning(SPECTACLE_LOG) << "Encoder did not exit after SIGKILL";
            }
        });
        return;
    }

    // Also closes a session whose PipeWire stream has not arrived yet.
    stopScreencastSession();
}

void VideoPlatformNiri::startWindowPick(const QUrl &fileUrl, const QVariantMap &options, bool includePointer)
{
    m_windowPickPending = true;
    const quint64 generation = ++m_windowPickGeneration;
    m_windowPickFileUrl = fileUrl;
    m_windowPickOptions = options;
    m_windowPickIncludePointer = includePointer;

    m_windowPickProcess.setProgram(u"niri"_s);
    m_windowPickProcess.setArguments({u"msg"_s, u"--json"_s, u"pick-window"_s});
    m_windowPickProcess.start();
    if (!m_windowPickProcess.waitForStarted(s_startTimeoutMsec)) {
        cancelWindowPick(
            i18nc("@info:shell, %1 is an error message", "Failed to record: could not start the window picker: %1", m_windowPickProcess.errorString()));
        return;
    }

    QTimer::singleShot(s_windowPickTimeoutMsec, this, [this, generation] {
        if (m_windowPickPending && m_windowPickGeneration == generation) {
            cancelWindowPick(i18n("Recording canceled: no window was selected."));
        }
    });
}

void VideoPlatformNiri::handleWindowPickFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_windowPickPending) {
        return;
    }

    // Consume the pending state before re-entering startRecording().
    m_windowPickPending = false;
    ++m_windowPickGeneration;
    const QUrl fileUrl = m_windowPickFileUrl;
    const QVariantMap options = m_windowPickOptions;
    const bool includePointer = m_windowPickIncludePointer;

    const QByteArray standardOutput = m_windowPickProcess.readAllStandardOutput();
    const QString errorOutput = QString::fromLocal8Bit(m_windowPickProcess.readAllStandardError()).trimmed();
    if (status != QProcess::NormalExit || exitCode != 0) {
        if (!errorOutput.isEmpty()) {
            Q_EMIT recordingFailed(errorOutput);
        } else {
            Q_EMIT recordingFailed(i18nc("@info:shell, %1 is a process exit code", "The window picker exited with code %1.", exitCode));
        }
        setRecordingState(RecordingState::NotRecording);
        return;
    }

    QString parseError;
    const auto window = NiriIpc::parseWindow(standardOutput, &parseError);
    if (!window) {
        if (!parseError.isEmpty()) {
            Q_EMIT recordingFailed(parseError);
        } else {
            // niri prints a literal null document when the pick is cancelled.
            Q_EMIT recordingCanceled(i18n("Recording canceled: no window selected."));
        }
        setRecordingState(RecordingState::NotRecording);
        return;
    }

    QVariantMap nextOptions = options;
    nextOptions.insert(u"window_id"_s, QVariant::fromValue<qulonglong>(window->id));
    startRecording(fileUrl, Window, nextOptions, includePointer);
}

void VideoPlatformNiri::cancelWindowPick(const QString &message)
{
    const bool wasPending = m_windowPickPending;
    ++m_windowPickGeneration;
    m_windowPickPending = false;
    if (m_windowPickProcess.state() != QProcess::NotRunning) {
        m_windowPickProcess.kill();
        m_windowPickProcess.waitForFinished(s_killTimeoutMsec);
    }
    if (!wasPending) {
        return;
    }
    setRecordingState(RecordingState::NotRecording);
    Q_EMIT recordingCanceled(message);
}

void VideoPlatformNiri::startScreencastRecording(RecordingMode mode,
                                                 Format format,
                                                 bool includePointer,
                                                 const QString &screenConnector,
                                                 quint64 windowId,
                                                 AudioMode audio,
                                                 const QString &microphone,
                                                 int bitrateKbps,
                                                 const QMargins &cropMargins)
{
    if (!screencastStartAllowed(m_screencastPending, m_screencastSessionPath, m_process.state() != QProcess::NotRunning)) {
        Q_EMIT recordingFailed(i18n("A recording is already starting"));
        return;
    }
    if (!gstScreencastAvailable()) {
        Q_EMIT recordingFailed(
            i18nc("@info:shell, %1 is an error message", "Failed to record: screencast recording is unavailable: %1.", gstScreencastSupportError()));
        return;
    }
    if (format != WebM_VP9 && !isMp4Format(format)) {
        Q_EMIT recordingFailed(i18n("Failed to record: this video format is not supported for screencast recording."));
        return;
    }
    const QString videoError = gstVideoFormatSupportError(format);
    if (!videoError.isEmpty()) {
        Q_EMIT recordingFailed(i18nc("@info:shell, %1 is an error message", "Failed to record: %1.", videoError));
        return;
    }
    const QString audioError = gstAudioSupportError(format, audio);
    if (!audioError.isEmpty()) {
        Q_EMIT recordingFailed(i18nc("@info:shell, %1 is an error message", "Failed to record: %1.", audioError));
        return;
    }

    QString error;
    QString recordMethod;
    QVariantList recordArguments;
    QVariantMap properties;
    properties.insert(u"cursor-mode"_s, QVariant::fromValue<quint32>(includePointer ? 1 : 0));

    if (mode == Window) {
        if (windowId == 0) {
            Q_EMIT recordingFailed(i18n("No window selected to record"));
            return;
        }
        properties.insert(u"window-id"_s, QVariant::fromValue<qulonglong>(windowId));
        recordMethod = u"RecordWindow"_s;
        recordArguments = {properties};
    } else if (mode == Screen || mode == Region) {
        if (screenConnector.isEmpty()) {
            Q_EMIT recordingFailed(i18n("Failed to record: the screen connector is empty."));
            return;
        }
        recordMethod = u"RecordMonitor"_s;
        recordArguments = {screenConnector, properties};
    } else {
        Q_EMIT recordingFailed(i18n("Failed to record: screencast recording only supports screen, window and region modes."));
        return;
    }

    const quint64 generation = ++m_screencastGeneration;
    m_pendingNodeId = 0;
    m_pendingNodeGeneration = 0;
    m_pendingFormat = format;
    m_screencastMode = mode;
    m_pendingAudioMode = audio;
    m_pendingMicrophone = microphone;
    m_pendingBitrateKbps = bitrateKbps;
    m_pendingCropMargins = cropMargins;

    auto reply = callScreencast(s_screencastPath, s_screencastInterface, u"CreateSession"_s, {QVariantMap()}, &error);
    if (!reply || reply->arguments().isEmpty()) {
        stopScreencastSession();
        Q_EMIT recordingFailed(i18nc("@info:shell, %1 is an error message", "Failed to start screencast recording: %1", error));
        return;
    }
    m_screencastSessionPath = objectPathFrom(reply->arguments().constFirst());
    if (m_screencastSessionPath.isEmpty()) {
        stopScreencastSession();
        Q_EMIT recordingFailed(i18n("Failed to start screencast recording: niri returned an empty screencast session."));
        return;
    }

    reply = callScreencast(m_screencastSessionPath, s_sessionInterface, recordMethod, recordArguments, &error);
    if (!reply || reply->arguments().isEmpty()) {
        failScreencastRecording(i18nc("@info:shell, %1 is an error message", "Failed to start screencast recording: %1", error));
        return;
    }
    m_screencastStreamPath = objectPathFrom(reply->arguments().constFirst());
    if (m_screencastStreamPath.isEmpty()) {
        failScreencastRecording(i18n("Failed to start screencast recording: niri returned an empty screencast stream."));
        return;
    }

    // Subscribe before Start(): niri emits PipeWireStreamAdded as soon as the
    // stream is live, and a missing node id would leave the session running
    // without an encoder.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.connect(s_screencastService, m_screencastStreamPath, s_streamInterface, u"PipeWireStreamAdded"_s, this, SLOT(handlePipeWireStreamAdded(uint)))) {
        failScreencastRecording(i18n("Failed to start screencast recording: could not subscribe to the screencast stream signal."));
        return;
    }
    if (!bus.connect(s_screencastService, m_screencastSessionPath, s_sessionInterface, u"Closed"_s, this, SLOT(handleScreencastClosed()))) {
        failScreencastRecording(i18n("Failed to start screencast recording: could not subscribe to the screencast session signal."));
        return;
    }

    reply = callScreencast(m_screencastSessionPath, s_sessionInterface, u"Start"_s, {}, &error);
    if (!reply) {
        failScreencastRecording(i18nc("@info:shell, %1 is an error message", "Failed to start screencast recording: %1", error));
        return;
    }
    if (m_screencastSessionPath.isEmpty()) {
        // Session.Closed was delivered while Start() was blocking.
        Q_EMIT recordingFailed(i18n("Failed to start screencast recording: the niri screencast session was closed."));
        return;
    }

    // From here until the PipeWire node arrives the start is cancellable and
    // bounded: reporting Recording makes StopRecording/finishRecording reach
    // this backend, and the timeout prevents a leaked niri session if the
    // signal never comes.
    m_screencastPending = true;
    setRecordingMode(mode);
    setRecordingState(RecordingState::Recording);
    QTimer::singleShot(s_windowStartTimeoutMsec, this, [this, generation] {
        if (!screencastPendingGenerationIsCurrent(m_screencastPending, generation, m_screencastGeneration)) {
            return;
        }
        failScreencastRecording(i18n("Timed out waiting for the PipeWire stream"));
    });

    // A fast compositor may already have delivered PipeWireStreamAdded while
    // Start() was blocking.
    maybeStartGst();
}

void VideoPlatformNiri::maybeStartGst()
{
    if (!m_screencastPending) {
        return;
    }
    if (!screencastPendingGenerationIsCurrent(true, m_pendingNodeGeneration, m_screencastGeneration)) {
        return;
    }
    if (m_pendingNodeId == 0 || m_screencastSessionPath.isEmpty()) {
        return;
    }
    if (m_process.state() != QProcess::NotRunning) {
        return;
    }

    const RecordingMode mode = m_screencastMode;
    if ((mode != Screen && mode != Window && mode != Region) || recordingMode() != mode) {
        // The state machine moved on (for example another recording mode was
        // requested); do not attach a delayed stream to it.
        stopScreencastSession();
        setRecordingState(RecordingState::NotRecording);
        return;
    }

    const Format format = m_pendingFormat;
    if (format != WebM_VP9 && !isMp4Format(format)) {
        failScreencastRecording(i18n("Failed to start screencast recording: the stream format is not supported."));
        return;
    }
    const QString videoError = gstVideoFormatSupportError(format);
    if (!videoError.isEmpty()) {
        failScreencastRecording(i18nc("@info:shell, %1 is an error message", "Failed to start screencast recording: %1.", videoError));
        return;
    }

    const QString audioError = gstAudioSupportError(format, m_pendingAudioMode);
    if (!audioError.isEmpty()) {
        failScreencastRecording(i18nc("@info:shell, %1 is an error message", "Failed to start screencast recording: %1.", audioError));
        return;
    }

    const quint32 nodeId = m_pendingNodeId;
    const GstCapabilities capabilities = gstCapabilitiesFor(format, m_pendingAudioMode);
    const QStringList args = gstArguments(format,
                                          nodeId,
                                          m_fileUrl.toLocalFile(),
                                          capabilities,
                                          m_pendingAudioMode,
                                          m_pendingMicrophone,
                                          m_pendingBitrateKbps,
                                          m_pendingCropMargins);
    if (args.isEmpty()) {
        failScreencastRecording(i18n("Failed to start screencast recording: the stream format is not supported."));
        return;
    }

    // The stream is here: consume the pending state before starting the
    // process so the timeout and any second start are invalidated.
    m_screencastPending = false;
    ++m_screencastGeneration;
    m_pendingNodeId = 0;
    m_pendingNodeGeneration = 0;

    qCDebug(SPECTACLE_LOG) << "screencast gst-launch-1.0 arguments:" << args;
    m_stderr.clear();
    m_process.start(gstLaunchPath(), args);
    if (!m_process.waitForStarted(s_startTimeoutMsec)) {
        m_process.kill();
        m_process.waitForFinished(s_killTimeoutMsec);
        failScreencastRecording(i18nc("@info:shell, %1 is an error message", "Failed to start gst-launch-1.0: %1", m_process.errorString()));
        return;
    }

    m_gstRecording = true;
    setRecordingMode(mode);
    setRecordingState(RecordingState::Recording);
}

void VideoPlatformNiri::stopScreencastSession()
{
    // Any pending wait is over, and its timeout must not fire for a later start.
    ++m_screencastGeneration;
    m_screencastPending = false;
    m_pendingNodeId = 0;
    m_pendingNodeGeneration = 0;
    m_pendingFormat = NoFormat;
    m_screencastMode = NoRecordingModes;
    m_pendingAudioMode = AudioMode::None;
    m_pendingMicrophone.clear();

    if (m_screencastSessionPath.isEmpty()) {
        return;
    }

    const QString sessionPath = m_screencastSessionPath;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!m_screencastStreamPath.isEmpty()) {
        bus.disconnect(s_screencastService, m_screencastStreamPath, s_streamInterface, u"PipeWireStreamAdded"_s, this, SLOT(handlePipeWireStreamAdded(uint)));
    }
    bus.disconnect(s_screencastService, sessionPath, s_sessionInterface, u"Closed"_s, this, SLOT(handleScreencastClosed()));

    m_screencastSessionPath.clear();
    m_screencastStreamPath.clear();

    QString error;
    callScreencast(sessionPath, s_sessionInterface, u"Stop"_s, {}, &error, s_dbusStopTimeoutMsec);
    if (!error.isEmpty()) {
        qCInfo(SPECTACLE_LOG) << "Failed to stop niri screencast session:" << error;
    }
}

void VideoPlatformNiri::failScreencastRecording(const QString &message)
{
    stopScreencastSession();
    setRecordingState(RecordingState::NotRecording);
    Q_EMIT recordingFailed(message);
}

void VideoPlatformNiri::handlePipeWireStreamAdded(uint nodeId)
{
    if (m_screencastSessionPath.isEmpty() || m_screencastStreamPath.isEmpty()) {
        return;
    }

    const QString streamPath = m_screencastStreamPath;
    QDBusConnection::sessionBus()
        .disconnect(s_screencastService, streamPath, s_streamInterface, u"PipeWireStreamAdded"_s, this, SLOT(handlePipeWireStreamAdded(uint)));
    m_screencastStreamPath.clear();

    m_pendingNodeId = nodeId;
    m_pendingNodeGeneration = m_screencastGeneration;
    maybeStartGst();
}

void VideoPlatformNiri::handleScreencastClosed()
{
    // niri closes the session when the recorded window disappears or the user
    // revokes it. Finish the pipeline through the regular teardown path so the
    // container is finalized and the usual save/export flow runs.
    if (m_process.state() != QProcess::NotRunning) {
        finishRecording();
        return;
    }
    if (m_screencastPending) {
        failScreencastRecording(i18n("Failed to record: the niri screencast session was closed."));
        return;
    }
    // No encoder was running: just drop the stale session handle instead of
    // reporting a failure for a session we already finished.
    stopScreencastSession();
}

#include "moc_VideoPlatformNiri.cpp"

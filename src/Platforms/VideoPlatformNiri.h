/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "IdleInhibitor.h"
#include "VideoPlatform.h"

#include <QByteArray>
#include <QMargins>
#include <QProcess>
#include <QSize>
#include <QStringList>

#include <optional>

namespace NiriIpc
{
struct Output;
}

/**
 * Screen recording backend for niri.
 *
 * Screen and Window are captured through niri's org.gnome.Mutter.ScreenCast
 * implementation (RecordMonitor / RecordWindow) and encoded by
 * gst-launch-1.0, including optional audio mixed from PulseAudio/PipeWire.
 * Region keeps using wf-recorder; Screen falls back to wf-recorder when the
 * GStreamer consumer is unavailable.
 */
class VideoPlatformNiri final : public VideoPlatform
{
    Q_OBJECT

public:
    enum class AudioMode {
        None,
        System,
        Microphone,
        Both,
    };

    /// Hardware profiles reported by `vainfo`. Element availability alone does
    /// not mean the driver can actually encode the codec.
    struct VaProfiles {
        bool h264 = false;
        bool hevc = false;
        bool av1 = false;
    };
    static VaProfiles parseVaProfiles(const QString &vainfoOutput);

    /// Optional GStreamer elements the pipeline depends on. Passed in so
    /// gstArguments() stays pure and unit-testable.
    struct GstCapabilities {
        bool vp9enc = false;
        bool webmmux = false;
        bool x264enc = false;
        bool vah264enc = false;
        bool h264parse = false;
        bool vah265enc = false;
        bool h265parse = false;
        bool vaav1enc = false;
        bool av1parse = false;
        bool mp4mux = false;
        bool opusenc = false;
        bool avencAac = false;
        bool pulsesrc = false;
        bool audiomixer = false;
    };

    explicit VideoPlatformNiri(QObject *parent = nullptr);
    ~VideoPlatformNiri() override;

    RecordingModes supportedRecordingModes() const override;
    Formats supportedFormats() const override;
    Formats supportedFormatsForMode(RecordingMode mode) const override;

    /// Best GStreamer-only fallback for a window recording when the preferred
    /// format is not available for Window. Returns NoFormat when none works.
    static Format windowFallbackFormat(const QUrl &fileUrl, Formats gstFormats);
    bool isRecordingAudio() const override;
    bool isRecordingOrStarting() const override;

    void startRecording(const QUrl &fileUrl, RecordingMode mode, const QVariantMap &options, bool includePointer) override;
    void finishRecording() override;

    static QStringList argumentsFor(RecordingMode mode,
                                    const QString &outputPath,
                                    const QRect &region,
                                    Format format,
                                    bool systemAudio,
                                    const QString &microphone,
                                    int bitrateKbps = 10000);

    /// Bitrate presets exposed as Settings::VideoQuality. Returns kbit/s.
    static int bitrateKbpsForQuality(int quality);

    /// wf-recorder/ffmpeg codec name for a format, or an empty string.
    static QString wfRecorderCodec(Format format);

    /// Pure gate used by the Region/Screen-fallback validation. Element
    /// availability is passed in so the failure path is unit-testable.
    static bool wfRecorderCodecAvailable(Format format, bool hasVp9, bool hasH264, bool hasHevc, bool hasAv1);

    /**
     * Builds the gst-launch-1.0 argument list for a Mutter ScreenCast stream.
     *
     * The returned list is empty when the format, the requested audio mode or
     * the capabilities say that a required element is missing. The caller
     * passes probed capabilities so this function stays pure and testable.
     */
    static QStringList gstArguments(Format format,
                                    quint32 nodeId,
                                    const QString &outputPath,
                                    const GstCapabilities &capabilities,
                                    AudioMode audio = AudioMode::None,
                                    const QString &microphone = {},
                                    int bitrateKbps = 10000,
                                    const QMargins &cropMargins = {});

    /// True when a new recording may start. Pending screencast starts and
    /// active screencast sessions block a second request just like a running
    /// encoder.
    static bool screencastStartAllowed(bool pending, const QString &sessionPath, bool processRunning);

    /// Guards the screencast stream timeout against stale generations.
    static bool screencastPendingGenerationIsCurrent(bool pending, quint64 expectedGeneration, quint64 currentGeneration);

    /**
     * Picks the format for a recording.
     *
     * An explicit local output file determines the format through its extension
     * (e.g. foo.webm always records VP9); otherwise the preferred format from
     * Settings is used. Returns NoFormat when the result is not supported, so
     * callers can reject the request instead of writing a broken file.
     */
    static Format effectiveFormat(const QUrl &fileUrl, Format preferred, Formats supported);

    /// True when a Region recording has no rectangle yet and the selection
    /// overlay has to ask the user for one first.
    static bool regionNeedsSelection(const QVariantMap &options);

    /// The output and physical crop margins for a logical region. Returns
    /// nullopt when the region is not fully contained in one output.
    struct RegionCapture {
        QString output;
        QMargins margins;
        QSize streamSize;
    };
    static std::optional<RegionCapture> regionCaptureFor(const QRect &region, const QList<NiriIpc::Output> &outputs);

private Q_SLOTS:
    void handlePipeWireStreamAdded(uint nodeId);
    void handleScreencastClosed();

private:
    void startScreencastRecording(RecordingMode mode,
                                  Format format,
                                  bool includePointer,
                                  const QString &screenConnector,
                                  quint64 windowId,
                                  AudioMode audio,
                                  const QString &microphone,
                                  int bitrateKbps,
                                  const QMargins &cropMargins = {});
    // Asks niri which window to record. The pick runs asynchronously so the
    // GUI thread keeps repainting while `niri msg pick-window` waits for a
    // click, and it can be cancelled by finishRecording().
    void startWindowPick(const QUrl &fileUrl, const QVariantMap &options, bool includePointer);
    void handleWindowPickFinished(int exitCode, QProcess::ExitStatus status);
    void cancelWindowPick(const QString &message);
    void maybeStartGst();
    void stopScreencastSession();
    void failScreencastRecording(const QString &message);

    QProcess m_process;
    IdleInhibitor m_idleInhibitor;
    QProcess m_windowPickProcess;
    QByteArray m_stderr;
    QUrl m_fileUrl;
    QString m_screencastSessionPath;
    QString m_screencastStreamPath;
    Format m_pendingFormat = NoFormat;
    RecordingMode m_screencastMode = NoRecordingModes;
    AudioMode m_pendingAudioMode = AudioMode::None;
    QString m_pendingMicrophone;
    int m_pendingBitrateKbps = 10000;
    QMargins m_pendingCropMargins;
    bool m_gstRecording = false;
    bool m_screencastPending = false;
    quint64 m_screencastGeneration = 0;
    bool m_windowPickPending = false;
    quint64 m_windowPickGeneration = 0;
    QUrl m_windowPickFileUrl;
    QVariantMap m_windowPickOptions;
    bool m_windowPickIncludePointer = false;
    quint32 m_pendingNodeId = 0;
    quint64 m_pendingNodeGeneration = 0;
};

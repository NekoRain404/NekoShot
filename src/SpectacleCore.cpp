/*
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *  SPDX-FileCopyrightText: 2025 Jhair Paris <dev@jhairparis.com>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SpectacleCore.h"
#include "CaptureModeModel.h"
#include "CommandLineOptions.h"
#include "Config.h"
#include "ExportManager.h"
#include "Geometry.h"
#include "Gui/CaptureWindow.h"
#include "Gui/InlineMessageModel.h"
#include "Gui/Selection.h"
#include "Gui/SelectionEditor.h"
#include "Gui/SpectacleWindow.h"
#include "ImageMetaData.h"
#include "OcrManager.h"
#include "PlasmaVersion.h"
#include "StartupProfile.h"
#if WITH_X11
#include "Platforms/ImagePlatformXcb.h"
#endif
#include "Platforms/NiriPlatform.h"
#include "Platforms/PlatformLoader.h"
#include "RecordingModeModel.h"
#include "ShortcutActions.h"
// generated
#include "settings.h"

#include <KFormat>
#include <KGlobalAccel>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KMessageBox>
#include <KNotification>
#include <KStatusNotifierItem>
#include <KWindowSystem>
#if WITH_X11
#include <KX11Extras>
#endif
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>

#include <KLocalizedQmlContext>
#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QDrag>
#include <QKeySequence>
#include <QMetaObject>
#include <QMimeData>
#include <QMovie>
#include <QObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QtMath>
#include <qobjectdefs.h>

using namespace Qt::StringLiterals;

SpectacleCore *SpectacleCore::s_self = nullptr;
static std::unique_ptr<KStatusNotifierItem> s_systemTrayIcon;

static QList<KNotification *> notifications;

SpectacleCore::SpectacleCore(QObject *parent)
    : QObject(parent)
{
    StartupProfile::mark("core-ctor-enter");
    // Timer to prevent lots of extra rendering to images
    m_annotationSyncTimer = std::make_unique<QTimer>();
    m_annotationSyncTimer->setInterval(400);
    m_annotationSyncTimer->setSingleShot(true);

    m_delayAnimation = std::make_unique<QVariantAnimation>(this);
    m_delayAnimation->setStartValue(0.0);
    m_delayAnimation->setEndValue(1.0);
    m_delayAnimation->setDuration(1);
    m_delayAnimation->setCurrentTime(0);
    auto delayAnimation = m_delayAnimation.get();
    // We need to reset this on start in case a previous instance
    // didn't reset these before it closed or crashed.
    unityLauncherUpdate({{u"progress-visible"_s, false}, {u"progress"_s, 0}});
    using State = QVariantAnimation::State;
    auto onStateChanged = [this](State newState, State oldState) {
        Q_UNUSED(oldState)
        if (newState == State::Running) {
            unityLauncherUpdate({{u"progress-visible"_s, true}});
        } else if (newState == State::Stopped) {
            unityLauncherUpdate({{u"progress-visible"_s, false}});
            m_delayAnimation->setCurrentTime(0);
        }
    };
    auto onValueChanged = [this](const QVariant &value) {
        Q_EMIT captureTimeRemainingChanged();
        Q_EMIT captureProgressChanged();
        unityLauncherUpdate({{u"progress"_s, value.toReal()}});
        const auto windows = SpectacleWindow::instances();
        if (m_delayAnimation->state() != State::Stopped && !windows.isEmpty()) {
            if (captureTimeRemaining() <= 500 && windows.constFirst()->isVisible()) {
                SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
            }
            SpectacleWindow::setTitleForAll(SpectacleWindow::Timer);
        }
    };
    auto onFinished = [this]() {
        m_imagePlatform->doGrab(ImagePlatform::ShutterMode::Immediate, m_lastGrabMode, m_lastIncludePointer, m_lastIncludeDecorations, m_lastIncludeShadow);
    };
    QObject::connect(delayAnimation, &QVariantAnimation::stateChanged, this, onStateChanged, Qt::QueuedConnection);
    QObject::connect(delayAnimation, &QVariantAnimation::valueChanged, this, onValueChanged, Qt::QueuedConnection);
    QObject::connect(delayAnimation, &QVariantAnimation::finished, this, onFinished, Qt::QueuedConnection);

    m_imagePlatform = loadImagePlatform();
    StartupProfile::mark("core-ctor-image-platform");
    m_videoPlatform = loadVideoPlatform();
    StartupProfile::mark("core-ctor-video-platform");
    auto imagePlatform = m_imagePlatform.get();
    m_annotationDocument = std::make_unique<AnnotationDocument>();

    // essential connections
    connect(SelectionEditor::instance(), &SelectionEditor::accepted, this, [this](const QRectF &rect, const ExportManager::Actions &actions) {
        m_returnToViewer = m_startMode == StartMode::Gui;
        if (m_videoMode) {
            const auto captureWindows = CaptureWindow::instances();
            SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
            for (auto captureWindow : captureWindows) {
                // Destroy the QPlatformWindow so we can change the window behavior.
                // The QPlatformWindow will be recreated when the window is shown again.
                captureWindow->destroy();
                captureWindow->setFlag(Qt::WindowTransparentForInput, true);
                captureWindow->setFlag(Qt::WindowStaysOnTopHint, true);
                if (auto window = LayerShellQt::Window::get(captureWindow)) {
                    using namespace LayerShellQt;
                    window->setCloseOnDismissed(true);
                    window->setLayer(Window::LayerOverlay);
                    auto anchors = Window::Anchors::fromInt(Window::AnchorTop | Window::AnchorBottom | Window::AnchorLeft | Window::AnchorRight);
                    window->setAnchors(anchors);
                    // -1 means this window should not make way for other surfaces such as panels.
                    window->setExclusiveZone(-1);
                    window->setKeyboardInteractivity(Window::KeyboardInteractivityNone);
                    window->setScreen(captureWindow->screenToFollow());
                }
            }
            SpectacleWindow::setVisibilityForAll(QWindow::FullScreen);
            // deleteWindows();
            // showViewerIfGuiMode(true);
            bool includePointer = m_cliOptions[CommandLineOptions::Pointer];
            includePointer |= m_startMode != StartMode::Background && Settings::videoIncludePointer();
            ExportManager::instance()->updateTimestamp();
            const auto &output = m_outputUrl.isLocalFile() ? videoOutputUrl() : QUrl();
            static const auto rectKey = u"rect"_s;
            m_videoPlatform->startRecording(output, VideoPlatform::Region, {{rectKey, rect}}, includePointer);
        } else {
            SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
            deleteWindows();
            // The selection overlay works in logical global coordinates while
            // the annotation document works in image-local coordinates. The
            // captured image keeps its logical origin in its metadata, so
            // translate the selection before cropping. Without this, layouts
            // with a monitor left of / above the primary one would crop the
            // wrong area.
            const QImage &baseImage = m_annotationDocument->baseImage();
            QPointF imageOrigin;
            if (ImageMetaData::hasLogicalXY(baseImage)) {
                imageOrigin = ImageMetaData::logicalXY(baseImage);
            } else {
                // Producers that do not attach metadata position the image at
                // the top-left of the logical screen layout.
                imageOrigin = SelectionEditor::instance()->screensRect().topLeft();
            }
            m_annotationDocument->cropCanvas(rect.translated(-imageOrigin));
            syncExportImage();
            auto exportActions = actions & ExportManager::AnyAction ? actions : autoExportActions();
            if (m_ocrExportInProgress) {
                if (Settings::closeAfterOcr()) {
                    exportActions.setFlag(ExportManager::Action::Save, false);
                    exportActions.setFlag(ExportManager::Action::CopyPath, false);
                    m_quitAfterOcr = true;
                }
                exportActions.setFlag(ExportManager::Action::CopyImage, false);
                m_ocrExportInProgress = false;
            }
            const bool willQuit = exportActions.testAnyFlags(ExportManager::AnyAction) //
                && exportActions.testFlag(ExportManager::UserAction) //
                && Settings::quitAfterSaveCopyExport();
            m_returnToViewer &= !willQuit;
            showViewerIfGuiMode();
            SpectacleWindow::setTitleForAll(SpectacleWindow::Unsaved);
            ExportManager::instance()->scanQRCode();
            ExportManager::instance()->exportImage(exportActions, outputUrl());
        }
    });

    connect(imagePlatform, &ImagePlatform::newScreenshotTaken, this, [this](const QImage &image) {
        StartupProfile::mark("screenshot-received");
        m_grabInFlight = false;
        InlineMessageModel::instance()->clear();
        m_editExistingUrl.clear();
        m_cliOptions[CommandLineOptions::EditExisting] = false;
        m_annotationDocument->clearAnnotations();
        m_annotationDocument->setBaseImage(image);
        setExportImage(image);
        ExportManager::instance()->updateTimestamp();
        m_returnToViewer = true;
        showViewerIfGuiMode();
        SpectacleWindow::setTitleForAll(SpectacleWindow::Unsaved);
        // QR results are only consumed by the viewer; background/DBus captures
        // would burn a thread scanning an image nobody sees.
        if (m_startMode == StartMode::Gui) {
            StartupProfile::mark("scan-qr-start");
            ExportManager::instance()->scanQRCode();
            StartupProfile::mark("scan-qr-return");
        }
        ExportManager::instance()->exportImage(autoExportActions(), outputUrl());
        StartupProfile::mark("export-image-call-return");
        setVideoMode(false);
    });
    connect(imagePlatform, &ImagePlatform::newCroppableScreenshotTaken, this, [this](const QImage &image) {
        m_grabInFlight = false;
        InlineMessageModel::instance()->clear();
        m_editExistingUrl.clear();
        m_cliOptions[CommandLineOptions::EditExisting] = false;
        setVideoMode(false);
        m_annotationDocument->clearAnnotations();
        m_annotationDocument->setBaseImage(image);
        setExportImage(image);
        ExportManager::instance()->updateTimestamp();
        initCaptureWindows(CaptureWindow::Image);
        SpectacleWindow::setTitleForAll(SpectacleWindow::Unsaved);
        SpectacleWindow::setVisibilityForAll(QWindow::FullScreen);
        // SelectionEditor::reset has to be delayed until after all the DPRs are correct on Wayland.
        connect(m_captureWindows.front().get(),
                &CaptureWindow::allExposed, //
                SelectionEditor::instance(),
                &SelectionEditor::reset,
                Qt::SingleShotConnection);
    });

    // The behavior happens to be very similar right now, so we use a shared base function
    auto onScreenshotOrRecordingFailed = [this](const QString &message, QString uiMessage, void (SpectacleCore::*dBusFailedSignal)(const QString &)) {
        m_exitCode = 1;
        if (!message.isEmpty()) {
            uiMessage = uiMessage % u"\n"_s % message;
        }
        const auto &windows = qGuiApp->allWindows();
        const bool hasVisibleWindow = std::any_of(windows.cbegin(), windows.cend(), [](auto *window) {
            // Is visible and is a normal window or dialog (no tooltips or menus)
            const auto typeFlags = window->flags() & Qt::WindowType_Mask;
            return window->isVisible() && (typeFlags == Qt::Window || typeFlags == Qt::Dialog);
        });
        switch (m_startMode) {
        case StartMode::Background:
            showErrorMessage(uiMessage);
            if (!hasVisibleWindow) {
                Q_EMIT allDone();
            }
            return;
        case StartMode::DBus: {
            showErrorMessage(uiMessage);
            Q_EMIT(this->*dBusFailedSignal)(message);
            if (!hasVisibleWindow) {
                Q_EMIT allDone();
            }
            return;
        }
        case StartMode::Gui: {
            if (!ViewerWindow::instance()) {
                InlineMessageModel::instance()->clear();
                if (hasVisibleWindow) {
                    showErrorMessage(uiMessage);
                    return;
                } else {
                    initViewerWindow(ViewerWindow::Dialog);
                }
            }
            qWarning().noquote() << message;
            auto viewer = ViewerWindow::instance();
            viewer->setVisible(true);
            InlineMessageModel::instance()->push(InlineMessageModel::Error, uiMessage);
            return;
        }
        }
    };
    connect(imagePlatform, &ImagePlatform::newScreenshotFailed, this, [this, onScreenshotOrRecordingFailed](const QString message) {
        m_grabInFlight = false;
        auto uiMessage = i18nc("@info", "An error occurred while taking a screenshot.");
        onScreenshotOrRecordingFailed(message, uiMessage, &SpectacleCore::dbusScreenshotFailed);
    });
    connect(imagePlatform, &ImagePlatform::newScreenshotCanceled, this, [this]() {
        m_grabInFlight = false;
        if (m_startMode != StartMode::Gui || !m_returnToViewer || isGuiNull()) {
            finishNonGuiCapture();
            return;
        }
        SpectacleWindow::setTitleForAll(SpectacleWindow::Previous);
        const auto windows = SpectacleWindow::instances();
        if (windows.empty()) {
            initViewerWindow(ViewerWindow::Viewer);
            return;
        }
        for (auto w : windows) {
            w->setVisible(true);
        }
    });

    auto videoPlatform = m_videoPlatform.get();
    connect(videoPlatform, &VideoPlatform::recordingStateChanged, this, [this, videoPlatform](VideoPlatform::RecordingState state) {
        if (state == VideoPlatform::RecordingState::Recording) {
            static const auto recordingIcon = u":/icons/256-status-media-recording.webp"_s;
            static const auto recordingStartedIcon = u":/icons/256-status-media-recording-started.webp"_s;
            static const auto recordingPulseIcon = u":/icons/256-status-media-recording-pulse.webp"_s;
            s_systemTrayIcon = std::make_unique<KStatusNotifierItem>();
            s_systemTrayIcon->setStatus(KStatusNotifierItem::Active);
            s_systemTrayIcon->setCategory(KStatusNotifierItem::SystemServices);
            s_systemTrayIcon->setToolTipTitle(videoPlatform->isRecordingAudio()
                                                  ? i18nc("@info:tooltip title for recording tray icon", "Spectacle is Recording with Sound")
                                                  : i18nc("@info:tooltip title for recording tray icon", "Spectacle is Recording"));
            s_systemTrayIcon->setStandardActionsEnabled(false);
            connect(s_systemTrayIcon.get(), &KStatusNotifierItem::activateRequested, this, [] {
                SpectacleCore::instance()->finishRecording();
            });
            const auto messageTitle = videoPlatform->isRecordingAudio() ? i18nc("recording notification title", "Spectacle is Recording with Sound")
                                                                        : i18nc("recording notification title", "Spectacle is Recording");
            QKeySequence stopShortcut;
            if (NiriPlatform::hasGlobalShortcuts()) {
                auto getSimpleDefaultShortcut = [] {
                    const auto shortcuts = KGlobalAccel::self()->shortcut(ShortcutActions::self()->recordRegionAction());
                    if (shortcuts.contains(QKeySequence{Qt::META | Qt::Key_R})) {
                        return QKeySequence{Qt::META | Qt::Key_R};
                    }
                    return QKeySequence{};
                };
                auto getShortcut = [](const auto &list) {
                    auto it = std::find_if(list.cbegin(), list.cend(), [](const QKeySequence &shortcut) {
                        return !shortcut.isEmpty();
                    });
                    return it != list.cend() ? *it : QKeySequence{};
                };
                stopShortcut = getSimpleDefaultShortcut();
                if (stopShortcut.isEmpty()) {
                    auto mode = m_videoPlatform->recordingMode();
                    if (mode == VideoPlatform::Screen) {
                        stopShortcut = getShortcut(KGlobalAccel::self()->shortcut(ShortcutActions::self()->recordScreenAction()));
                    } else if (mode == VideoPlatform::Window) {
                        stopShortcut = getShortcut(KGlobalAccel::self()->shortcut(ShortcutActions::self()->recordWindowAction()));
                    } else if (mode == VideoPlatform::Region) {
                        stopShortcut = getShortcut(KGlobalAccel::self()->shortcut(ShortcutActions::self()->recordRegionAction()));
                    }
                }
            }
            const auto messageBody = stopShortcut.isEmpty()
                ? i18nc("recording notification message without shortcut", "To finish the recording, click the pulsing red System Tray icon.")
                : xi18nc("recording notification message with shortcut",
                         "To finish the recording, click the pulsing red System Tray icon or press <shortcut>%1</shortcut>.",
                         stopShortcut.toString(QKeySequence::NativeText));
            auto notification = new KNotification(u"recordingStarted"_s, KNotification::CloseOnTimeout, this);
            notification->setTitle(messageTitle);
            notification->setText(messageBody);
            // Whether the notification is transient and should not be kept in history.
            notification->setHint(u"transient"_s, true);
            // Can't set notification duration with KNotification directly.
            // Also see https://bugs.kde.org/show_bug.cgi?id=503838
            QTimer::singleShot(4000, notification, &KNotification::close);
            connect(m_videoPlatform.get(), &VideoPlatform::recordingStateChanged, notification, [notification](VideoPlatform::RecordingState state) {
                if (state != VideoPlatform::RecordingState::Recording) {
                    notification->close();
                }
            });
            notification->sendEvent();
            if (!QMovie::supportedFormats().contains("webp"_ba)) {
                const auto messageTitle = i18nc("missing webp support notification title", "WebP support is missing.");
                const auto messageBody = i18nc("missing webp support notification message",
                                               "Please install Qt Image Formats to get animated system tray icons for Spectacle, and then report this "
                                               "packaging issue to your distributor.");
                s_systemTrayIcon->showMessage(messageTitle, messageBody, u"dialog-warning"_s, 4000);
                s_systemTrayIcon->setIconByName(u"media-record"_s);
                return;
            }
            static const auto initPulseAnimation = [] {
                auto animation = new QMovie(recordingPulseIcon, {}, s_systemTrayIcon.get());
                animation->setCacheMode(QMovie::CacheAll);
                connect(animation, &QMovie::frameChanged, animation, [animation] {
                    s_systemTrayIcon->setIconByPixmap(animation->currentPixmap());
                });
                // We periodically switch to a static image instead of having a period in the
                // animation where nothing happens to be a bit more efficient.
                // NOTE: If QMovie::finished isn't working,
                // you probably edited the icon and forgot to set the loop count.
                connect(animation, &QMovie::finished, animation, [animation] {
                    animation->stop();
                    static const auto recordingPixmap = QPixmap{recordingIcon};
                    s_systemTrayIcon->setIconByPixmap(recordingPixmap);
                    const auto animationDuration = animation->nextFrameDelay() * animation->frameCount() / (animation->speed() / 100.0);
                    QTimer::singleShot(animationDuration / 2, animation, &QMovie::start);
                });
                animation->start();
            };
            auto startedAnimation = new QMovie(recordingStartedIcon, {}, s_systemTrayIcon.get());
            startedAnimation->setCacheMode(QMovie::CacheAll);
            connect(startedAnimation, &QMovie::frameChanged, startedAnimation, [startedAnimation] {
                s_systemTrayIcon->setIconByPixmap(startedAnimation->currentPixmap());
            });
            connect(startedAnimation, &QMovie::finished, startedAnimation, [startedAnimation] {
                startedAnimation->stop();
                startedAnimation->deleteLater();
                initPulseAnimation();
            });
            startedAnimation->start();
        } else if (state == VideoPlatform::RecordingState::Rendering && s_systemTrayIcon) {
            const auto messageTitle = i18nc("recording notification title", "Spectacle is Finishing the Recording");
            const auto messageBody = i18nc("recording notification message", "Please wait");
            s_systemTrayIcon->setToolTipTitle(i18nc("@info:tooltip title for rendering tray icon", //
                                                    "Spectacle is Finishing the Recording"));
            auto subtitle = i18nc("@info:tooltip subtitle for rendering tray icon", //
                                  "Time recorded: %1\n" //
                                  "Click to stop rendering early (this will lose data)",
                                  recordedTime());
            auto notification = new KNotification(u"recordingSaveInProgress"_s, KNotification::CloseOnTimeout, this);
            notification->setTitle(messageTitle);
            notification->setText(messageBody);
            notification->setHint(u"transient"_s, true);
            connect(m_videoPlatform.get(), &VideoPlatform::recordingStateChanged, notification, [notification](VideoPlatform::RecordingState state) {
                if (state != VideoPlatform::RecordingState::Rendering) {
                    notification->close();
                }
            });
            notification->sendEvent();
            s_systemTrayIcon->setToolTipSubTitle(subtitle);
        } else {
            s_systemTrayIcon.reset();
            m_captureWindows.clear();
        }
    });
    connect(videoPlatform, &VideoPlatform::recordedTimeChanged, this, [this, videoPlatform] {
        Q_EMIT recordedTimeChanged();
        if (!s_systemTrayIcon || videoPlatform->recordingState() != VideoPlatform::RecordingState::Recording) {
            return;
        }
        auto subtitle = i18nc("@info:tooltip subtitle for recording tray icon", //
                              "Time recorded: %1\n" //
                              "Click to finish recording",
                              recordedTime());
        s_systemTrayIcon->setToolTipSubTitle(subtitle);
    });
    connect(videoPlatform, &VideoPlatform::recordingSaved, this, [this](const QUrl &fileUrl) {
        resetVideoModeAfterRecording();
        // Always try to save. Needed to move recordings out of temp dir.
        ExportManager::instance()->exportVideo(autoExportActions() | ExportManager::Save, fileUrl, videoOutputUrl());
        m_recordingEventLoopLocker.reset();
    });
    connect(videoPlatform, &VideoPlatform::recordingCanceled, this, [this] {
        resetVideoModeAfterRecording();
        m_recordingEventLoopLocker.reset();
        if (m_startMode != StartMode::Gui || !m_returnToViewer || isGuiNull()) {
            Q_EMIT allDone();
            return;
        }
        SpectacleWindow::setTitleForAll(SpectacleWindow::Previous);
        const auto windows = SpectacleWindow::instances();
        if (windows.empty()) {
            initViewerWindow(ViewerWindow::Viewer);
            return;
        }
        for (auto w : windows) {
            w->setVisible(true);
        }
    });
    connect(videoPlatform, &VideoPlatform::recordingFailed, this, [this, onScreenshotOrRecordingFailed](const QString &message) {
        resetVideoModeAfterRecording();
        m_recordingEventLoopLocker.reset();
        auto uiMessage = i18nc("@info", "An error occurred while attempting to record the screen.");
        onScreenshotOrRecordingFailed(message, uiMessage, &SpectacleCore::dbusRecordingFailed);
    });
    connect(videoPlatform, &VideoPlatform::regionRequested, this, [this] {
        initCaptureWindows(CaptureWindow::Video);
        SpectacleWindow::setTitleForAll(SpectacleWindow::Unsaved);
        SpectacleWindow::setVisibilityForAll(QWindow::FullScreen);
        // SelectionEditor::reset has to be delayed until after all the DPRs are correct on Wayland.
        connect(m_captureWindows.front().get(),
                &CaptureWindow::allExposed, //
                SelectionEditor::instance(),
                &SelectionEditor::reset,
                Qt::SingleShotConnection);
    });

    // set up the export manager
    auto exportManager = ExportManager::instance();
    auto onImageExported = [this](const ExportManager::Actions &actions, const QUrl &url) {
        if (actions & ExportManager::AnySave) {
            m_editExistingUrl = url;
            m_outputUrl.clear();
        }
        if (actions & ExportManager::UserAction && Settings::quitAfterSaveCopyExport()) {
            deleteWindows();
        }

        if (isGuiNull()) {
            if (m_cliOptions[CommandLineOptions::NoNotify]) {
                // if we notify, we Q_EMIT allDone only if the user either dismissed the notification or pressed
                // the "Open" button, otherwise the app closes before it can react to it.
                if (actions & ExportManager::CopyImage) {
                    // Allow some time for clipboard content to transfer if '--nonotify' is used, see Bug #411263
                    // TODO: Find better solution
                    QTimer::singleShot(250, this, &SpectacleCore::allDone);
                } else {
                    Q_EMIT allDone();
                }
            } else {
                doNotify(ScreenCapture::Screenshot, actions, url);
            }
            return;
        }

        auto viewerWindow = ViewerWindow::instance();
        if (!viewerWindow) {
            return;
        }

        if (actions & ExportManager::AnySave) {
            SpectacleWindow::setTitleForAll(SpectacleWindow::Saved, url.fileName());
            if (actions & ExportManager::CopyImage) {
                auto text =
                    xi18nc("@info", "The screenshot was copied to the clipboard and saved as <link url=\"%1\">%2</link>", url.toString(), url.fileName());
                InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
            } else if (actions & ExportManager::CopyPath) {
                auto text = xi18nc("@info",
                                   "The screenshot has been saved as <link url=\"%1\">%2</link> and its location has been copied to clipboard",
                                   url.toString(),
                                   url.fileName());
                InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
            } else {
                auto text = xi18nc("@info", "The screenshot was saved as <link url=\"%1\">%2</link>", url.toString(), url.fileName());
                InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
            }
        } else if (actions & ExportManager::CopyImage) {
            auto text = i18nc("@info", "The screenshot has been copied to the clipboard.");
            InlineMessageModel::instance()->push(InlineMessageModel::Copied, text);
        }
    };
    connect(exportManager, &ExportManager::imageExported, this, onImageExported);
    auto onVideoExported = [this](const ExportManager::Actions &actions, const QUrl &url) {
        setCurrentVideo(url);

        if (actions & ExportManager::UserAction && Settings::quitAfterSaveCopyExport()) {
            deleteWindows();
        } else if (!ViewerWindow::instance()) {
            showViewerIfGuiMode();
        }

        if (isGuiNull()) {
            if (m_cliOptions[CommandLineOptions::NoNotify]) {
                Q_EMIT allDone();
            } else {
                doNotify(ScreenCapture::Recording, actions, url);
            }
            return;
        }

        auto viewerWindow = ViewerWindow::instance();
        if (!viewerWindow) {
            return;
        }

        if (actions & ExportManager::AnySave) {
            SpectacleWindow::setTitleForAll(SpectacleWindow::Saved, url.fileName());
            if (actions & ExportManager::CopyPath) {
                auto text = xi18nc("@info",
                                   "The video has been saved as <link url=\"%1\">%2</link> and its location has been copied to clipboard",
                                   url.toString(),
                                   url.fileName());
                InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
            } else {
                auto text = xi18nc("@info", "The video was saved as <link url=\"%1\">%2</link>", url.toString(), url.fileName());
                InlineMessageModel::instance()->push(InlineMessageModel::Saved, text, url);
            }
        } else if (actions & ExportManager::CopyPath) {
            auto text = i18nc("@info", "The video location has been copied to the clipboard.");
            InlineMessageModel::instance()->push(InlineMessageModel::Copied, text);
        }
    };
    connect(exportManager, &ExportManager::videoExported, this, onVideoExported);

    auto onQRCodeScanned = [](const QVariant &result) {
        auto viewerWindow = ViewerWindow::instance();
        if (!viewerWindow) {
            return;
        }
        // Thanks to: https://stackoverflow.com/questions/1500260/detect-urls-in-text-with-javascript
        using QRE = QRegularExpression;
        static const QRE urlRegex(u"(\\b(https?|ftp|file)://[-A-Z0-9+&@#/%?=~_|!:,.;]*[-A-Z0-9+&@#/%=~_|])"_s, QRE::CaseInsensitiveOption);
        static const auto linkifier = u"<a href=\"\\1\">\\1</a>"_s;
        auto text = [](const QVariant &result) -> QString {
            if (result.typeId() == QMetaType::QString) {
                // Not using xi18nc because it doesn't work with the link replacement logic.
                // Also see https://invent.kde.org/graphics/spectacle/-/merge_requests/432#note_1111125
                auto text = i18nc("@info", //
                                  "Code found: %1",
                                  result.toString().toHtmlEscaped().replace(urlRegex, linkifier));
                return u"<html>" % text % u"</html>";
            }
            return i18nc("@info", "Found QR code with binary content.");
        }(result);
        InlineMessageModel::instance()->push(InlineMessageModel::Scanned, text, result);
    };
    connect(exportManager, &ExportManager::qrCodeScanned, this, onQRCodeScanned);

    auto onOcrTextRecognized = [this](const QString &text, const QStringList &languageCodes, bool success) {
        if (!success) {
            InlineMessageModel::instance()->push(InlineMessageModel::Error, i18nc("@info", "Text extraction failed"));
            return;
        }

        if (text.isEmpty()) {
            InlineMessageModel::instance()->push(InlineMessageModel::Copied, i18nc("@info", "No text found in the image"));
            return;
        }

        InlineMessageModel::instance()->push(InlineMessageModel::Copied, i18nc("@info", "Text extraction completed"));

        // ensure program stays alive until the notification finishes.
        if (!m_eventLoopLocker) {
            m_eventLoopLocker = std::make_unique<QEventLoopLocker>();
        }

        auto notification = new KNotification(u"ocrTextExtracted"_s, KNotification::CloseOnTimeout, nullptr);
        notification->setTitle(i18nc("@info:notification title", "Text Extracted"));

        notifications.append(notification);

        auto ocrManager = OcrManager::instance();
        auto languageNames = ocrManager->availableLanguagesWithNames();

        QStringList displayLanguages;
        for (const QString &code : languageCodes) {
            QString displayName = languageNames.value(code, code);
            if (!displayLanguages.contains(displayName)) {
                displayLanguages.append(displayName);
            }
        }

        QString languagesText;
        if (displayLanguages.size() == 1) {
            languagesText = displayLanguages.first();
        } else if (displayLanguages.size() == 2) {
            languagesText =
                i18nc("@info The variables are language names, e.g. 'English and German'", "%1 and %2", displayLanguages.at(0), displayLanguages.at(1));
        } else {
            languagesText = displayLanguages.join(u", "_s);
        }

        auto notificationText = xi18nc("@info:notification", "Text copied to clipboard.<nl/>Languages used: %1", languagesText);
        notification->setText(notificationText);
        notification->setIconName(u"document-scan"_s);

        if (!text.isEmpty()) {
            auto openEditorAction = notification->addAction(i18nc("@action:button", "Open in Text Editor"));
            connect(openEditorAction, &KNotificationAction::activated, this, [text, notification]() {
                // Create temporary file with extracted text
                auto exportManager = ExportManager::instance();
                exportManager->updateTimestamp();
                auto timestamp = exportManager->timestamp();

                QString filename = QStringLiteral("spectacle_ocr_%1.txt").arg(timestamp.toString(QStringLiteral("yyyyMMdd_HHmmss")));
                QString templatePath = QDir::tempPath() + QStringLiteral("/") + filename;

                QTemporaryFile tempFile;
                tempFile.setFileTemplate(templatePath);
                tempFile.setAutoRemove(false);

                if (tempFile.open()) {
                    QTextStream stream(&tempFile);
                    stream << text;
                    tempFile.close();

                    auto job = new KIO::OpenUrlJob(QUrl::fromLocalFile(tempFile.fileName()));
                    job->setStartupId(notification->xdgActivationToken().toUtf8());
                    job->setDeleteTemporaryFile(true);
                    job->start();
                }

                notification->close();
            });
        }

        auto onExpired = [this, notification] {
            notifications.removeOne(static_cast<KNotification *>(notification));

            if (notifications.empty() && m_eventLoopLocker) {
                QTimer::singleShot(250, this, [this] {
                    m_eventLoopLocker.reset();
                });
            }
        };
        connect(notification, &QObject::destroyed, this, onExpired);
        // We keep the application running for a while to ensure the notification action remains available in the history.
        // See: https://bugs.kde.org/show_bug.cgi?id=514434
        QTimer::singleShot(180000, notification, onExpired);

        notification->sendEvent();

        if (m_quitAfterOcr) {
            m_quitAfterOcr = false;
            deleteWindows();
        }
    };

    // Connect to OCR manager
    connect(OcrManager::instance(), &OcrManager::textRecognized, this, onOcrTextRecognized);
    connect(OcrManager::instance(), &OcrManager::statusChanged, this, [this](OcrManager::OcrStatus) {
        Q_EMIT ocrStatusChanged();
    });

    connect(exportManager, &ExportManager::errorMessage, this, &SpectacleCore::showErrorMessage);

    connect(m_annotationDocument.get(), &AnnotationDocument::repaintNeeded, m_annotationSyncTimer.get(), qOverload<>(&QTimer::start));
    connect(
        m_annotationSyncTimer.get(),
        &QTimer::timeout,
        this,
        [this] {
            auto image = m_annotationDocument->renderToImage();
            auto canvasPos = m_annotationDocument->canvasRect().topLeft();
            ImageMetaData::setLogicalXY(image, canvasPos.x(), canvasPos.y());
            ExportManager::instance()->setImage(image);
        },
        Qt::QueuedConnection); // QueuedConnection to help prevent making the visible render lag.

    if (NiriPlatform::hasGlobalShortcuts()) {
        // set up shortcuts
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->openAction(),
                                                QList<QKeySequence>{
                                                    Qt::Key_Print,
                                                    // Default screenshot shortcut on Windows.
                                                    // Also for keyboards without a print screen key.
                                                    Qt::META | Qt::SHIFT | Qt::Key_S,
                                                });
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->fullScreenAction(), Qt::SHIFT | Qt::Key_Print);
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->activeWindowAction(), Qt::META | Qt::Key_Print);
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->windowUnderCursorAction(), Qt::META | Qt::CTRL | Qt::Key_Print);
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->regionAction(), Qt::META | Qt::SHIFT | Qt::Key_Print);
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->currentScreenAction(), QList<QKeySequence>());
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->openWithoutScreenshotAction(), QList<QKeySequence>());
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->recordScreenAction(), Qt::META | Qt::ALT | Qt::Key_R);
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->recordWindowAction(), Qt::META | Qt::CTRL | Qt::Key_R);
        KGlobalAccel::self()->setGlobalShortcut(ShortcutActions::self()->recordRegionAction(),
                                                QList<QKeySequence>{
                                                    // Similar to region screenshot
                                                    Qt::META | Qt::SHIFT | Qt::Key_R,
                                                    // Also use Meta+R for now
                                                    Qt::META | Qt::Key_R,
                                                });
    }

    connect(qApp, &QApplication::screenRemoved, this, [this](QScreen *screen) {
        // It's dangerous to erase from within a for loop, so we use std::find_if
        auto hasScreen = [screen](const CaptureWindow::UniquePointer &window) {
            return window->screen() == screen;
        };
        auto it = std::find_if(m_captureWindows.begin(), m_captureWindows.end(), hasScreen);
        if (it != m_captureWindows.end()) {
            m_captureWindows.erase(it);
        }
    });
    StartupProfile::mark("core-ctor-end");
}

bool SpectacleCore::ocrAvailable() const
{
    return OcrManager::instance()->isAvailable();
}

OcrManager::OcrStatus SpectacleCore::ocrStatus() const
{
    return OcrManager::instance()->status();
}

QVariantMap SpectacleCore::ocrAvailableLanguages() const
{
    auto ocrManager = OcrManager::instance();
    if (!ocrManager->isAvailable()) {
        return QVariantMap();
    }

    auto languageMap = ocrManager->availableLanguagesWithNames();
    QVariantMap result;
    for (auto it = languageMap.constBegin(); it != languageMap.constEnd(); ++it) {
        result[it.key()] = it.value();
    }
    return result;
}

bool SpectacleCore::startOcrExtraction(const QString &languageCode)
{
    if (m_videoMode) {
        return false;
    }

    const bool hasCaptureWindows = !CaptureWindow::instances().isEmpty();

    if (hasCaptureWindows) {
        auto selectionEditor = SelectionEditor::instance();
        auto inlineMessages = InlineMessageModel::instance();

        m_ocrExportInProgress = true;
        if (!selectionEditor->acceptSelection(ExportManager::UserAction)) {
            m_ocrExportInProgress = false;
            inlineMessages->push(InlineMessageModel::Error, i18nc("@info", "Please select a region before extracting text"));
            return false;
        }

        QMetaObject::invokeMethod(
            this,
            [this, languageCode]() {
                performOcrExtraction(languageCode);
            },
            Qt::QueuedConnection);
        return true;
    }

    return performOcrExtraction(languageCode);
}

bool SpectacleCore::performOcrExtraction(const QString &languageCode)
{
    auto ocrManager = OcrManager::instance();
    auto inlineMessages = InlineMessageModel::instance();

    if (!ocrManager->isAvailable()) {
        inlineMessages->push(InlineMessageModel::Error, i18nc("@info", "OCR is not available."));
        return false;
    }

    const QImage image = m_annotationDocument->renderToImage();
    if (image.isNull()) {
        inlineMessages->push(InlineMessageModel::Error, i18nc("@info", "No screenshot available."));
        return false;
    }

    inlineMessages->push(InlineMessageModel::Copied, i18nc("@info", "Extracting text from image..."));

    if (languageCode.isEmpty()) {
        ocrManager->recognizeText(image);
    } else {
        ocrManager->recognizeTextWithLanguage(image, languageCode);
    }

    return true;
}

SpectacleCore::~SpectacleCore() noexcept
{
    // The tray icon is a static so the recording notification survives window
    // teardown. Destroy it while QCoreApplication is still alive: its
    // QEventLoopLocker dereferences the application at destruction, and a
    // static destroyed after main() would crash.
    s_systemTrayIcon.reset();
    s_self = nullptr;
}

SpectacleCore *SpectacleCore::instance()
{
    if (!s_self) {
        s_self = new SpectacleCore(qApp);
    }

    return s_self;
}

ImagePlatform *SpectacleCore::imagePlatform() const
{
    return m_imagePlatform.get();
}

VideoPlatform *SpectacleCore::videoPlatform() const
{
    return m_videoPlatform.get();
}

AnnotationDocument *SpectacleCore::annotationDocument() const
{
    return m_annotationDocument.get();
}

QUrl SpectacleCore::screenCaptureUrl() const
{
    return m_screenCaptureUrl;
}

void SpectacleCore::setScreenCaptureUrl(const QUrl &url)
{
    if (m_screenCaptureUrl == url) {
        return;
    }
    m_screenCaptureUrl = url;
    Q_EMIT screenCaptureUrlChanged();
}

void SpectacleCore::setScreenCaptureUrl(const QString &filePath)
{
    if (QDir::isRelativePath(filePath)) {
        setScreenCaptureUrl(QUrl::fromUserInput(QDir::current().absoluteFilePath(filePath)));
    } else {
        setScreenCaptureUrl(QUrl::fromUserInput(filePath));
    }
}

QUrl SpectacleCore::outputUrl() const
{
    return m_outputUrl.isEmpty() ? m_editExistingUrl : m_outputUrl;
}

int SpectacleCore::captureTimeRemaining() const
{
    int totalDuration = m_delayAnimation->totalDuration();
    int currentTime = m_delayAnimation->currentTime();
    return currentTime > totalDuration || m_delayAnimation->state() == QVariantAnimation::Stopped ? 0 : totalDuration - currentTime;
}

qreal SpectacleCore::captureProgress() const
{
    // using currentValue() sometimes gives 1.0 when we don't want it.
    return m_delayAnimation->state() == QVariantAnimation::Stopped ? 0 : m_delayAnimation->currentValue().toReal();
}

void SpectacleCore::activate(const QStringList &arguments, const QString &workingDirectory)
{
    StartupProfile::mark("activate-enter");
    if (!workingDirectory.isEmpty()) {
        QDir::setCurrent(workingDirectory);
    }

    // We can't re-use QCommandLineParser instances, it preserves earlier parsed values
    QCommandLineParser parser;
    parser.addOptions(CommandLineOptions::self()->allOptions);
    parser.parse(arguments);

    // --daemon is a startup-only flag: activate() runs again for every D-Bus
    // activation, so once daemon mode is enabled it must stay enabled. Each
    // activation still honors its own CLI flags; a forwarded request must do
    // what the user asked for, just without making the process one-shot.
    // Record it before the --stop-recording branch below: a fresh
    // `nekoshot --daemon --stop-recording` must become an idle resident, since
    // Main has already skipped the allDone->quit connection for it.
    if (parser.isSet(CommandLineOptions::self()->daemon)) {
        m_daemonMode = true;
    }

    // --stop-recording is a control request, not a capture: handle it before
    // anything else so it can never start a screenshot or a new recording.
    if (parser.isSet(CommandLineOptions::self()->stopRecording)) {
        if (m_videoPlatform->isRecordingOrStarting()) {
            // Stopping is asynchronous: the encoder still has to flush its
            // container. Let recordingSaved/recordingCanceled/recordingFailed
            // drive allDone, otherwise a one-shot CLI process quits before the
            // file is finalized and leaves an MP4 without its moov atom.
            finishRecording();
            return;
        }
        // A one-shot `nekoshot --stop-recording` reports completion and exits;
        // a resident daemon keeps waiting for later requests.
        if (!m_daemonMode) {
            Q_EMIT allDone();
        }
        return;
    }

    if (m_videoPlatform->isRecordingOrStarting()) {
        // A duplicate recording request must not tear down the recording that
        // is already running (or still waiting for its first frame). The
        // backend rejects duplicate starts too, but this activation path would
        // otherwise stop the previous recording before the backend sees it.
        if (parser.isSet(CommandLineOptions::self()->record)) {
            qWarning().noquote() << i18nc("@info:shell", "Ignoring activation: a recording is already in progress");
            return;
        }
        // BUG: https://bugs.kde.org/show_bug.cgi?id=481471
        // TODO: find a way to support screenshot shortcuts while recording?
        finishRecording();
        return;
    }

    if (m_grabInFlight) {
        // A delay, grab, picker, or selection overlay is already pending.
        // A forwarded activation must not tear down its windows or start a
        // second grab; ignore it and let the active flow finish.
        qWarning().noquote() << "Ignoring activation: a screenshot is already in progress";
        return;
    }

    // Collect parsed command line options
    using Option = CommandLineOptions::Option;
    m_cliOptions.fill(false); // reset all values to false
    int optionsToCheck = parser.optionNames().size();
    for (int i = 0; optionsToCheck > 0 && i < CommandLineOptions::self()->allOptions.size(); ++i) {
        m_cliOptions[i] = parser.isSet(CommandLineOptions::self()->allOptions[i]);
        if (m_cliOptions[i]) {
            --optionsToCheck;
        }
    }

    // Determine start mode
    m_startMode = StartMode::Gui; // Default to Gui
    // Gui is an option that's normally useless since it's the default mode.
    // Make it override the other modes if explicitly set, using the launchonly option,
    // or editing an existing image. Editing an existing image requires a viewer window.
    if (!m_cliOptions[Option::Gui] && !m_cliOptions[Option::LaunchOnly] && !m_cliOptions[Option::EditExisting]) {
        // Background gets precedence over DBus
        if (m_cliOptions[Option::Background]) {
            m_startMode = StartMode::Background;
        } else if (m_cliOptions[Option::DBus]) {
            m_startMode = StartMode::DBus;
        }
    }

    // If any capture mode is given in the cli options, let it override
    // the setting to not take a screenshot on launch
    // clang-format off
    const bool captureModeFromCli =
        m_cliOptions[Option::Fullscreen] ||
        m_cliOptions[Option::Current] ||
        m_cliOptions[Option::ActiveWindow] ||
        m_cliOptions[Option::WindowUnderCursor] ||
        m_cliOptions[Option::Region] ||
        m_cliOptions[Option::Record];
    // clang-format on

    bool daemonLaunchCapture = false;
    if (m_daemonMode) {
        m_returnToViewer = false;
        const bool onlyDaemonFlag = m_cliOptions[Option::Daemon] && !m_cliOptions[Option::Gui] && !m_cliOptions[Option::LaunchOnly]
            && !m_cliOptions[Option::Background] && !m_cliOptions[Option::DBus] && !captureModeFromCli;
        if (onlyDaemonFlag) {
            // Initial `--daemon` startup: wait for D-Bus requests.
            m_startMode = StartMode::DBus;
        } else if (m_cliOptions[Option::Gui] || m_cliOptions[Option::LaunchOnly]) {
            // Explicit GUI request: open the interface or preview.
            m_startMode = StartMode::Gui;
        } else if (m_cliOptions[Option::Background]) {
            // Background semantics, including -o/--output.
            m_startMode = StartMode::Background;
        } else if (m_cliOptions[Option::DBus] || m_cliOptions[Option::Record]) {
            // Explicit `-s`/`--dbus` and forwarded recording stay headless.
            m_startMode = StartMode::DBus;
        } else if (m_cliOptions[Option::Fullscreen] || m_cliOptions[Option::Current] || m_cliOptions[Option::ActiveWindow]
                   || m_cliOptions[Option::WindowUnderCursor] || m_cliOptions[Option::Region]) {
            // Forwarded screenshot flags mirror a cold `nekoshot -r`: overlay,
            // selection, then viewer/annotation. The process stays resident.
            m_startMode = StartMode::Gui;
            m_returnToViewer = true;
        } else if (Settings::launchAction() == Settings::DoNotTakeScreenshot) {
            m_startMode = StartMode::Gui;
        } else {
            // Forwarded invocation without flags: follow the launch action with
            // GUI semantics so it also opens the viewer after the capture.
            m_startMode = StartMode::Gui;
            m_returnToViewer = true;
            daemonLaunchCapture = true;
        }
    }

    StartupProfile::mark("activate-parsed");
    if (parser.optionNames().size() > 0 || daemonLaunchCapture || m_startMode != StartMode::Gui || !m_returnToViewer) {
        // Delete windows if we have CLI options or not in GUI mode.
        // We don't want to delete them otherwise because that will mess with the
        // settings for PrintScreen key behavior.
        deleteWindows();
    }

    // reset last region if it should not be remembered across restarts
    if (!(Settings::rememberSelectionRect() == Settings::EnumRememberSelectionRect::Always)) {
        Settings::setSelectionRect({0, 0, 0, 0});
    }

    /* The logic for setting options for each start mode:
     *
     * - Gui/DBus: Prioritise command line options and default to saved settings.
     * - Background: Prioritise command line options and use defaults based on
     *   how command line options are meant to be used.
     *
     * Never start with a delay by default. It is annoying and confuses users
     * when nothing happens immediately after starting spectacle.
     */

    bool onClick;
    bool includeDecorations;
    bool includePointer;
    bool includeShadow;
    if (m_startMode == StartMode::Background) {
        onClick = m_cliOptions[Option::OnClick];
        includeDecorations = !m_cliOptions[Option::NoDecoration];
        includePointer = m_cliOptions[Option::Pointer];
        includeShadow = !m_cliOptions[Option::NoShadow];
    } else {
        onClick = Settings::captureOnClick() || m_cliOptions[Option::OnClick];
        includeDecorations = Settings::includeDecorations() && !m_cliOptions[Option::NoDecoration];
        includeShadow = Settings::includeShadow() && !m_cliOptions[Option::NoShadow];
        includePointer = Settings::includePointer() || m_cliOptions[Option::Pointer];
    }
    onClick &= m_imagePlatform->supportedShutterModes().testFlag(ImagePlatform::OnClick);

    int delayMsec = 0; // default to 0 if cli value parse fails
    if (onClick) {
        delayMsec = -1;
    } else if (m_cliOptions[Option::Delay]) {
        bool parseOk = false;
        int value = parser.value(CommandLineOptions::self()->delay).toInt(&parseOk);
        if (parseOk) {
            delayMsec = value;
        }
    }

    if (m_cliOptions[Option::EditExisting]) {
        auto input = parser.value(CommandLineOptions::self()->editExisting);
        m_editExistingUrl = QUrl::fromUserInput(input, QDir::currentPath(), QUrl::AssumeLocalFile);
        // QFileInfo::exists() only works with local files.
        auto existingLocalFile = m_editExistingUrl.toLocalFile();
        if (QFileInfo::exists(existingLocalFile)) {
            InlineMessageModel::instance()->clear();
            // If editing an existing image, open the annotation editor.
            // This QImage constructor only works with local files or Qt resource file names.
            QImage existingImage(existingLocalFile);
            m_annotationDocument->clearAnnotations();
            m_annotationDocument->setBaseImage(existingImage);
            m_returnToViewer = true;
            showViewerIfGuiMode();
            SpectacleWindow::setTitleForAll(SpectacleWindow::Saved, m_editExistingUrl.fileName());
            if (!existingImage.isNull()) {
                setExportImage(existingImage);
                ExportManager::instance()->scanQRCode();
            }
            return;
        } else {
            m_cliOptions[Option::EditExisting] = false;
            m_editExistingUrl.clear();
        }
    } else {
        m_editExistingUrl.clear();
    }

    if (m_cliOptions[Option::Output]) {
        m_outputUrl = QUrl::fromUserInput(parser.value(CommandLineOptions::self()->output), QDir::currentPath(), QUrl::AssumeLocalFile);
        if (!m_outputUrl.isValid()) {
            m_cliOptions[Option::Output] = false;
            m_outputUrl.clear();
        }
    } else {
        m_outputUrl.clear();
    }

    // Determine grab mode
    using CaptureMode = CaptureModeModel::CaptureMode;
    using GrabMode = ImagePlatform::GrabMode;
    auto cliGrabMode = [&]() -> std::optional<GrabMode> {
        if (m_cliOptions[Option::Fullscreen]) {
            return m_cliOptions[Option::Scaled] ? GrabMode::AllScreensScaled : GrabMode::AllScreens;
        } else if (m_cliOptions[Option::Current]) {
            return GrabMode::CurrentScreen;
        } else if (m_cliOptions[Option::ActiveWindow]) {
            return GrabMode::ActiveWindow;
        } else if (m_cliOptions[Option::Region]) {
            return GrabMode::PerScreenImageNative;
        } else if (m_cliOptions[Option::WindowUnderCursor]) {
            return toGrabMode(CaptureMode::WindowUnderCursor);
        }
        return std::nullopt;
    };
    auto launchActionGrabMode = [&] {
        switch (Settings::launchAction()) {
        case Settings::TakeRectangularScreenshot:
            return GrabMode::PerScreenImageNative;
        case Settings::TakeFullscreenScreenshot:
            return GrabMode::AllScreens;
        case Settings::UseLastUsedCapturemode:
            return toGrabMode(CaptureMode(Settings::captureMode()));
        default:
            return GrabMode::NoGrabModes;
        }
    };
    auto grabMode = cliGrabMode().value_or(m_startMode == StartMode::Background ? GrabMode::AllScreens : launchActionGrabMode());

    if (m_cliOptions[Option::Scaled] && !m_cliOptions[Option::Fullscreen]) {
        qWarning().noquote() << i18nc("@info:shell", "--scaled is only allowed together with -f/--fullscreen and will be ignored");
    }

    using RecordingMode = VideoPlatform::RecordingMode;
    RecordingMode recordingMode = RecordingMode::NoRecordingModes;
    if (m_cliOptions[Option::Record]) {
        auto input = parser.value(CommandLineOptions::self()->record);
        if (input.startsWith(u"s"_s, Qt::CaseInsensitive)) {
            recordingMode = RecordingMode::Screen;
        } else if (input.startsWith(u"w"_s, Qt::CaseInsensitive)) {
            recordingMode = RecordingMode::Window;
        } else if (input.startsWith(u"r"_s, Qt::CaseInsensitive)) {
            recordingMode = RecordingMode::Region;
        } else {
            // QCommandLineParser handles the case where input is empty
            qWarning().noquote() << i18nc("@info:shell", "%1 is not a valid mode for --record", input);
            Q_EMIT allDone();
            return;
        }
        setVideoMode(true);

        if (m_startMode != StartMode::Background) {
            includePointer = Settings::videoIncludePointer() || m_cliOptions[Option::Pointer];
        }
    } else {
        // Activation switches modes without restoring a screenshot; the
        // implicit capture belongs only to the GUI recording flow.
        setVideoMode(false, false);
    }

    if (m_cliOptions[Option::ReleaseCapture]) {
        SelectionEditor::instance()->forceReleaseCapture(true);
    }

    switch (m_startMode) {
    case StartMode::DBus:
        // Headless requests: explicit `-s`/`--dbus` captures and forwarded
        // recordings. Forwarded screenshots run in Gui mode instead.
        if (m_daemonMode && captureModeFromCli) {
            if (m_videoMode) {
                startRecording(recordingMode, includePointer);
            } else {
                takeNewScreenshot(grabMode, delayMsec, includePointer, includeDecorations, includeShadow);
            }
        }
        break;
    case StartMode::Background:
        if (m_videoMode) {
            startRecording(recordingMode, includePointer);
        } else {
            takeNewScreenshot(grabMode, delayMsec, includePointer, includeDecorations, includeShadow);
        }
        break;
    case StartMode::Gui:
        if (isGuiNull()) {
            if (m_cliOptions[Option::LaunchOnly] || //
                (Settings::launchAction() == Settings::DoNotTakeScreenshot && !captureModeFromCli)) {
                initViewerWindow(ViewerWindow::Dialog);
                ViewerWindow::instance()->setVisible(true);
            } else {
                if (m_videoMode) {
                    startRecording(recordingMode, includePointer);
                } else {
                    takeNewScreenshot(grabMode, delayMsec, includePointer, includeDecorations, includeShadow);
                }
            }
        } else {
            using Actions = Settings::EnumPrintKeyRunningAction;
            switch (Settings::printKeyRunningAction()) {
            case Actions::TakeNewScreenshot: {
                // takeNewScreenshot switches to on click if immediate is not supported.
                takeNewScreenshot(grabMode, 0, includePointer, includeDecorations, includeShadow);
                break;
            }
            case Actions::FocusWindow: {
                bool isCaptureWindow = !CaptureWindow::instances().isEmpty();
                SpectacleWindow *window = nullptr;
                if (isCaptureWindow) {
                    window = CaptureWindow::instances().constFirst();
                } else {
                    window = ViewerWindow::instance();
                }
                if (isCaptureWindow) {
                    SpectacleWindow::setVisibilityForAll(QWindow::FullScreen);
                } else {
                    // Unminimize the window.
                    window->unminimize();
                }
                window->requestActivate();
                break;
            }
            case Actions::StartNewInstance: {
                QProcess newInstance;
                newInstance.setProgram(QCoreApplication::applicationFilePath());
                newInstance.setArguments({CommandLineOptions::toArgument(CommandLineOptions::self()->newInstance)});
                newInstance.startDetached();
                break;
            }
            }
        }

        break;
    }
}

void SpectacleCore::takeNewScreenshot(ImagePlatform::GrabMode grabMode, int timeout, bool includePointer, bool includeDecorations, bool includeWindowShadow)
{
    if (m_grabInFlight) {
        // A capture, delay, or selection overlay is already pending. Starting
        // another one would run a second grab/overlay/picker and could race
        // ClipboardGuard; ignore this request instead.
        qWarning().noquote() << "Ignoring screenshot request: a capture is already in progress";
        return;
    }
    m_grabInFlight = true;

    // Clear when a new screenshot is taken to avoid overwriting
    // the previous file with a completely unrelated image.
    m_editExistingUrl.clear();
    m_cliOptions[CommandLineOptions::EditExisting] = false;

    m_delayAnimation->stop();

    m_lastGrabMode = grabMode;
    m_lastIncludePointer = includePointer;
    m_lastIncludeDecorations = includeDecorations;
    m_lastIncludeShadow = includeWindowShadow;

    if ((timeout < 0 || !m_imagePlatform->supportedShutterModes().testFlag(ImagePlatform::Immediate))
        && m_imagePlatform->supportedShutterModes().testFlag(ImagePlatform::OnClick)) {
        SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
        m_imagePlatform->doGrab(ImagePlatform::ShutterMode::OnClick, m_lastGrabMode, m_lastIncludePointer, m_lastIncludeDecorations, m_lastIncludeShadow);
        return;
    }

    timeout = qMax(0, timeout);
    const bool noDelay = timeout == 0;
#if WITH_X11
    if (PlasmaVersion::get() < PlasmaVersion::check(5, 27, 4) && KX11Extras::compositingActive()) {
        // when compositing is enabled, we need to give it enough time for the window
        // to disappear and all the effects are complete before we take the shot. there's
        // no way of knowing how long the disappearing effects take, but as per default
        // settings (and unless the user has set an extremely slow effect), 200
        // milliseconds is a good amount of wait time.
        timeout = qMax(timeout, 200);
    } else if (qobject_cast<ImagePlatformXcb *>(m_imagePlatform.get())) {
        // X11 compositors (which may or may not be kwin) require small delay for
        // window to disappear.
        // Also, minimum 50ms delay is needed to prevent segfaults from xcb function
        // calls that don't get replies fast enough.
        timeout = qMax(timeout, 50);
    }
#endif

    if (noDelay) {
        SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
        QTimer::singleShot(timeout, this, [this]() {
            m_imagePlatform->doGrab(ImagePlatform::ShutterMode::Immediate, m_lastGrabMode, m_lastIncludePointer, m_lastIncludeDecorations, m_lastIncludeShadow);
        });
        return;
    }

    m_delayAnimation->setDuration(timeout);
    m_delayAnimation->start();

    // skip minimize animation.
    SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
    SpectacleWindow::setVisibilityForAll(QWindow::Minimized);
}

void SpectacleCore::takeNewScreenshot(int captureMode, int timeout, bool includePointer, bool includeDecorations, bool includeShadow)
{
    using CaptureMode = CaptureModeModel::CaptureMode;
    if (timeout < 0 && !m_imagePlatform->supportedShutterModes().testFlag(ImagePlatform::OnClick)) {
        timeout = Settings::captureDelay() * 1000;
    }
    takeNewScreenshot(toGrabMode(CaptureMode(captureMode)), timeout, includePointer, includeDecorations, includeShadow);
}

void SpectacleCore::finishNonGuiCapture()
{
    // Outside GUI mode allDone() used to make the process exit, so the capture
    // windows disappeared along with it. A resident instance keeps running, so
    // the overlay has to be destroyed explicitly or it would stay on screen.
    // Defer both steps: cancellation can be triggered from a window's event
    // handler, and deleting that window there would be re-entrant.
    const quint64 generation = ++m_captureGeneration;
    QTimer::singleShot(0, this, [this, generation] {
        // A new capture started in the meantime; its windows must not be
        // deleted by this stale cancellation.
        if (generation != m_captureGeneration) {
            return;
        }
        deleteWindows();
        Q_EMIT allDone();
    });
}

void SpectacleCore::cancelScreenshot()
{
    m_grabInFlight = false;
    if (m_startMode != StartMode::Gui || !m_returnToViewer) {
        finishNonGuiCapture();
        return;
    }

    int currentTime = m_delayAnimation->currentTime();
    m_delayAnimation->stop();
    if (currentTime > 0) {
        SpectacleWindow::setTitleForAll(SpectacleWindow::Previous);
    } else if (!ViewerWindow::instance()) {
        initViewerWindow(ViewerWindow::Viewer);
        ViewerWindow::instance()->setVisible(true);
    } else if (ViewerWindow::instance()) {
        Q_EMIT allDone();
    }
}

void SpectacleCore::showErrorMessage(const QString &message)
{
    qWarning().noquote() << message;

    if (m_startMode == StartMode::Gui) {
        KMessageBox::error(nullptr, message);
    } else if (m_startMode == StartMode::DBus) {
        // A resident/daemon request must not block on a modal dialog; use a
        // notification (and the D-Bus failure signal) instead.
        KNotification::event(KNotification::Error, i18n("NekoShot"), message);
    }
}

void SpectacleCore::showViewerIfGuiMode(bool minimized)
{
    if (m_startMode != StartMode::Gui || !m_returnToViewer) {
        return;
    }
    initViewerWindow(ViewerWindow::Viewer);
    if (!m_videoMode && m_cliOptions[CommandLineOptions::EditExisting]) {
        ViewerWindow::instance()->setAnnotating(true);
    }
    if (minimized) {
        ViewerWindow::instance()->showMinimized();
    } else {
        ViewerWindow::instance()->setVisible(true);
    }
}

void SpectacleCore::doNotify(ScreenCapture type, const ExportManager::Actions &actions, const QUrl &saveUrl)
{
    if (m_cliOptions[CommandLineOptions::NoNotify]) {
        return;
    }

    // ensure program stays alive until the notification finishes.
    if (!m_eventLoopLocker) {
        m_eventLoopLocker = std::make_unique<QEventLoopLocker>();
    }

    KNotification *notification = nullptr;
    QString title;
    // Use nullptr as the notification parent to prevent them from disappearing from history.
    if (type == ScreenCapture::Screenshot) {
        notification = new KNotification(u"newScreenshotSaved"_s, KNotification::CloseOnTimeout, nullptr);
        title = CaptureModeModel::captureModeLabel(toCaptureMode(m_lastGrabMode));
    } else {
        notification = new KNotification(u"recordingSaved"_s, KNotification::CloseOnTimeout, nullptr);
        title = RecordingModeModel::recordingModeLabel(m_lastRecordingMode);
    }
    notification->setTitle(title);

    notifications.append(notification);

    // a speaking message is prettier than a URL, special case for copy image/location to clipboard and the default pictures location
    const QString &saveDirPath = saveUrl.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).path();
    const QString &saveFileName = saveUrl.fileName();

    using Action = ExportManager::Action;
    if (type == ScreenCapture::Screenshot) {
        if (actions & Action::AnySave && !saveFileName.isEmpty()) {
            if (actions & Action::CopyPath) {
                notification->setText(i18nc("%1 is the filename, %2 the path to the save location",
                                            "A screenshot was saved as '%1' to '%2' and the file path of the screenshot has been saved to your clipboard.",
                                            saveFileName,
                                            saveDirPath));
            } else if (saveDirPath == QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)) {
                notification->setText(i18nc("%1 is the filename", "A screenshot was saved as '%1' to your Pictures folder.", saveFileName));
            } else {
                notification->setText(
                    i18nc("%1 is the filename, %2 the path to the save location", "A screenshot was saved as '%1' to '%2'.", saveFileName, saveDirPath));
            }
        } else if (actions & Action::CopyImage) {
            notification->setText(i18n("A screenshot was saved to your clipboard."));
        }
    } else if (type == ScreenCapture::Recording && actions & Action::AnySave && !saveFileName.isEmpty()) {
        if (actions & Action::CopyPath) {
            notification->setText(i18nc("%1 is the filename, %2 the path to the save location",
                                        "A recording was saved as '%1' to '%2' and the file path of the recording has been saved to your clipboard.",
                                        saveFileName,
                                        saveDirPath));
        } else if (saveDirPath == QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)) {
            notification->setText(i18nc("%1 is the filename", "A recording was saved as '%1' to your Videos folder.", saveFileName));
        } else {
            notification->setText(
                i18nc("%1 is the filename, %2 the path to the save location", "A recording was saved as '%1' to '%2'.", saveFileName, saveDirPath));
        }
    }

    if (!saveUrl.isEmpty()) {
        notification->setUrls({saveUrl});

        auto open = [notification, saveUrl]() {
            auto job = new KIO::OpenUrlJob(saveUrl);
            job->setStartupId(notification->xdgActivationToken().toUtf8());
            job->start();
        };
        auto defaultAction = notification->addDefaultAction(i18nc("Open the screenshot we just saved", "Open"));
        connect(defaultAction, &KNotificationAction::activated, this, open);

        if (type == ScreenCapture::Screenshot) {
            auto annotate = [saveUrl]() {
                QProcess newInstance;
                newInstance.setProgram(QCoreApplication::applicationFilePath());
                newInstance.setArguments({CommandLineOptions::toArgument(CommandLineOptions::self()->newInstance),
                                          CommandLineOptions::toArgument(CommandLineOptions::self()->editExisting),
                                          saveUrl.toLocalFile()});
                newInstance.startDetached();
            };
            auto annotateAction = notification->addAction(i18n("Annotate"));
            connect(annotateAction, &KNotificationAction::activated, this, annotate);
        }
    }

    auto onExpired = [this, notification] {
        notifications.removeOne(static_cast<KNotification *>(notification));
        // When there are no more notifications running, we can remove the loop locker.
        if (notifications.empty() && m_eventLoopLocker) {
            QTimer::singleShot(250, this, [this] {
                m_eventLoopLocker.reset();
            });
        }
    };
    connect(notification, &QObject::destroyed, this, onExpired);
    // BUG: https://bugs.kde.org/show_bug.cgi?id=503838
    // We can't call KNotification::close or else the notifications will be removed from history.
    // 10 seconds is roughly the expected default duration.
    QTimer::singleShot(10000, notification, onExpired);

    notification->sendEvent();
}

ExportManager::Actions SpectacleCore::autoExportActions() const
{
    using Action = ExportManager::Action;
    using Option = CommandLineOptions::Option;
    bool save = (m_startMode != StartMode::Gui && m_cliOptions[Option::Output]) || m_videoMode;
    bool copyImage = m_cliOptions[Option::CopyImage] && !m_videoMode;
    bool copyPath = m_cliOptions[Option::CopyPath];
    ExportManager::Actions actions;
    if (m_startMode != StartMode::Background) {
        save |= Settings::autoSaveImage();
        copyImage |= Settings::clipboardGroup() == Settings::PostScreenshotCopyImage && !m_videoMode;
        copyPath |= Settings::clipboardGroup() == Settings::PostScreenshotCopyLocation;
    }
    if (m_startMode == StartMode::Gui) {
        actions.setFlag(Action::Save, save);
        actions.setFlag(Action::CopyImage, copyImage);
    } else {
        // In background and dbus mode, ensure that either save or copy image is enabled.
        actions.setFlag(Action::Save, save || !copyImage);
        actions.setFlag(Action::CopyImage, !actions.testFlag(Action::Save) || copyImage);
    }
    actions.setFlag(Action::CopyPath, actions.testFlag(Action::Save) && copyPath);
    return actions;
}

ImagePlatform::GrabMode SpectacleCore::toGrabMode(CaptureModeModel::CaptureMode captureMode) const
{
    using GrabMode = ImagePlatform::GrabMode;
    using CaptureMode = CaptureModeModel::CaptureMode;
    const auto &supportedGrabModes = m_imagePlatform->supportedGrabModes();
    if (captureMode == CaptureMode::CurrentScreen && supportedGrabModes.testFlag(ImagePlatform::CurrentScreen)) {
        return GrabMode::CurrentScreen;
    } else if (captureMode == CaptureMode::ActiveWindow && supportedGrabModes.testFlag(ImagePlatform::ActiveWindow)) {
        return GrabMode::ActiveWindow;
    } else if (captureMode == CaptureMode::WindowUnderCursor && supportedGrabModes.testFlag(ImagePlatform::WindowUnderCursor)) {
        if (supportedGrabModes.testFlag(ImagePlatform::TransientWithParent)) {
            return GrabMode::TransientWithParent;
        } else {
            return GrabMode::WindowUnderCursor;
        }
    } else if (captureMode == CaptureMode::RectangularRegion && supportedGrabModes.testFlag(ImagePlatform::PerScreenImageNative)) {
        return GrabMode::PerScreenImageNative;
    } else if (captureMode == CaptureMode::AllScreensScaled && supportedGrabModes.testFlag(ImagePlatform::AllScreensScaled)) {
        return GrabMode::AllScreensScaled;
    } else if (supportedGrabModes.testFlag(ImagePlatform::AllScreens)) { // default if supported
        return GrabMode::AllScreens;
    } else {
        return GrabMode::NoGrabModes;
    }
}

CaptureModeModel::CaptureMode SpectacleCore::toCaptureMode(ImagePlatform::GrabMode grabMode) const
{
    using GrabMode = ImagePlatform::GrabMode;
    using CaptureMode = CaptureModeModel::CaptureMode;
    if (grabMode == GrabMode::CurrentScreen) {
        return CaptureMode::CurrentScreen;
    } else if (grabMode == GrabMode::ActiveWindow) {
        return CaptureMode::ActiveWindow;
    } else if (grabMode == GrabMode::WindowUnderCursor) {
        return CaptureMode::WindowUnderCursor;
    } else if (grabMode == GrabMode::PerScreenImageNative) {
        return CaptureMode::RectangularRegion;
    } else if (grabMode == GrabMode::AllScreensScaled) {
        return CaptureMode::AllScreensScaled;
    } else {
        if (m_imagePlatform->supportedGrabModes().testFlag(ImagePlatform::CurrentScreen)) {
            return CaptureMode::AllScreens;
        } else {
            return CaptureMode::FullScreen;
        }
    }
}

bool SpectacleCore::isGuiNull() const
{
    return SpectacleWindow::instances().isEmpty();
}

bool SpectacleCore::isDaemonMode() const
{
    return m_daemonMode;
}

int SpectacleCore::exitCode() const
{
    return m_exitCode;
}

bool SpectacleCore::beginDbusRequest()
{
    if (m_grabInFlight) {
        // A forwarded GUI capture or selection is still pending. Ignoring the
        // request also means its state must not be reset underneath it.
        qWarning().noquote() << "Ignoring D-Bus request: a capture is already in progress";
        return false;
    }

    // A D-Bus request must not inherit state from a previous activation, such
    // as a forwarded `nekoshot -r` that left CLI options or Gui mode behind.
    // Requests always start with DBus semantics and no viewer; a request that
    // wants a window (OpenWithoutScreenshot) changes the mode itself.
    m_startMode = StartMode::DBus;
    m_returnToViewer = false;
    // Reset directly: setVideoMode() may start an implicit capture (see below),
    // which must never happen for a D-Bus request.
    m_videoMode = false;
    m_cliOptions.fill(false);
    m_editExistingUrl.clear();
    m_outputUrl.clear();
    return true;
}

void SpectacleCore::initGuiNoScreenshot()
{
    initViewerWindow(ViewerWindow::Dialog);
    ViewerWindow::instance()->setVisible(true);
}

// Hurry up the sync if the sync timer is active.
void SpectacleCore::syncExportImage()
{
    if (!m_annotationSyncTimer->isActive()) {
        return;
    }
    auto image = m_annotationDocument->renderToImage();
    auto canvasPos = m_annotationDocument->canvasRect().topLeft();
    ImageMetaData::setLogicalXY(image, canvasPos.x(), canvasPos.y());
    setExportImage(image);
}

// A convenient way to stop the sync timer and set the export image.
void SpectacleCore::setExportImage(const QImage &image)
{
    m_annotationSyncTimer->stop();
    ExportManager::instance()->setImage(image);
}

void SpectacleCore::prepareForDaemon()
{
    StartupProfile::mark("daemon-warm-enter");
    auto engine = getQmlEngine();
    const QUrl components[] = {
        QUrl(QString(SPECTACLE_QML_PATH) + u"/Gui/CaptureOverlay.qml"_s),
        QUrl(QString(SPECTACLE_QML_PATH) + u"/Gui/AnnotationEditor.qml"_s),
        QUrl(QString(SPECTACLE_QML_PATH) + u"/Gui/ScreenshotView.qml"_s),
        QUrl(QString(SPECTACLE_QML_PATH) + u"/Gui/FloatingToolBar.qml"_s),
        QUrl(QString(SPECTACLE_QML_PATH) + u"/Gui/CaptureSettingsColumn.qml"_s),
        QUrl(QString(SPECTACLE_QML_PATH) + u"/Gui/ViewerWindow.qml"_s),
        QUrl(QString(SPECTACLE_QML_PATH) + u"/Gui/DialogPage.qml"_s),
    };
    for (const auto &url : components) {
        QQmlComponent component(engine, url);
        if (component.isError()) {
            qWarning().noquote() << "Failed to preload QML component" << url << component.errorString();
        }
    }
    StartupProfile::mark("daemon-warm-end");
}

QQmlEngine *SpectacleCore::getQmlEngine()
{
    if (m_engine == nullptr) {
        m_engine = std::make_unique<QQmlEngine>(this);
        m_engine->rootContext()->setContextObject(new KLocalizedQmlContext(m_engine.get()));
    }
    return m_engine.get();
}

void SpectacleCore::initCaptureWindows(CaptureWindow::Mode mode)
{
    // Invalidate any pending deferred cancellation cleanup: it must not delete
    // the windows created for this new capture.
    ++m_captureGeneration;
    deleteWindows();

    // Allow the window to be transparent. Used for video recording UI.
    // It has to be set before creating the window.
    QQuickWindow::setDefaultAlphaBuffer(true);

    auto engine = getQmlEngine();
    const auto screens = qApp->screens();
    for (auto *screen : screens) {
        const auto screenRect = Geometry::mapFromPlatformRect(screen->geometry(), screen->devicePixelRatio());
        // Don't show windows for screens that don't have an image. Use the
        // image's logical rectangle when the backend recorded one: with a
        // negative-origin layout the annotation canvas starts at (0, 0) and
        // would otherwise exclude screens left of / above the primary output.
        const QImage &baseImage = m_annotationDocument->baseImage();
        const QRectF imageRect =
            !baseImage.isNull() && ImageMetaData::hasLogicalXY(baseImage) ? ImageMetaData::logicalRect(baseImage) : m_annotationDocument->canvasRect();
        if (!m_videoMode && !baseImage.isNull() && !screenRect.intersects(imageRect)) {
            continue;
        }
        m_captureWindows.emplace_back(CaptureWindow::makeUnique(mode, screen, engine));
    }
}

void SpectacleCore::initViewerWindow(ViewerWindow::Mode mode)
{
    // always switch to gui mode when a viewer window is used.
    m_startMode = SpectacleCore::StartMode::Gui;
    m_returnToViewer = true;
    deleteWindows();

    // Transparency isn't needed for this window.
    QQuickWindow::setDefaultAlphaBuffer(false);

    m_viewerWindow = ViewerWindow::makeUnique(mode, getQmlEngine());
}

void SpectacleCore::deleteWindows()
{
    m_viewerWindow.reset();
    m_captureWindows.clear();
}

void SpectacleCore::unityLauncherUpdate(const QVariantMap &properties) const
{
    QDBusMessage message = QDBusMessage::createSignal(u"/org/kde/Spectacle"_s, u"com.canonical.Unity.LauncherEntry"_s, u"Update"_s);
    message.setArguments({QApplication::desktopFileName(), properties});
    QDBusConnection::sessionBus().send(message);
}

void SpectacleCore::startRecording(VideoPlatform::RecordingMode mode, bool withPointer)
{
    if (m_videoPlatform->isRecordingOrStarting() || mode == VideoPlatform::NoRecordingModes) {
        return;
    }
    const bool hasCaptureWindows = !CaptureWindow::instances().empty();
    if (hasCaptureWindows && mode != VideoPlatform::Region && !m_recordingEventLoopLocker) {
        m_recordingEventLoopLocker = std::make_unique<QEventLoopLocker>();
    }
    if (hasCaptureWindows) {
        SpectacleWindow::setVisibilityForAll(QWindow::Hidden);
        if (mode != VideoPlatform::Region) {
            m_returnToViewer = true;
        }
    }
    m_lastRecordingMode = mode;
    setVideoMode(true);
    const auto &output = m_outputUrl.isLocalFile() ? videoOutputUrl() : QUrl();
    m_videoPlatform->startRecording(output, mode, {}, withPointer);
}

void SpectacleCore::finishRecording()
{
    // StopRecording can arrive while a window picker is still waiting for a
    // click, when the platform has not reported Recording yet. The platform
    // treats that as a cancellation instead of asserting.
    m_videoPlatform->finishRecording();
}

bool SpectacleCore::videoMode() const
{
    return m_videoMode;
}

void SpectacleCore::resetVideoModeAfterRecording()
{
    if (!m_videoMode) {
        return;
    }
    m_videoMode = false;
    Q_EMIT videoModeChanged(false);
}

void SpectacleCore::setVideoMode(bool videoMode, bool restoreScreenshot)
{
    if (videoMode == m_videoMode) {
        return;
    }
    InlineMessageModel::instance()->clear();
    m_videoMode = videoMode;
    if (!videoMode && restoreScreenshot && m_annotationDocument->baseImage().isNull() //
        && m_startMode == StartMode::Gui && m_returnToViewer) {
        // Only the GUI recording flow returns to the viewer with no image.
        // D-Bus/daemon requests must capture exactly once, from their own path.
        takeNewScreenshot(ImagePlatform::PerScreenImageNative, 0, Settings::includePointer(), Settings::includeDecorations(), Settings::includeShadow());
    }
    Q_EMIT videoModeChanged(videoMode);
}

QUrl SpectacleCore::currentVideo() const
{
    return m_currentVideo;
}

void SpectacleCore::setCurrentVideo(const QUrl &currentVideo)
{
    if (currentVideo == m_currentVideo) {
        return;
    }
    m_currentVideo = currentVideo;
    Q_EMIT currentVideoChanged(currentVideo);
}

QUrl SpectacleCore::videoOutputUrl() const
{
    return VideoPlatform::formatForPath(m_outputUrl.path()) != VideoPlatform::NoFormat ? m_outputUrl : QUrl();
}

QString SpectacleCore::recordedTime() const
{
    return timeFromMilliseconds(m_videoPlatform->recordedTime());
}

QString SpectacleCore::timeFromMilliseconds(qint64 milliseconds) const
{
    KFormat::DurationFormatOptions options = KFormat::DefaultDuration;
    if (milliseconds < 1000.0 * 60.0 * 60.0) {
        options |= KFormat::FoldHours;
    }
    return KFormat().formatDuration(milliseconds, options);
}

void SpectacleCore::activateAction(const QString &actionName, const QVariant &parameter)
{
    Q_UNUSED(parameter)
    if (!beginDbusRequest()) {
        return;
    }
    if (m_videoPlatform->isRecordingOrStarting()) {
        // BUG: https://bugs.kde.org/show_bug.cgi?id=481471
        // TODO: find a way to support screenshot shortcuts while recording?
        finishRecording();
    } else if (actionName == ShortcutActions::self()->fullScreenAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::AllScreens, 0);
    } else if (actionName == ShortcutActions::self()->currentScreenAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::CurrentScreen, 0);
    } else if (actionName == ShortcutActions::self()->activeWindowAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::ActiveWindow, 0);
    } else if (actionName == ShortcutActions::self()->windowUnderCursorAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::WindowUnderCursor, 0);
    } else if (actionName == ShortcutActions::self()->regionAction()->objectName()) {
        takeNewScreenshot(CaptureModeModel::RectangularRegion, 0);
    } else if (actionName == ShortcutActions::self()->recordRegionAction()->objectName()) {
        startRecording(VideoPlatform::Region);
    } else if (actionName == ShortcutActions::self()->recordScreenAction()->objectName()) {
        startRecording(VideoPlatform::Screen);
    } else if (actionName == ShortcutActions::self()->recordWindowAction()->objectName()) {
        startRecording(VideoPlatform::Window);
    } else if (actionName == ShortcutActions::self()->openWithoutScreenshotAction()->objectName()) {
        initGuiNoScreenshot();
    }
}

#include "moc_SpectacleCore.cpp"

/*
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "CommandLineOptions.h"
#include "Config.h"
#include "Gui/WidgetTheme.h"
#include "Platforms/NiriPlatform.h"
#include "ScreenShotEffect.h"
#include "ShortcutActions.h"
#include "SpectacleCore.h"
#include "SpectacleDBusAdapter.h"
#include "StartupProfile.h"
#include "settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QIcon>
#include <QSessionManager>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>
#include <KMessageBox>
#include <KWindowSystem>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    StartupProfile::mark("main-enter");
    // set up the application

    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QIcon::setFallbackThemeName(u"breeze"_s);
    QApplication app(argc, argv);
    StartupProfile::mark("qapplication");

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("nekoshot"));
    QCoreApplication::setApplicationName(u"nekoshot"_s);
    WidgetTheme::apply(app);

    KAboutData aboutData(u"nekoshot"_s,
                         i18n("NekoShot"),
                         QStringLiteral(SPECTACLE_VERSION),
                         i18n("Screenshot utility for niri, based on Spectacle"),
                         KAboutLicense::GPL_V2,
                         i18n("© 2015 Boudhayan Gupta and Spectacle contributors"));
    // KAboutData::setApplicationData() writes these back to QCoreApplication/QGuiApplication,
    // so they must be set on aboutData to take effect at runtime.
    aboutData.setOrganizationDomain(QByteArrayLiteral("org.nekoshot"));
    aboutData.setDesktopFileName(u"org.nekoshot.NekoShot"_s);
    aboutData.addAuthor(u"Boudhayan Gupta"_s, {}, u"bgupta@kde.org"_s);
    aboutData.addAuthor(u"David Redondo"_s, {}, u"kde@david-redondo.de"_s);
    aboutData.addAuthor(u"Noah Davis"_s, {}, u"noahadvs@gmail.com"_s);
    aboutData.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"), i18nc("EMAIL OF TRANSLATORS", "Your emails"));
    KAboutData::setApplicationData(aboutData);
    StartupProfile::mark("aboutdata");
    app.setWindowIcon(QIcon::fromTheme(u"nekoshot"_s));

    KCrash::initialize();

    QCommandLineParser commandLineParser;
    aboutData.setupCommandLine(&commandLineParser);
    commandLineParser.addOptions(CommandLineOptions::self()->allOptions);

    // first parsing for help-about
    commandLineParser.process(app.arguments());
    aboutData.processCommandLine(&commandLineParser);

    // BUG: https://bugs.kde.org/show_bug.cgi?id=451842
    // We currently don't support desktop environments besides KDE Plasma on Wayland
    // because we have to rely on KWin's DBus API.
    if (KWindowSystem::isPlatformWayland() && !ScreenShotEffect::isLoaded() && !NiriPlatform::isNiriSession()) {
        auto message = i18n(
            "On Wayland, Spectacle requires KDE Plasma's KWin compositor, which does not seem to be available. Use Spectacle on KDE Plasma, or use a different "
            "screenshot tool.");
        qWarning().noquote() << message;
        if (commandLineParser.isSet(CommandLineOptions::self()->background) || commandLineParser.isSet(CommandLineOptions::self()->dbus)
            || commandLineParser.isSet(CommandLineOptions::self()->daemon)) {
            // Return early if not in GUI mode.
            return 1;
        } else {
            KMessageBox::error(nullptr, message);
        }
    }

    // niri has no KWin Effect API; screen contents come from grim. In
    // non-interactive modes fail fast with a non-zero exit status instead of
    // letting the normal capture failure path quit successfully. When the
    // platform override picks a different backend, the niri preflight does not
    // apply.
    const QString platformOverride = qEnvironmentVariable("NEKOSHOT_IMAGE_PLATFORM");
    const QString legacyPlatformOverride = qEnvironmentVariable("SPECTACLE_IMAGE_PLATFORM");
    const QString selectedPlatformOverride = !platformOverride.isEmpty() ? platformOverride : legacyPlatformOverride;
    const bool knownNonNiriOverride = selectedPlatformOverride == QLatin1String("ImagePlatformKWin")
        || selectedPlatformOverride == QLatin1String("ImagePlatformXcb") || selectedPlatformOverride == QLatin1String("ImagePlatformNull");
    const bool niriBackendSelected = !knownNonNiriOverride && NiriPlatform::isNiriSession();
    if (niriBackendSelected && QStandardPaths::findExecutable(QStringLiteral("grim")).isEmpty()
        && (commandLineParser.isSet(CommandLineOptions::self()->background) || commandLineParser.isSet(CommandLineOptions::self()->dbus)
            || commandLineParser.isSet(CommandLineOptions::self()->daemon))) {
        qWarning().noquote() << i18n("grim is not installed");
        return 1;
    }

    // Prevent session manager from restoring the app on start up.
    // https://bugs.kde.org/show_bug.cgi?id=430411
    auto disableSessionManagement = [](QSessionManager &sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
    };
    QObject::connect(&app, &QGuiApplication::commitDataRequest, disableSessionManagement);
    QObject::connect(&app, &QGuiApplication::saveStateRequest, disableSessionManagement);

    // --new-instance skips KDBusService, so it cannot host D-Bus requests and
    // cannot work as a daemon. Reject the combination instead of silently
    // starting a process that exits after the first capture.
    if (commandLineParser.isSet(CommandLineOptions::self()->newInstance) && commandLineParser.isSet(CommandLineOptions::self()->daemon)) {
        qWarning().noquote() << i18n("--daemon cannot be combined with --new-instance");
        return 1;
    }

    // If the new instance command line option has been specified,
    // use this alternative path for executing Spectacle.
    if (commandLineParser.isSet(CommandLineOptions::self()->newInstance)) {
        auto spectacleCore = SpectacleCore::instance();

        QObject::connect(qApp, &QApplication::aboutToQuit, Settings::self(), &Settings::save);
        QObject::connect(spectacleCore, &SpectacleCore::allDone, &app, &QCoreApplication::quit, Qt::QueuedConnection);

        // fire it up
        spectacleCore->activate(app.arguments(), QDir::currentPath());

        const int result = app.exec();
        QThreadPool::globalInstance()->waitForDone(1000);
        return result != 0 ? result : spectacleCore->exitCode();
    }

    // With the StartupOption::Unique flag, this process will exit during the construction of
    // KDBusService if Spectacle has already been registered.
    // This object does not need a parent since it will be deleted when it falls out of scope.
    KDBusService service(KDBusService::Unique);
    StartupProfile::mark("kdbusservice");

    auto spectacleCore = SpectacleCore::instance();
    StartupProfile::mark("core-instance");

    QObject::connect(&service, &KDBusService::activateRequested, spectacleCore, &SpectacleCore::activate);
    QObject::connect(&service, &KDBusService::activateActionRequested, spectacleCore, &SpectacleCore::activateAction);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, Settings::self(), &Settings::save);
    // --daemon is a startup-only flag and activate() is what records it in
    // SpectacleCore. The quit connection has to be decided before activate()
    // runs, because a synchronous allDone from invalid arguments must still
    // terminate a one-shot process. Use the already-parsed command line here;
    // a resident instance must outlive each completed capture.
    const bool startInDaemonMode = commandLineParser.isSet(CommandLineOptions::self()->daemon);
    if (!startInDaemonMode) {
        QObject::connect(spectacleCore, &SpectacleCore::allDone, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    }

    // create the dbus connections
    SpectacleDBusAdapter *dbusAdapter = new SpectacleDBusAdapter(spectacleCore);
    QObject::connect(spectacleCore, &SpectacleCore::dbusScreenshotFailed, dbusAdapter, &SpectacleDBusAdapter::ScreenshotFailed);
    QObject::connect(spectacleCore, &SpectacleCore::dbusRecordingFailed, dbusAdapter, &SpectacleDBusAdapter::RecordingFailed);
    QObject::connect(ExportManager::instance(),
                     &ExportManager::imageExported,
                     spectacleCore,
                     [dbusAdapter](const ExportManager::Actions &actions, const QUrl &url) {
                         Q_UNUSED(actions)
                         Q_EMIT dbusAdapter->ScreenshotTaken(url.toLocalFile());
                     });
    QObject::connect(ExportManager::instance(),
                     &ExportManager::videoExported,
                     spectacleCore,
                     [dbusAdapter](const ExportManager::Actions &actions, const QUrl &url) {
                         Q_UNUSED(actions)
                         Q_EMIT dbusAdapter->RecordingTaken(url.toLocalFile());
                     });
    QDBusConnection::sessionBus().registerObject(u"/"_s, spectacleCore);
    QDBusConnection::sessionBus().registerService(u"org.nekoshot.NekoShot"_s);

    // fire it up
    spectacleCore->activate(app.arguments(), QDir::currentPath());
    StartupProfile::mark("activate-return");
    if (startInDaemonMode) {
        spectacleCore->prepareForDaemon();
    }
    QTimer::singleShot(0, qApp, [] {
        StartupProfile::mark("exec-tick");
    });

    if (spectacleCore->isDaemonMode()) {
        // Qt quits automatically once the last QEventLoopLocker is released
        // (ExportManager holds one while an image export settles) and no
        // windows remain. A resident instance has to survive those exports, and
        // it also must not exit when a viewer it opened is closed.
        QCoreApplication::setQuitLockEnabled(false);
        app.setQuitOnLastWindowClosed(false);
    }

    const int result = app.exec();
    StartupProfile::mark("exec-return");
    QThreadPool::globalInstance()->waitForDone(1000);
    return result != 0 ? result : spectacleCore->exitCode();
}

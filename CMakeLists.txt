cmake_minimum_required (VERSION 3.16 FATAL_ERROR)
# Managed by release script
set(PROJECT_VERSION "0.11.1")
set(SPECTACLE_VERSION ${PROJECT_VERSION})

# Spectacle project
project(NekoShot VERSION ${SPECTACLE_VERSION})

# minimum requirements
set(PROJECT_DEP_VERSION "6.7.90")
set(QT_MIN_VERSION "6.11.0")
set(KF6_MIN_VERSION "6.30.0")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# set up standard kde build settings

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

# Set C++ standard separately from KDECompilerSettings since that is set to an older C++ standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# used cmake macros

include(ECMInstallIcons)
include(ECMSetupVersion)
include(FeatureSummary)
include(ECMQtDeclareLoggingCategory)
include(ECMAddTests)
include(KDEClangFormat)
include(KDEGitCommitHooks)
include(ECMDeprecationSettings)
include(ECMFindQmlModule)

option(WITH_X11 "Enable X11 support" OFF)

# find dependencies

find_package(
    Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED
        Core
        Concurrent
        Qml
        Quick
        QuickControls2
        QuickTemplates2
        Widgets
        DBus
        PrintSupport
        Test
        WaylandClient
        Multimedia
)
find_package(Qt6GuiPrivate ${QT_MIN_VERSION} REQUIRED NO_MODULE)

qt_policy(SET QTP0001 NEW)

find_package(
    KF6 ${KF6_MIN_VERSION} REQUIRED
        CoreAddons
        WidgetsAddons
        DBusAddons
        Notifications
        Config
        I18n
        KIO
        WindowSystem
        GlobalAccel
        XmlGui
        GuiAddons
        KirigamiPlatform
        StatusNotifierItem
        Prison
        Crash
)
if (NOT TARGET KF6::PrisonScanner ) 
    message(SEND_ERROR "Prison built without required PrisonScanner component")
endif()

find_package(Wayland REQUIRED COMPONENTS Client)
find_package(PlasmaWaylandProtocols REQUIRED)
find_package(LayerShellQt REQUIRED)
find_package(KPipeWire)
find_package(PkgConfig REQUIRED)

set_package_properties(KPipeWire PROPERTIES DESCRIPTION
    "Used to record pipewire streams into a file"
    TYPE REQUIRED
)

ecm_find_qmlmodule(org.kde.kquickimageeditor)
find_package(KQuickImageEditor 0.7 REQUIRED COMPONENTS)
set_package_properties(KQuickImageEditor PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "Simple image editor for QtQuick applications"
    URL "https://invent.kde.org/libraries/kquickimageeditor/"
    PURPOSE "Add image editing capability to the screenshots"
)

pkg_check_modules(TESSERACT REQUIRED IMPORTED_TARGET tesseract)

if(WITH_X11)
    find_package(XCB REQUIRED COMPONENTS XFIXES IMAGE UTIL CURSOR RANDR)
endif()

# setup handling of deprecated Qt & KF API

ecm_set_disabled_deprecation_versions(QT 6.11.0  KF 6.27.0)

# hand off to subdirectories

add_subdirectory(src)
add_subdirectory(dbus)
add_subdirectory(desktop)
add_subdirectory(icons)
add_subdirectory(tests)

ki18n_install(po)

install(FILES docs/nekoshot.1 DESTINATION ${KDE_INSTALL_MANDIR}/man1)

ecm_qt_install_logging_categories(EXPORT SPECTACLE FILE spectacle.categories DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})

# summaries

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.cpp *.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

# Packaging (DEB/RPM/TGZ) -----------------------------------------------------
# The Arch package is the primary distribution channel; these generators make
# the same installed tree available as generic archives and to Debian/RPM based
# distributions. Library dependencies are computed from the installed binaries
# by dpkg-shlibdeps / rpm's autoreq where the tools are available.
set(CPACK_PACKAGE_NAME "nekoshot")
set(CPACK_PACKAGE_VENDOR "NekoShot")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "NekoRain <NekoRain404@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Spectacle-based screenshot and screen recording utility for niri")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/NekoRain404/NekoShot")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSES/GPL-2.0-or-later.txt")
set(CPACK_GENERATOR "TGZ")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "NekoRain <NekoRain404@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Debian 13 (trixie) package names. Library versions are not discovered by
# dpkg-shlibdeps on Arch, where this package is built, so the runtime library
# dependencies are listed explicitly.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "grim, wl-clipboard, niri, tesseract-ocr, libtesseract5, \
    libqt6core6, libqt6gui6, libqt6widgets6, libqt6quick6, libqt6qml6, libqt6dbus6, \
    libqt6network6, libqt6multimedia6, libqt6printsupport6, libqt6svg6, \
    libkf6coreaddons6, libkf6configcore6, libkf6configgui6, libkf6i18n6, libkf6guiaddons6, \
    libkf6windowsystem6, libkf6notifications6, libkf6dbusaddons6, libkf6globalaccel6, \
    libkf6xmlgui6, libkf6kiocore6, libkf6kiowidgets6, libkf6kiofilewidgets6, \
    libkf6statusnotifieritem6, libkf6crash6, libkf6colorscheme6, libkf6completion6, \
    libkf6configwidgets6, libkf6widgetsaddons6, libkf6jobwidgets6, libkf6service6, \
    libkf6kcmutils6, liblayershellqtinterface6, libprison0, \
    qml6-module-org-kde-kirigami, qml6-module-org-kde-kquickimageeditor, \
    qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtmultimedia")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "User Interface/Desktops")
set(CPACK_RPM_PACKAGE_REQUIRES "grim, wl-clipboard, niri, tesseract")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

include(CPack)

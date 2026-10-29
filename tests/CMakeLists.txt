include_directories(${PROJECT_SOURCE_DIR}/src)

SET(FILENAME_TEST_SRCS
    FilenameTest.cpp
    ../src/ShortcutActions.cpp
    ../src/ExportManager.cpp
    ../src/Platforms/ImagePlatform.cpp
    ../src/Platforms/VideoPlatform.cpp
)

ecm_qt_declare_logging_category(FILENAME_TEST_SRCS
    HEADER spectacle_debug.h
    IDENTIFIER SPECTACLE_LOG
    CATEGORY_NAME spectacle
    DESCRIPTION "spectacle (general)"
    EXPORT SPECTACLE
)

# Generate the Settings class once; both filename_test and niri_capture_test
# need it (ImagePlatformNiri reads Settings on the main thread).
SET(NEKOSHOT_SETTINGS_SRCS)
kconfig_add_kcfg_files(NEKOSHOT_SETTINGS_SRCS GENERATE_MOC ${PROJECT_SOURCE_DIR}/src/Gui/SettingsDialog/settings.kcfgc)
list(APPEND FILENAME_TEST_SRCS ${NEKOSHOT_SETTINGS_SRCS})

ecm_add_test(
    ${FILENAME_TEST_SRCS}
    TEST_NAME "filename_test"
    LINK_LIBRARIES  Qt::Test
        Qt::PrintSupport Qt::Qml KF6::I18n KF6::ConfigCore KF6::GlobalAccel KF6::KIOCore KF6::WindowSystem KF6::XmlGui KF6::GuiAddons KF6::PrisonScanner
)

ecm_add_test(
    NiriIpcTest.cpp
    ../src/Platforms/NiriIpc.cpp
    TEST_NAME "niri_ipc_test"
    LINK_LIBRARIES Qt::Test Qt::Core Qt::Gui
)

ecm_add_test(NiriCaptureTest.cpp
    ../src/Platforms/ImagePlatformNiri.cpp
    ../src/Platforms/ImagePlatform.cpp
    ../src/Platforms/NiriPlatform.cpp
    ../src/Platforms/NiriIpc.cpp
    ../src/Platforms/NiriCapture.cpp
    ../src/Platforms/ClipboardGuard.cpp
    ../src/Platforms/ImageComposition.cpp
    ${NEKOSHOT_SETTINGS_SRCS}
    TEST_NAME "niri_capture_test"
    LINK_LIBRARIES Qt::Test Qt::Core Qt::Gui Qt::Qml Qt::Concurrent KF6::I18n KF6::ConfigCore KF6::ConfigGui
)

ecm_add_test(
    ClipboardGuardTest.cpp
    ../src/Platforms/ClipboardGuard.cpp
    TEST_NAME "clipboard_guard_test"
    LINK_LIBRARIES Qt::Test Qt::Core Qt::Gui
)

SET(WF_RECORDER_TEST_SRCS
    WfRecorderArgsTest.cpp
    ../src/Platforms/VideoPlatformNiri.cpp
    ../src/Platforms/VideoPlatform.cpp
    ../src/Platforms/NiriIpc.cpp
    ../src/Platforms/IdleInhibitor.cpp
    ../src/ExportManager.cpp
)

ecm_qt_declare_logging_category(WF_RECORDER_TEST_SRCS
    HEADER spectacle_debug.h
    IDENTIFIER SPECTACLE_LOG
    CATEGORY_NAME spectacle
    DESCRIPTION "spectacle (general)"
)

kconfig_add_kcfg_files(WF_RECORDER_TEST_SRCS GENERATE_MOC ${PROJECT_SOURCE_DIR}/src/Gui/SettingsDialog/settings.kcfgc)

ecm_add_test(
    ${WF_RECORDER_TEST_SRCS}
    TEST_NAME "wf_recorder_args_test"
    LINK_LIBRARIES Qt::Test Qt::Core Qt::Gui Qt::Qml Qt::PrintSupport Qt::Concurrent KF6::I18n KF6::Notifications KF6::ConfigCore KF6::ConfigGui KF6::KIOCore KF6::GuiAddons KF6::PrisonScanner
)

# Live-session smoke test. It returns 77 (skip) when NIRI_SOCKET is absent, so
# a headless CI runner still reports success.
add_test(NAME integration_smoke
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run-niri.sh $<TARGET_FILE:nekoshot>)
set_tests_properties(integration_smoke PROPERTIES SKIP_RETURN_CODE 77)

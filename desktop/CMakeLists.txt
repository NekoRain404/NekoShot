# install the .desktop and rc files in the correct place

configure_file(org.nekoshot.NekoShot.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.nekoshot.NekoShot.desktop)
install(
    PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/org.nekoshot.NekoShot.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install( DIRECTORY DESTINATION "${KDE_INSTALL_FULL_DATAROOTDIR}/kglobalaccel" )
install(
    CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"${KDE_INSTALL_FULL_APPDIR}/org.nekoshot.NekoShot.desktop\" \"\$ENV{DESTDIR}${KDE_INSTALL_FULL_DATAROOTDIR}/kglobalaccel/org.nekoshot.NekoShot.desktop\")"
)

install(
    FILES nekoshot.notifyrc
    DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
)

install(
    FILES org.nekoshot.NekoShot.appdata.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)

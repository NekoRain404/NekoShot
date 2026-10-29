# install the DBus service and interface files in the correct place
include(ECMGenerateDBusServiceFile)
include(ECMConfiguredInstall)

# Generate and install dbus-activated systemd service
ecm_install_configured_files(INPUT app-org.nekoshot.NekoShot.service.in DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})

ecm_generate_dbus_service_file(
   NAME org.nekoshot.NekoShot
   EXECUTABLE "${KDE_INSTALL_FULL_BINDIR}/nekoshot --dbus"
   SYSTEMD_SERVICE app-org.nekoshot.NekoShot.service
   DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
)

install(FILES org.nekoshot.NekoShot.xml DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})

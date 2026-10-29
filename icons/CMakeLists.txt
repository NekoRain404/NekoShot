# default icons for the hicolor theme.
# have ecm install them

ecm_install_icons(
    ICONS
        sc-apps-nekoshot.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

qt_add_resources(nekoshot "nekoshot_icons"
    PREFIX "/icons"
    FILES
        256-status-media-recording-started.webp
        256-status-media-recording-pulse.webp
        256-status-media-recording.webp
)

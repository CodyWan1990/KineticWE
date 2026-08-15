configure_file(org.freedesktop.impl.portal.desktop.kwe.desktop.in org.freedesktop.impl.portal.desktop.kwe.desktop @ONLY)
configure_file(org.freedesktop.impl.portal.desktop.kwe.cmake.in org.freedesktop.impl.portal.desktop.kwe.service @ONLY)

install(FILES kwe.portal DESTINATION ${KDE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
install(FILES kineticwe-portals.conf DESTINATION ${KDE_INSTALL_DATADIR}/xdg-desktop-portal)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.kwe.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.kwe.desktop DESTINATION ${KDE_INSTALL_APPDIR})

ecm_install_configured_files(INPUT plasma-xdg-desktop-portal-kwe.service.in @ONLY
            DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})

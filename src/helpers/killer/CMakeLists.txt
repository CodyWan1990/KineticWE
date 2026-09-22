########### next target ###############

set(kineticwe_killer_helper_SRCS killer.cpp killdialog.cpp killdialog.h)

ki18n_wrap_ui(kineticwe_killer_helper_SRCS killdialog.ui)

add_executable(kineticwe_killer_helper ${kineticwe_killer_helper_SRCS})

target_link_libraries(kineticwe_killer_helper
    KF6::AuthCore
    KF6::GuiAddons
    KF6::I18n
    KF6::Service
    KF6::WidgetsAddons
    KF6::WindowSystem
    Qt::GuiPrivate
    Qt::Widgets
    Qt::Concurrent
)

ecm_qt_declare_logging_category(kineticwe_killer_helper
    HEADER debug.h
    IDENTIFIER KWIN_KILLER
    CATEGORY_NAME org.kineticwe.killer
)

install(TARGETS kineticwe_killer_helper DESTINATION ${KDE_INSTALL_LIBEXECDIR})

configure_file(org.kineticwe.killer.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/org.kineticwe.killer.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kineticwe.killer.desktop DESTINATION ${KDE_INSTALL_APPDIR})

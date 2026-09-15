add_subdirectory(icons)

########### next target ###############
add_executable(kwin5_update_default_rules update_default_rules.cpp)
target_link_libraries(kwin5_update_default_rules
    KF6::ConfigCore
    Qt::Core
    Qt::DBus
)
install(TARGETS kwin5_update_default_rules DESTINATION ${KDE_INSTALL_LIBDIR}/kconf_update_bin/)

########### install files ###############

install(FILES org_kde_kwin.categories DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})

# Aurorae decoration themes vendored from the upstream Layan-kde release (see
# aurorae-themes/README.md for the pin and the update commands). Installed to
# <datadir>/aurorae/themes/<Name>/, where the Aurorae plugin and Kinetic
# Settings -> Title Bars find them. Only the theme dirs are installed; the
# LICENSE/AUTHORS/README stay in the source tree.
install(DIRECTORY
    aurorae-themes/Layan
    aurorae-themes/Layan-solid
    DESTINATION ${KDE_INSTALL_DATADIR}/aurorae/themes
)

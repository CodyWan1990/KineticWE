add_subdirectory(icons)

########### next target ###############

########### install files ###############

install(FILES kineticwe.categories DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})

# Default icon theme vendored from the upstream L4ki/Breeze-Chameleon-Icons
# repo (see icon-themes/README.md for the license, hash and update commands).
# Shipped as a compressed archive rather than ~9k loose files; extracted into
# the build tree here and installed to <datadir>/icons/, where the session
# (scripts/start-kineticwe.sh) seeds it as the default icon theme.
set(KINETICWE_ICON_THEME_NAME "Breeze-Round-Chameleon Dark Icons")
set(KINETICWE_ICON_THEME_ARCHIVE
    "${CMAKE_CURRENT_SOURCE_DIR}/icon-themes/Breeze-Round-Chameleon-Dark-Icons.tar.xz"
)
# Extract straight into this directory's build dir, which always exists: a
# custom command's WORKING_DIRECTORY is entered before any of its commands run,
# so pointing it at a directory the rule itself has to create would fail.
# The archive's top-level entry is the theme directory itself.
set(KINETICWE_ICON_THEME_DIR "${CMAKE_CURRENT_BINARY_DIR}/${KINETICWE_ICON_THEME_NAME}")
set(KINETICWE_ICON_THEME_STAMP "${KINETICWE_ICON_THEME_DIR}/index.theme")
add_custom_command(
    OUTPUT "${KINETICWE_ICON_THEME_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E tar xJf "${KINETICWE_ICON_THEME_ARCHIVE}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${KINETICWE_ICON_THEME_ARCHIVE}"
    COMMENT "Extracting vendored icon theme (${KINETICWE_ICON_THEME_NAME})"
    VERBATIM
)
add_custom_target(kineticwe-icon-themes ALL DEPENDS "${KINETICWE_ICON_THEME_STAMP}")
install(DIRECTORY "${KINETICWE_ICON_THEME_DIR}"
        DESTINATION ${KDE_INSTALL_ICONDIR})

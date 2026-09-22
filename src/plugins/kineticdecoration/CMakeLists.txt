# SPDX-FileCopyrightText: 2026 The KineticWE Developers
# SPDX-License-Identifier: GPL-2.0-or-later

# The first-party KineticWE window decoration. It is a normal KDecoration3
# C++ plugin (the only kind KWin's DecorationBridge loads), installed under the
# KineticWE decoration plugin namespace so it never collides with the stock
# KDE org.kde.kdecoration3 plugins.
#
# kineticwe.json is embedded by moc through K_PLUGIN_FACTORY_WITH_JSON
# (Q_PLUGIN_METADATA FILE). This KF6 CoreAddons kcoreaddons_add_plugin() has no
# JSON keyword, so the file is listed as a (non-compiled) source instead so moc
# resolves it next to kineticdecoration.cpp.
kcoreaddons_add_plugin(kineticdecoration
    SOURCES
        kineticbutton.cpp
        kineticdecoration.cpp
        kineticpalette.cpp
        kineticwe.json
    INSTALL_NAMESPACE "org.kineticwe.decoration"
)

# KWin discovers decoration plugins with KPluginMetaData::findPluginById(),
# which matches the plugin Id against the file name
# ("The id should be the same as the filename"). The Id in kineticwe.json is
# "kineticwe" (also the bridge default and the Kinetic Settings default), so
# the installed file has to be kineticwe.so, not kineticdecoration.so, or KWin
# finds no decoration plugin at all.
set_target_properties(kineticdecoration PROPERTIES OUTPUT_NAME kineticwe)

target_link_libraries(kineticdecoration
    KDecoration3::KDecoration
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    Qt::Gui
)

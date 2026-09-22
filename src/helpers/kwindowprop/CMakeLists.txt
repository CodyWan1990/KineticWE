# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2025 David Redondo <kde@david-redondo.de>

add_executable(kineticwe_windowprop main.cpp)
target_link_libraries(kineticwe_windowprop Qt::Core Qt::DBus)
install(TARGETS kineticwe_windowprop ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})


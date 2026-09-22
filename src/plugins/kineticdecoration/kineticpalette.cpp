/*
    SPDX-FileCopyrightText: 2026 The KineticWE Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kineticpalette.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QStandardPaths>

namespace KWin
{
namespace Decoration
{

QString kineticColorsPath()
{
    // The template runs with the real config home and writes
    // <real config home>/kineticwe/decoration.colors. The compositor's own
    // config root is redirected, so prefer the real home the session publishes.
    QString configHome = qEnvironmentVariable("KWE_REAL_CONFIG_HOME");
    if (configHome.isEmpty()) {
        configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }
    return configHome + QStringLiteral("/kineticwe/decoration.colors");
}

KineticColors loadKineticColors(KineticTheme theme)
{
    KineticColors colors;
    if (theme == KineticTheme::Auto) {
        return colors;
    }
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(kineticColorsPath(), KConfig::SimpleConfig);
    const QString groupName = theme == KineticTheme::Light ? QStringLiteral("Light") : QStringLiteral("Dark");
    if (!config->hasGroup(groupName)) {
        return colors;
    }
    const KConfigGroup group = config->group(groupName);
    colors.frame = QColor(group.readEntry(QStringLiteral("Frame"), QString()));
    colors.titleBar = QColor(group.readEntry(QStringLiteral("TitleBar"), QString()));
    colors.foreground = QColor(group.readEntry(QStringLiteral("Foreground"), QString()));
    colors.outline = QColor(group.readEntry(QStringLiteral("Outline"), QString()));
    return colors;
}

QColor kineticColor(const KineticColors &colors, const KDecoration3::DecoratedWindow *window, KDecoration3::ColorRole role, bool active)
{
    if (colors.isValid()) {
        switch (role) {
        case KDecoration3::ColorRole::Frame:
            return colors.frame.isValid() ? colors.frame : colors.titleBar;
        case KDecoration3::ColorRole::TitleBar:
            return active ? colors.titleBar : (colors.frame.isValid() ? colors.frame : colors.titleBar);
        case KDecoration3::ColorRole::Foreground:
            return colors.foreground.isValid() ? colors.foreground : (window ? window->color(KDecoration3::ColorGroup::Active, role) : QColor(Qt::white));
        }
    }
    if (window) {
        return window->color(active ? KDecoration3::ColorGroup::Active : KDecoration3::ColorGroup::Inactive, role);
    }
    return QColor(Qt::black);
}

}
}

/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
    SPDX-FileCopyrightText: 2015 Mika Allan Rauhala <mika.allan.rauhala@gmail.com>
    SPDX-FileCopyrightText: 2020 Carson Black <uhhadd@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "decorationpalette.h"
#include "config-kwin.h"

#include <KConfigGroup>

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>

namespace KWin
{
namespace Decoration
{

DecorationPalette::DecorationPalette(const QString &colorScheme)
    : m_colorScheme(colorScheme != QStringLiteral("kdeglobals") ? colorScheme : QString())
{
    if (m_colorScheme.isEmpty()) {
        m_colorSchemeConfig = KSharedConfig::openConfig(kineticweKdeGlobalsPath(), KConfig::FullConfig);
    } else {
        m_colorSchemeConfig = KSharedConfig::openConfig(m_colorScheme, KConfig::SimpleConfig);
    }
    m_watcher = KConfigWatcher::create(m_colorSchemeConfig);

    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, &DecorationPalette::update);

    // The palette file is written by external tools (the Noctalia theme
    // service merges its generated color scheme into kdeglobals with a raw
    // atomic write), and it is opened here by absolute path because the
    // compositor's config root is redirected (see kineticweKdeGlobalsPath()).
    // KConfigWatcher is DBus-notify only and explicitly refuses absolute
    // paths (see kconfigwatcher.cpp in KConfig), so it never fires for this
    // file and decoration colors would stay frozen until restart. Mirror
    // TilingController and add a QFileSystemWatcher fallback.
    const QString watchedFile = m_colorScheme.isEmpty() ? kineticweKdeGlobalsPath() : m_colorScheme;
    m_fileWatcher = new QFileSystemWatcher(this);
    m_fileWatcher->addPath(watchedFile);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, [this, watchedFile]() {
        // Writers use atomic-rename semantics which invalidate the inotify
        // watch on the original inode; re-adding the path restores the watch
        // on the new file.
        m_fileWatcher->addPath(watchedFile);
        m_fileReloadTimer.start();
    });

    // Debounce: an atomic write (write-to-tmp + rename) can produce several
    // fileChanged events, and the DBus KConfigWatcher may fire on top of
    // them. Coalesce everything into a single re-read + repaint.
    m_fileReloadTimer.setSingleShot(true);
    m_fileReloadTimer.setInterval(50);
    connect(&m_fileReloadTimer, &QTimer::timeout, this, &DecorationPalette::update);

    update();
}

bool DecorationPalette::isValid() const
{
    return true;
}

QColor DecorationPalette::color(KDecoration3::ColorGroup group, KDecoration3::ColorRole role) const
{
    using KDecoration3::ColorGroup;
    using KDecoration3::ColorRole;

    if (m_legacyColors.has_value()) {
        switch (role) {
        case ColorRole::Frame:
            switch (group) {
            case ColorGroup::Active:
                return m_legacyColors->activeFrameColor;
            case ColorGroup::Inactive:
                return m_legacyColors->inactiveFrameColor;
            default:
                return QColor();
            }
        case ColorRole::TitleBar:
            switch (group) {
            case ColorGroup::Active:
                return m_legacyColors->activeTitleBarColor;
            case ColorGroup::Inactive:
                return m_legacyColors->inactiveTitleBarColor;
            default:
                return QColor();
            }
        case ColorRole::Foreground:
            switch (group) {
            case ColorGroup::Active:
                return m_legacyColors->activeForegroundColor;
            case ColorGroup::Inactive:
                return m_legacyColors->inactiveForegroundColor;
            case ColorGroup::Warning:
                return m_legacyColors->warningForegroundColor;
            default:
                return QColor();
            }
        default:
            return QColor();
        }
    }

    switch (role) {
    case ColorRole::Frame:
        switch (group) {
        case ColorGroup::Active:
            return m_colors.active.background().color();
        case ColorGroup::Inactive:
            return m_colors.inactive.background().color();
        default:
            return QColor();
        }
    case ColorRole::TitleBar:
        switch (group) {
        case ColorGroup::Active:
            return m_colors.active.background().color();
        case ColorGroup::Inactive:
            return m_colors.inactive.background().color();
        default:
            return QColor();
        }
    case ColorRole::Foreground:
        switch (group) {
        case ColorGroup::Active:
            return m_colors.active.foreground().color();
        case ColorGroup::Inactive:
            return m_colors.inactive.foreground().color();
        case ColorGroup::Warning:
            return m_colors.inactive.foreground(KColorScheme::ForegroundRole::NegativeText).color();
        default:
            return QColor();
        }
    default:
        return QColor();
    }
}

QPalette DecorationPalette::palette() const
{
    return m_palette;
}

void DecorationPalette::update()
{
    m_colorSchemeConfig->sync();
    m_palette = KColorScheme::createApplicationPalette(m_colorSchemeConfig);

    if (KColorScheme::isColorSetSupported(m_colorSchemeConfig, KColorScheme::Header)) {
        m_colors.active = KColorScheme(QPalette::Normal, KColorScheme::Header, m_colorSchemeConfig);
        m_colors.inactive = KColorScheme(QPalette::Inactive, KColorScheme::Header, m_colorSchemeConfig);
        m_legacyColors.reset();
    } else {
        KConfigGroup wmConfig(m_colorSchemeConfig, QStringLiteral("WM"));

        if (!wmConfig.exists()) {
            m_colors.active = KColorScheme(QPalette::Normal, KColorScheme::Window, m_colorSchemeConfig);
            m_colors.inactive = KColorScheme(QPalette::Inactive, KColorScheme::Window, m_colorSchemeConfig);
            m_legacyColors.reset();
            return;
        }

        m_legacyColors = LegacyColors{};
        m_legacyColors->activeFrameColor = wmConfig.readEntry("frame", m_palette.color(QPalette::Active, QPalette::Window));
        m_legacyColors->inactiveFrameColor = wmConfig.readEntry("inactiveFrame", m_legacyColors->activeFrameColor);
        m_legacyColors->activeTitleBarColor = wmConfig.readEntry("activeBackground", m_palette.color(QPalette::Active, QPalette::Highlight));
        m_legacyColors->inactiveTitleBarColor = wmConfig.readEntry("inactiveBackground", m_legacyColors->inactiveTitleBarColor);
        m_legacyColors->activeForegroundColor = wmConfig.readEntry("activeForeground", m_palette.color(QPalette::Active, QPalette::HighlightedText));
        m_legacyColors->inactiveForegroundColor = wmConfig.readEntry("inactiveForeground", m_legacyColors->activeForegroundColor.darker());

        KConfigGroup windowColorsConfig(m_colorSchemeConfig, QStringLiteral("Colors:Window"));
        m_legacyColors->warningForegroundColor = windowColorsConfig.readEntry("ForegroundNegative", QColor(237, 21, 2));
    }

    Q_EMIT changed();
}

}
}

#include "moc_decorationpalette.cpp"

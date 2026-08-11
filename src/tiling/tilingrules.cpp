/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tilingrules.h"
#include "window.h"

#include <KConfigGroup>

namespace KWin
{

TilingRules::TilingRules()
{
}

TilingRules::TilingRules(const KConfigGroup &group)
{
    load(group);
}

void TilingRules::load(const KConfigGroup &group)
{
    m_ignoreClasses = group.readEntry("IgnoreClass", QStringList());
    m_ignoreTitles = group.readEntry("IgnoreTitle", QStringList());
    m_floatingClasses = group.readEntry("FloatingClass", QStringList());
    m_floatingTitles = group.readEntry("FloatingTitle", QStringList());
    m_floatUtility = group.readEntry("FloatUtility", true);
    m_floatDialog = group.readEntry("FloatDialog", true);
    m_floatTransient = group.readEntry("FloatTransient", true);

    // Windows that must never be tiled, regardless of user configuration:
    // system surfaces that deliberately present themselves as NORMAL-type,
    // resizable windows and would otherwise be swept into a tile.
    //
    // xwaylandvideobridge: its capture surface (ContentsWindow) is a
    // borderless, input-transparent, fully transparent (opacity 0) window
    // sized to the union of all screens and shown at startup so X11 clients
    // can capture it. Tiling it moves/resizes/decorates an invisible system
    // surface, presenting it as a black box covering a monitor.
    m_ignoreClasses.append(QStringLiteral("xwaylandvideobridge"));

    // Normalize class patterns: strip whitespace and lower-case for case-insensitive matching.
    auto normalize = [](QStringList &list) {
        for (QString &s : list) {
            s = s.trimmed().toLower();
        }
    };
    normalize(m_ignoreClasses);
    normalize(m_ignoreTitles);
    normalize(m_floatingClasses);
    normalize(m_floatingTitles);
}

bool TilingRules::isIgnored(const Window *window) const
{
    if (!window || !window->isClient()) {
        return true;
    }
    if (matchClass(window, m_ignoreClasses)) {
        return true;
    }
    if (matchTitle(window, m_ignoreTitles)) {
        return true;
    }
    // The xwaylandvideobridge capture surface can additionally be identified
    // by its window role ("ContentsWindow"), which is set even in cases where
    // WM_CLASS arrives late. Belt-and-braces on top of the class match so it
    // is never adopted into a tile and shown as a black box.
    if (window->windowRole().toLower().contains(QLatin1StringView("contentswindow"))) {
        return true;
    }
    return false;
}

bool TilingRules::isVideoBridgeSurface(const Window *window) const
{
    if (!window || !window->isClient()) {
        return false;
    }
    // Identified by its resource class; the window role ("ContentsWindow") is
    // a fallback that is present even when WM_CLASS has not arrived yet. This
    // matches isIgnored() above so that any window flagged here is also
    // floated (never tiled), keeping tiling and the transparency sanitization
    // in agreement about which surface they treat specially.
    if (window->resourceClass().toLower().contains(QLatin1StringView("xwaylandvideobridge"))) {
        return true;
    }
    return window->windowRole().toLower().contains(QLatin1StringView("contentswindow"));
}

bool TilingRules::isFloating(const Window *window) const
{
    if (!window) {
        return true;
    }
    if (!window->isResizable()) {
        return true;
    }
    if (m_floatDialog && window->isDialog()) {
        return true;
    }
    if (m_floatTransient && window->isTransient()) {
        return true;
    }
    if (m_floatUtility && window->isUtility()) {
        return true;
    }
    // On Wayland, dialog and utility windows are represented as transient windows
    // (xdg-toplevel with a parent). If floatDialog or floatUtility is enabled,
    // check transiency as a Wayland-compatible fallback.
    if ((m_floatDialog || m_floatUtility) && window->isTransient()) {
        return true;
    }
    if (window->isPopupWindow() || window->isAppletPopup()) {
        return true;
    }
    if (matchClass(window, m_floatingClasses)) {
        return true;
    }
    if (matchTitle(window, m_floatingTitles)) {
        return true;
    }
    return false;
}

TilingState::Mode TilingRules::initialMode(const Window *window) const
{
    if (isIgnored(window)) {
        return TilingState::Mode::Floating;
    }
    if (isFloating(window)) {
        return TilingState::Mode::Floating;
    }
    return TilingState::Mode::Tiled;
}

bool TilingRules::matchClass(const Window *window, const QStringList &patterns) const
{
    if (patterns.isEmpty()) {
        return false;
    }
    const QString resourceClass = window->resourceClass().toLower();
    const QString resourceName = window->resourceName().toLower();
    const QString desktopFile = window->desktopFileName().toLower();
    for (const QString &pattern : patterns) {
        if (resourceClass.contains(pattern) || resourceName.contains(pattern) || desktopFile.contains(pattern)) {
            return true;
        }
    }
    return false;
}

bool TilingRules::matchTitle(const Window *window, const QStringList &patterns) const
{
    if (patterns.isEmpty()) {
        return false;
    }
    const QString title = window->caption().toLower();
    for (const QString &pattern : patterns) {
        if (title.contains(pattern)) {
            return true;
        }
    }
    return false;
}

} // namespace KWin

/*
 * SPDX-FileCopyrightText: 2018 Alexander Volkov <a.volkov@rusbitech.ru>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef XDG_DESKTOP_PORTAL_KDE_UTILS_H
#define XDG_DESKTOP_PORTAL_KDE_UTILS_H

#include <QString>

class QWidget;
class QWindow;

enum class DialogResult : unsigned {
    Accepted = 0,
    Rejected = 1
};

class Utils
{
public:
    static void setParentWindow(QWidget *w, const QString &parent_window);
    static void setParentWindow(QWindow *w, const QString &parent_window);

    static QString applicationName(const QString &appId);

    // Builds the QApplication palette from kdeglobals and republishes the KDE
    // accent colour to QPalette::Accent / KIconLoader. Needed because the portal
    // runs without the session's Qt platform theme (see xdg-desktop-portal-kwe.cpp).
    static void applyKdeApplicationPalette();
    struct WarningContext {
        QString title;
        QString genericText;
        QString x11Text;
    };
    static void warnNoStreaming(const WarningContext &context);
};

#endif // XDG_DESKTOP_PORTAL_KDE_UTILS_H

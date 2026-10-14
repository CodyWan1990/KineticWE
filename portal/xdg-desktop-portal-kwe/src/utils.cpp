/*
 * SPDX-FileCopyrightText: 2018 Alexander Volkov <a.volkov@rusbitech.ru>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "utils.h"

#include "waylandintegration.h"

#include <KColorScheme>
#include <KConfigGroup>
#include <KIconLoader>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KWindowSystem>

#include <QApplication>
#include <QColor>
#include <QMessageBox>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QWidget>

void Utils::setParentWindow(QWidget *w, const QString &parent_window)
{
    if (parent_window.startsWith(QLatin1String("x11:"))) {
        w->setAttribute(Qt::WA_NativeWindow, true);
        setParentWindow(w->windowHandle(), parent_window);
    }
    if (parent_window.startsWith((QLatin1String("wayland:")))) {
        if (!w->window()->windowHandle()) {
            w->window()->winId(); // create QWindow
        }
        setParentWindow(w->window()->windowHandle(), parent_window);
    }
}

void Utils::setParentWindow(QWindow *w, const QString &parent_window)
{
    if (parent_window.startsWith(QLatin1String("x11:"))) {
        KWindowSystem::setMainWindow(w, QStringView(parent_window).mid(4).toULongLong(nullptr, 16));
    }
    if (parent_window.startsWith((QLatin1String("wayland:")))) {
        KWindowSystem::setMainWindow(w, parent_window.mid(strlen("wayland:")));
    }
}

QString Utils::applicationName(const QString &appId)
{
    QString applicationName;
    const QString desktopFile = appId + QStringLiteral(".desktop");
    const QStringList desktopFileLocations = QStandardPaths::locateAll(QStandardPaths::ApplicationsLocation, desktopFile, QStandardPaths::LocateFile);
    for (const QString &location : desktopFileLocations) {
        QSettings settings(location, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Desktop Entry"));
        if (settings.contains(QStringLiteral("X-GNOME-FullName"))) {
            applicationName = settings.value(QStringLiteral("X-GNOME-FullName")).toString();
        } else {
            applicationName = settings.value(QStringLiteral("Name")).toString();
        }

        if (!applicationName.isEmpty()) {
            break;
        }
    }

    // Use `appId` as a fallback in case we're unable to find actual application name
    if (applicationName.isEmpty()) {
        applicationName = appId;
    }

    return applicationName;
}

void Utils::applyKdeApplicationPalette()
{
    const KSharedConfigPtr kdeglobals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    QPalette palette = KColorScheme::createApplicationPalette(kdeglobals);

    // The session's Qt platform theme normally copies [General] AccentColor into
    // QPalette::Accent. Without it the palette only carries the colour scheme,
    // and KIconLoader/KIconEngine -- the only thing that recolours Breeze's
    // ColorScheme-Accent parts such as the folder body -- would fall back to the
    // icon theme's built-in colour.
    QColor accent = KConfigGroup(kdeglobals, QStringLiteral("General")).readEntry(QStringLiteral("AccentColor"), QColor());
    if (!accent.isValid()) {
        accent = KConfigGroup(kdeglobals, QStringLiteral("Colors:Selection"))
                     .readEntry(QStringLiteral("BackgroundNormal"), palette.color(QPalette::Highlight));
    }
    if (accent.isValid()) {
        palette.setColor(QPalette::Accent, accent);
    }

    qApp->setPalette(palette);
    KIconLoader::global()->setCustomPalette(palette);
}

void Utils::warnNoStreaming(const WarningContext &context)
{
    Q_ASSERT(!context.title.isEmpty());
    Q_ASSERT(!context.genericText.isEmpty());
    Q_ASSERT(!context.x11Text.isEmpty());

    auto x11 = QGuiApplication::platformName() == QLatin1String("xcb");
    auto box = new QMessageBox(QMessageBox::Critical, context.title, x11 ? context.x11Text : context.genericText);
    box->show();
}

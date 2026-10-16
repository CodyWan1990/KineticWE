#pragma once
#include <QDir>
#include <QLatin1StringView>
#include <QStandardPaths>
#include <QString>
#include <QtGlobal>

#define KWIN_PLUGIN_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
#define PROJECT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR}
#define PROJECT_VERSION_MINOR ${PROJECT_VERSION_MINOR}
#define PROJECT_VERSION_PATCH ${PROJECT_VERSION_PATCH}

#cmakedefine01 KWIN_BUILD_DECORATIONS
#cmakedefine01 KWIN_BUILD_KCMS
#cmakedefine01 KWIN_BUILD_NOTIFICATIONS
#cmakedefine01 KWIN_BUILD_SCREENLOCKER
#cmakedefine01 KWIN_HAVE_KSCREENLOCKER_SUSPEND_API
#cmakedefine01 KWIN_BUILD_TABBOX
#cmakedefine01 KWIN_BUILD_ACTIVITIES
#cmakedefine01 KWIN_BUILD_GLOBALSHORTCUTS
#cmakedefine01 KWIN_BUILD_X11
#cmakedefine01 KWIN_BUILD_QACCESSIBILITYCLIENT
constexpr QLatin1StringView KWIN_CONFIG("kineticwe.kwe");

// The compositor is launched with XDG_CONFIG_HOME pointed at the kineticwe
// config root (~/.config/kineticwe), so relative KConfig names resolve there.
// Shared KDE state that user applications also read (kdeglobals) lives in the
// user's REAL config home, which the session script carries in
// KWE_REAL_CONFIG_HOME. Falls back to ~/.config for standalone runs.
inline QString kineticweRealConfigDir()
{
    // In autotest mode QStandardPaths redirects configs to a test dir; keep
    // the old relative-name behavior so tests stay hermetic.
    if (QStandardPaths::isTestModeEnabled()) {
        return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    const QString realConfig = qEnvironmentVariable("KWE_REAL_CONFIG_HOME");
    if (!realConfig.isEmpty()) {
        return realConfig;
    }
    return QDir::homePath() + QStringLiteral("/.config");
}

inline QString kineticweKdeGlobalsPath()
{
    return kineticweRealConfigDir() + QStringLiteral("/kdeglobals");
}
constexpr QLatin1StringView KWIN_VERSION_STRING("${PROJECT_VERSION}");
constexpr QLatin1StringView XCB_VERSION_STRING("${XCB_VERSION}");
constexpr QLatin1StringView KWIN_KILLER_BIN("${KWIN_KILLER_BIN}");
constexpr QLatin1StringView KWIN_DIALOG_BIN("${KWIN_DIALOG_BIN}");
constexpr QLatin1StringView LIBEXEC_DIR("${CMAKE_INSTALL_FULL_LIBEXECDIR}");
#cmakedefine01 HAVE_X11_XCB
#cmakedefine01 HAVE_X11_XINPUT
#cmakedefine01 HAVE_GBM_BO_GET_FD_FOR_PLANE
#cmakedefine01 HAVE_GBM_BO_CREATE_WITH_MODIFIERS2
#cmakedefine01 HAVE_MEMFD
#cmakedefine01 HAVE_SCHED_RESET_ON_FORK
#cmakedefine01 HAVE_XKBCOMMON_NO_SECURE_GETENV
#cmakedefine01 HAVE_DL_LIBRARY
#cmakedefine01 HAVE_LIBINPUT_PLUGINS
#cmakedefine01 HAVE_SYSTEMD

constexpr QLatin1StringView XWAYLAND_SESSION_SCRIPTS("${XWAYLAND_SESSION_SCRIPTS}");

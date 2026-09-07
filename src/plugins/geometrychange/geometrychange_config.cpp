/*
    SPDX-FileCopyrightText: 2026 KineticWE contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "geometrychange_config.h"

#include "config-kwin.h"

// KConfigSkeleton
#include "geometrychangeconfig.h"

#include <kwineffects_interface.h>

#include <KPluginFactory>
#include <kconfiggroup.h>

#include <QVBoxLayout>

K_PLUGIN_CLASS(KWin::GeometryChangeEffectConfig)

namespace KWin
{

GeometryChangeEffectConfig::GeometryChangeEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    m_ui.setupUi(widget());

    GeometryChangeConfig::instance(KWIN_CONFIG);
    addConfig(GeometryChangeConfig::self(), widget());
}

void GeometryChangeEffectConfig::save()
{
    KCModule::save();
    OrgKdeKwinEffectsInterface interface(QStringLiteral("org.kde.KWin"),
                                         QStringLiteral("/Effects"),
                                         QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("geometrychange"));
}

} // namespace

#include "geometrychange_config.moc"

#include "moc_geometrychange_config.cpp"

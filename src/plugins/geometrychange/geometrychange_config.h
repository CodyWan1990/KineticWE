/*
    SPDX-FileCopyrightText: 2026 KineticWE contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <kcmodule.h>

#include "ui_geometrychange_config.h"

namespace KWin
{

class GeometryChangeEffectConfig : public KCModule
{
    Q_OBJECT

public:
    explicit GeometryChangeEffectConfig(QObject *parent, const KPluginMetaData &data);

public Q_SLOTS:
    void save() override;

private:
    Ui::GeometryChangeEffectConfigForm m_ui;
};

} // namespace

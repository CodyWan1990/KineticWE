/*
    SPDX-FileCopyrightText: 2026 KineticWE contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "geometrychange.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(GeometryChangeEffect,
                              "metadata.json.stripped",
                              return GeometryChangeEffect::supported();)

} // namespace KWin

#include "main.moc"

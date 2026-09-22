/*
    SPDX-FileCopyrightText: 2026 The KineticWE Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KDecoration3/Decoration>
#include <KDecoration3/DecorationButtonGroup>

#include "kineticpalette.h"

#include <memory>

class QFileSystemWatcher;

namespace KWin
{
namespace Decoration
{

/**
 * The first-party KineticWE window decoration.
 *
 * A small C++ KDecoration plugin that paints a flat, standard-looking title
 * bar. Colors come from the Noctalia "kineticwe" template when a variant is
 * selected, or from the shared color scheme in Auto mode, so the title bar
 * follows (and recolors with) the Noctalia theme.
 */
class KineticDecoration : public KDecoration3::Decoration
{
    Q_OBJECT

public:
    explicit KineticDecoration(QObject *parent, const QVariantList &args);
    ~KineticDecoration() override;

    bool init() override;
    void paint(QPainter *painter, const QRectF &repaintArea) override;

    /**
     * Resolved color for a role, for this decoration's variant. Used by the
     * buttons so they stay in sync with the title bar.
     */
    QColor colorFor(KDecoration3::ColorRole role, bool active) const;

    /**
     * Whether the variant forced colors (template palette present) rather than
     * following the shared color scheme.
     */
    bool forcedColors() const;

private:
    void updateBorders();
    void updateTitleBar();
    void reloadColors();

    qreal buttonSize() const;
    qreal frameThickness() const;

    std::unique_ptr<KDecoration3::DecorationButtonGroup> m_leftButtons;
    std::unique_ptr<KDecoration3::DecorationButtonGroup> m_rightButtons;
    QFileSystemWatcher *m_watcher = nullptr;
    KineticTheme m_theme = KineticTheme::Dark;
    KineticColors m_colors;
};

}
}

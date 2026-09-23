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

    /**
     * How this variant draws its buttons (standard glyphs or dots).
     */
    KineticButtonStyle buttonStyle() const;

    /**
     * The side length of a titlebar button, in device pixels. Uses the
     * user-configured value from kineticwe.kwe when present, otherwise the
     * grid-unit-derived default. Also used by the buttons themselves.
     */
    qreal buttonSize() const;

private:
    void updateBorders();
    void updateTitleBar();
    void reloadColors();

    // Reads the Kinetic titlebar metrics (height/font/button size) from the
    // compositor config; 0 means "not configured, use the derived default".
    void loadMetrics();
    // (Re)creates the left/right button groups. Called from init() and again
    // from updateBorders() when the configured button size changes, since
    // buttons size themselves only when constructed.
    void createButtonGroups();

    qreal frameThickness() const;

    std::unique_ptr<KDecoration3::DecorationButtonGroup> m_leftButtons;
    std::unique_ptr<KDecoration3::DecorationButtonGroup> m_rightButtons;
    QFileSystemWatcher *m_watcher = nullptr;
    KineticTheme m_theme = KineticTheme::Dark;
    KineticButtonStyle m_buttonStyle = KineticButtonStyle::Standard;
    KineticColors m_colors;

    // User-configured metrics ([org.kde.kdecoration2] in kineticwe.kwe).
    // 0 = unset; the derived default is used instead.
    int m_titleBarHeight = 0;     // logical pixels
    int m_titleFontPointSize = 0; // points
    int m_buttonSize = 0;         // logical pixels
    // Whether the window caption text is painted (KineticShowTitle; default on).
    bool m_showTitle = true;
    // Cached button size (device pixels) so a size change can rebuild the
    // button groups; 0 before the first updateBorders().
    qreal m_lastButtonSize = 0.0;
};

}
}

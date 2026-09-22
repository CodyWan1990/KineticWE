/*
    SPDX-FileCopyrightText: 2026 The KineticWE Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kineticdecoration.h"
#include "kineticbutton.h"

#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/DecorationButtonGroup>
#include <KDecoration3/DecorationSettings>

#include <KPluginFactory>

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

namespace KWin
{
namespace Decoration
{

KineticDecoration::KineticDecoration(QObject *parent, const QVariantList &args)
    : KDecoration3::Decoration(parent, args)
{
    // The decoration bridge passes the configured theme in the argument map
    // ("dark" / "light" / "dots-dark" / "dots-light"); anything else falls
    // back to following the scheme with standard buttons.
    if (!args.isEmpty()) {
        const QString theme = args.first().toMap().value(QStringLiteral("theme")).toString();
        m_theme = kineticThemeFromString(theme);
        m_buttonStyle = kineticButtonStyleFromString(theme);
    }
    m_colors = loadKineticColors(m_theme);
}

KineticDecoration::~KineticDecoration() = default;

QColor KineticDecoration::colorFor(KDecoration3::ColorRole role, bool active) const
{
    return kineticColor(m_colors, window(), role, active);
}

bool KineticDecoration::forcedColors() const
{
    return m_colors.isValid();
}

KineticButtonStyle KineticDecoration::buttonStyle() const
{
    return m_buttonStyle;
}

void KineticDecoration::reloadColors()
{
    const KineticColors colors = loadKineticColors(m_theme);
    if (colors.frame == m_colors.frame && colors.titleBar == m_colors.titleBar
        && colors.foreground == m_colors.foreground && colors.outline == m_colors.outline) {
        return;
    }
    m_colors = colors;
    updateBorders();
}

qreal KineticDecoration::buttonSize() const
{
    const std::shared_ptr<KDecoration3::DecorationSettings> s = settings();
    const qreal scale = window() ? window()->scale() : 1.0;
    const qreal unit = s ? s->gridUnit() : 18.0;
    return qBound<qreal>(14.0, unit * 1.2, 32.0) * scale;
}

qreal KineticDecoration::frameThickness() const
{
    const std::shared_ptr<KDecoration3::DecorationSettings> s = settings();
    const qreal scale = window() ? window()->scale() : 1.0;
    if (!s) {
        return 4.0 * scale;
    }
    qreal t = 4.0;
    switch (s->borderSize()) {
    case KDecoration3::BorderSize::None:
        t = 0.0;
        break;
    case KDecoration3::BorderSize::NoSides:
        t = 2.0;
        break;
    case KDecoration3::BorderSize::Tiny:
        t = 2.0;
        break;
    case KDecoration3::BorderSize::Normal:
        t = 4.0;
        break;
    case KDecoration3::BorderSize::Large:
        t = 6.0;
        break;
    case KDecoration3::BorderSize::VeryLarge:
        t = 8.0;
        break;
    case KDecoration3::BorderSize::Huge:
        t = 12.0;
        break;
    case KDecoration3::BorderSize::VeryHuge:
        t = 18.0;
        break;
    case KDecoration3::BorderSize::Oversized:
        t = 27.0;
        break;
    }
    return t * scale;
}

bool KineticDecoration::init()
{
    auto creator = [](KDecoration3::DecorationButtonType type, KDecoration3::Decoration *decoration, QObject *parent) -> KDecoration3::DecorationButton * {
        return new KineticButton(type, decoration, parent);
    };
    m_leftButtons = std::make_unique<KDecoration3::DecorationButtonGroup>(KDecoration3::DecorationButtonGroup::Position::Left, this, creator);
    m_rightButtons = std::make_unique<KDecoration3::DecorationButtonGroup>(KDecoration3::DecorationButtonGroup::Position::Right, this, creator);
    const std::shared_ptr<KDecoration3::DecorationSettings> s = settings();
    if (s) {
        const qreal spacing = qMax<qreal>(2.0, s->smallSpacing() * (window() ? window()->scale() : 1.0) * 0.5);
        m_leftButtons->setSpacing(spacing);
        m_rightButtons->setSpacing(spacing);
    }

    if (KDecoration3::DecoratedWindow *w = window()) {
        // update() is overloaded on Decoration, so disambiguate with lambdas.
        // activeChanged also refreshes the (variant-dependent) border outline.
        connect(w, &KDecoration3::DecoratedWindow::activeChanged, this, &KineticDecoration::updateBorders);
        connect(w, &KDecoration3::DecoratedWindow::maximizedChanged, this, &KineticDecoration::updateBorders);
        connect(w, &KDecoration3::DecoratedWindow::widthChanged, this, &KineticDecoration::updateBorders);
        connect(w, &KDecoration3::DecoratedWindow::heightChanged, this, &KineticDecoration::updateBorders);
        connect(w, &KDecoration3::DecoratedWindow::captionChanged, this, [this] { update(); });
        // The window-menu button draws the app icon, so repaint on icon changes.
        connect(w, &KDecoration3::DecoratedWindow::iconChanged, this, [this] { update(); });
        // Live recolor: KWin re-reads the shared color scheme and notifies the
        // window, which repaints the decoration with the new colors.
        connect(w, &KDecoration3::DecoratedWindow::paletteChanged, this, [this] { update(); });
    }
    if (s) {
        connect(s.get(), &KDecoration3::DecorationSettings::reconfigured, this, &KineticDecoration::updateBorders);
    }

    // Watch the Noctalia-generated colors file so a theme change recolors the
    // title bar without a compositor reload.
    if (m_theme != KineticTheme::Auto) {
        const QString path = kineticColorsPath();
        m_watcher = new QFileSystemWatcher(this);
        // The template writer rewrites the file in place (which emits
        // fileChanged but not directoryChanged); an atomic replace is the
        // opposite. Watch both, and re-add the file watch whenever it is lost
        // (a replace drops the watch on the old inode).
        m_watcher->addPath(path);
        m_watcher->addPath(QFileInfo(path).absolutePath());
        const auto reload = [this, path] {
            if (QFileInfo::exists(path) && !m_watcher->files().contains(path)) {
                m_watcher->addPath(path);
            }
            reloadColors();
        };
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, reload);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, reload);
    }

    updateBorders();
    return true;
}

void KineticDecoration::updateTitleBar()
{
    const qreal side = borders().left();
    const qreal width = window() ? window()->width() + side + borders().right() : size().width();
    const qreal title = borders().top();
    setTitleBar(QRectF(0, 0, width, title));

    const std::shared_ptr<KDecoration3::DecorationSettings> s = settings();
    const qreal padding = s ? qMax<qreal>(4.0, s->smallSpacing() * 1.5) : 6.0;
    const qreal leftY = (title - m_leftButtons->geometry().height()) / 2.0;
    m_leftButtons->setPos(QPointF(padding, qMax<qreal>(0.0, leftY)));
    const qreal rightY = (title - m_rightButtons->geometry().height()) / 2.0;
    m_rightButtons->setPos(QPointF(width - padding - m_rightButtons->geometry().width(), qMax<qreal>(0.0, rightY)));
}

void KineticDecoration::updateBorders()
{
    const std::shared_ptr<KDecoration3::DecorationSettings> s = settings();
    const qreal scale = window() ? window()->scale() : 1.0;
    const qreal padding = s ? qMax<qreal>(4.0, s->smallSpacing() * 1.5) : 6.0;

    qreal side = frameThickness();
    qreal bottom = side;
    if (s && s->borderSize() == KDecoration3::BorderSize::NoSides) {
        side = 0.0;
    }
    if (window() && (window()->isMaximized() || window()->isMaximizedHorizontally())) {
        side = 0.0;
    }
    if (window() && (window()->isMaximized() || window()->isMaximizedVertically())) {
        bottom = 0.0;
    }

    const qreal title = qMax(buttonSize() + padding, side);
    setBorders(QMarginsF(side, title, side, bottom));
    setOpaque(true);

    if (window()) {
        const KDecoration3::BorderRadius radius = window()->isMaximized() ? KDecoration3::BorderRadius(0.0) : KDecoration3::BorderRadius(qMax<qreal>(0.0, 8.0 * scale));
        setBorderRadius(radius);
        if (m_theme != KineticTheme::Auto) {
            const qreal thickness = qMax<qreal>(1.0, scale);
            const QColor outline = m_colors.outline.isValid() ? m_colors.outline : colorFor(KDecoration3::ColorRole::TitleBar, window()->isActive()).darker(130);
            setBorderOutline(KDecoration3::BorderOutline(thickness, outline, radius));
        }
    }

    updateTitleBar();
    update();
}

void KineticDecoration::paint(QPainter *painter, const QRectF &repaintArea)
{
    Q_UNUSED(repaintArea)

    const KDecoration3::DecoratedWindow *w = window();
    const bool active = w ? w->isActive() : true;
    const QColor frameColor = colorFor(KDecoration3::ColorRole::Frame, active);
    const QColor titleColor = colorFor(KDecoration3::ColorRole::TitleBar, active);
    const QColor captionColor = colorFor(KDecoration3::ColorRole::Foreground, active);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Frame and title bar share the frame color; the title bar uses its own.
    painter->setPen(Qt::NoPen);
    painter->setBrush(frameColor);
    painter->drawRect(rect());

    painter->setBrush(titleColor);
    painter->drawRect(titleBar());

    // Caption.
    const std::shared_ptr<KDecoration3::DecorationSettings> s = settings();
    QFont font = s ? s->font() : painter->font();
    // The default title font is a touch large for the short title bar, so
    // scale it down ~10% (with a floor) to give the caption some breathing room.
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(qMax<qreal>(8.0, font.pointSizeF() * 0.9));
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(qMax(10, qRound(font.pixelSize() * 0.9)));
    }
    painter->setFont(font);
    painter->setPen(captionColor);
    const qreal padding = s ? qMax<qreal>(4.0, s->smallSpacing() * 1.5) : 6.0;
    const qreal leftGroup = m_leftButtons ? m_leftButtons->geometry().width() : 0.0;
    const qreal rightGroup = m_rightButtons ? m_rightButtons->geometry().width() : 0.0;
    const QRectF captionRect = titleBar().adjusted(leftGroup + padding * 2.0, 0.0, -(rightGroup + padding * 2.0), 0.0);
    if (w && captionRect.width() > 0) {
        const QFontMetricsF fm(font);
        const QString elided = fm.elidedText(w->caption(), Qt::ElideMiddle, captionRect.width());
        painter->drawText(captionRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
    }

    // Border outline (drawn on top of everything else, including buttons).
    const KDecoration3::BorderOutline outline = borderOutline();
    if (!outline.isNull() && outline.thickness() > 0.0 && outline.color().alpha() > 0) {
        painter->setBrush(Qt::NoBrush);
        QPen pen(outline.color(), outline.thickness());
        painter->setPen(pen);
        const qreal half = outline.thickness() / 2.0;
        painter->drawRect(rect().adjusted(half, half, -half, -half));
    }

    if (m_leftButtons) {
        m_leftButtons->paint(painter, repaintArea);
    }
    if (m_rightButtons) {
        m_rightButtons->paint(painter, repaintArea);
    }

    painter->restore();
}

}
}

K_PLUGIN_FACTORY_WITH_JSON(KineticDecorationFactory, "kineticwe.json", registerPlugin<KWin::Decoration::KineticDecoration>();)

#include "kineticdecoration.moc"

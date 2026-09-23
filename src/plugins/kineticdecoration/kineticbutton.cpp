/*
    SPDX-FileCopyrightText: 2026 The KineticWE Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kineticbutton.h"
#include "kineticdecoration.h"

#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/Decoration>

#include <QIcon>
#include <QPainter>
#include <QPainterPath>

namespace KWin
{
namespace Decoration
{

KineticDecoration *KineticButton::kinetic() const
{
    return static_cast<KineticDecoration *>(decoration());
}

KineticButton::KineticButton(KDecoration3::DecorationButtonType type, KDecoration3::Decoration *decoration, QObject *parent)
    : KDecoration3::DecorationButton(type, decoration, parent)
{
    // The button group lays the buttons out using their current size(), so a
    // preferred size has to be set here. It comes from the decoration, which
    // uses the user-configured size from kineticwe.kwe when present and the
    // grid-unit-derived default otherwise.
    const qreal size = kinetic()->buttonSize();
    setGeometry(QRectF(QPointF(0, 0), QSizeF(size, size)));
}

KineticButton::~KineticButton() = default;

QColor KineticButton::glyphColor() const
{
    const KDecoration3::DecoratedWindow *w = decoration()->window();
    const bool active = w ? w->isActive() : true;
    if (type() == KDecoration3::DecorationButtonType::Close && (isHovered() || isPressed()) && !kinetic()->forcedColors() && w) {
        // Warning foreground is typically red and only valid with the
        // Foreground role.
        const QColor warning = w->color(KDecoration3::ColorGroup::Warning, KDecoration3::ColorRole::Foreground);
        if (warning.isValid()) {
            return warning;
        }
    }
    return kinetic()->colorFor(KDecoration3::ColorRole::Foreground, active);
}

QColor KineticButton::hoverColor() const
{
    const KDecoration3::DecoratedWindow *w = decoration()->window();
    const bool active = w ? w->isActive() : true;
    QColor c = kinetic()->colorFor(KDecoration3::ColorRole::Foreground, active);
    c.setAlphaF(isPressed() ? 0.28 : 0.14);
    return c;
}

QColor KineticButton::dotColor(bool hovered) const
{
    const KDecoration3::DecoratedWindow *w = decoration()->window();
    const bool active = w ? w->isActive() : true;
    if (hovered && type() == KDecoration3::DecorationButtonType::Close) {
        // The close dot turns red on hover, matching the theme preview. Fall
        // back to a generic red when the scheme provides no warning colour.
        QColor close = w ? w->color(KDecoration3::ColorGroup::Warning, KDecoration3::ColorRole::Foreground) : QColor();
        if (!close.isValid()) {
            close = QColor(0xDA, 0x44, 0x53);
        }
        close.setAlphaF(0.9);
        return close;
    }
    QColor c = kinetic()->colorFor(KDecoration3::ColorRole::Foreground, active);
    c.setAlphaF(hovered ? 0.8 : 0.55);
    return c;
}

void KineticButton::paint(QPainter *painter, const QRectF &repaintArea)
{
    Q_UNUSED(repaintArea)

    if (!isVisible()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = geometry();
    const qreal side = qMin(r.width(), r.height());
    const QRectF square(r.center().x() - side / 2.0, r.center().y() - side / 2.0, side, side);

    if (kinetic()->buttonStyle() == KineticButtonStyle::Dots) {
        paintDots(painter, square, side);
    } else {
        paintStandard(painter, square, side);
    }

    painter->restore();
}

void KineticButton::paintStandard(QPainter *painter, const QRectF &square, qreal side)
{
    if (isHovered() || isPressed()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(hoverColor());
        painter->drawEllipse(square.adjusted(side * 0.12, side * 0.12, -side * 0.12, -side * 0.12));
    }

    QPen pen(glyphColor());
    pen.setWidthF(qMax<qreal>(1.2, side * 0.09));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    // Glyph box centered in the button.
    const qreal g = side * 0.30;
    const QRectF box(square.center().x() - g, square.center().y() - g, g * 2.0, g * 2.0);
    drawGlyph(painter, box, pen.color());
}

void KineticButton::paintDots(QPainter *painter, const QRectF &square, qreal side)
{
    // The window-menu button keeps its app icon and a spacer draws nothing.
    if (type() == KDecoration3::DecorationButtonType::Menu
        || type() == KDecoration3::DecorationButtonType::ApplicationMenu
        || type() == KDecoration3::DecorationButtonType::Spacer) {
        paintStandard(painter, square, side);
        return;
    }

    const bool hovered = isHovered() || isPressed();
    const QPointF center = square.center();

    painter->setPen(Qt::NoPen);
    painter->setBrush(dotColor(hovered));
    const qreal radius = side * 0.30;
    painter->drawEllipse(center, radius, radius);

    if (!hovered) {
        return;
    }

    // Reveal the glyph, drawn in the title-bar colour so it reads as a cut-out
    // of the dot.
    const KDecoration3::DecoratedWindow *w = decoration()->window();
    const bool active = w ? w->isActive() : true;
    QPen pen(kinetic()->colorFor(KDecoration3::ColorRole::TitleBar, active));
    pen.setWidthF(qMax<qreal>(1.2, side * 0.075));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const qreal g = side * 0.17;
    const QRectF box(center.x() - g, center.y() - g, g * 2.0, g * 2.0);
    drawGlyph(painter, box, pen.color());
}

void KineticButton::drawGlyph(QPainter *painter, const QRectF &box, const QColor &color)
{
    switch (type()) {
    case KDecoration3::DecorationButtonType::Minimize: {
        const qreal y = box.center().y() + box.height() * 0.28;
        painter->drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
        break;
    }
    case KDecoration3::DecorationButtonType::Maximize: {
        const bool restore = isChecked();
        // Half the square edge, sized to sit alongside the minimize line and
        // close cross, which span ~0.6 of the button.
        const qreal h = box.width() * 0.44;
        const QPointF c = box.center();
        if (restore) {
            const qreal off = h * 0.65;
            painter->drawRect(QRectF(c.x() - h - off, c.y() - h - off, h * 2.0, h * 2.0));
            painter->drawRect(QRectF(c.x() - h + off, c.y() - h + off, h * 2.0, h * 2.0));
        } else {
            painter->drawRect(QRectF(c.x() - h, c.y() - h, h * 2.0, h * 2.0));
        }
        break;
    }
    case KDecoration3::DecorationButtonType::Close: {
        const qreal d = box.width() * 0.42;
        const QPointF c = box.center();
        painter->drawLine(c + QPointF(-d, -d), c + QPointF(d, d));
        painter->drawLine(c + QPointF(d, -d), c + QPointF(-d, d));
        break;
    }
    case KDecoration3::DecorationButtonType::OnAllDesktops: {
        painter->setBrush(color);
        painter->drawEllipse(box.center(), box.width() * 0.22, box.width() * 0.22);
        break;
    }
    case KDecoration3::DecorationButtonType::Shade: {
        QPainterPath path;
        path.moveTo(box.left(), box.center().y());
        path.lineTo(box.center().x(), box.bottom());
        path.lineTo(box.right(), box.center().y());
        painter->drawPath(path);
        break;
    }
    case KDecoration3::DecorationButtonType::KeepAbove:
    case KDecoration3::DecorationButtonType::KeepBelow: {
        const bool above = type() == KDecoration3::DecorationButtonType::KeepAbove;
        QPainterPath path;
        const qreal y = above ? box.bottom() : box.top();
        const qreal tip = above ? box.top() : box.bottom();
        path.moveTo(box.left(), y);
        path.lineTo(box.right(), y);
        path.lineTo(box.center().x(), tip);
        path.closeSubpath();
        painter->drawPath(path);
        break;
    }
    case KDecoration3::DecorationButtonType::ContextHelp: {
        painter->drawText(box, Qt::AlignCenter, QStringLiteral("?"));
        break;
    }
    case KDecoration3::DecorationButtonType::Menu:
    case KDecoration3::DecorationButtonType::ApplicationMenu: {
        // The window-menu button shows the app icon; the generic menu dots are
        // only a fallback for windows that have no icon.
        const KDecoration3::DecoratedWindow *w = decoration()->window();
        const QIcon icon = w ? w->icon() : QIcon();
        if (!icon.isNull()) {
            const qreal iconSize = box.width() * 1.2;
            const QRectF iconRect(box.center().x() - iconSize / 2.0, box.center().y() - iconSize / 2.0, iconSize, iconSize);
            if (w && !w->isActive()) {
                painter->setOpacity(0.7);
            }
            icon.paint(painter, iconRect.toRect(), Qt::AlignCenter, QIcon::Normal);
            painter->setOpacity(1.0);
            break;
        }
        painter->setBrush(color);
        painter->setPen(Qt::NoPen);
        for (int i = -1; i <= 1; ++i) {
            painter->drawEllipse(box.center() + QPointF(0, i * box.height() * 0.34), box.width() * 0.09, box.width() * 0.09);
        }
        break;
    }
    case KDecoration3::DecorationButtonType::Spacer:
        break;
    default: {
        // Custom/unhandled types: draw a neutral dot so the button is still
        // discoverable and clickable.
        painter->setBrush(color);
        painter->drawEllipse(box.center(), box.width() * 0.18, box.width() * 0.18);
        break;
    }
    }
}

}
}

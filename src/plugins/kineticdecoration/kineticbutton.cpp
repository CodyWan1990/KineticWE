/*
    SPDX-FileCopyrightText: 2026 The KineticWE Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kineticbutton.h"
#include "kineticdecoration.h"

#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/Decoration>
#include <KDecoration3/DecorationSettings>

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
    // preferred size has to be set here. It is derived from the grid unit so
    // the button scales with the user's font/DPI settings.
    const std::shared_ptr<KDecoration3::DecorationSettings> s = decoration->settings();
    const qreal scale = decoration->window() ? decoration->window()->scale() : 1.0;
    const qreal unit = s ? qMax<qreal>(14.0, s->gridUnit() * 1.2) : 18.0;
    const qreal size = qBound<qreal>(14.0, unit * scale, 32.0);
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

    switch (type()) {
    case KDecoration3::DecorationButtonType::Minimize: {
        const qreal y = box.center().y() + box.height() * 0.28;
        painter->drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
        break;
    }
    case KDecoration3::DecorationButtonType::Maximize: {
        const bool restore = isChecked();
        const qreal d = box.width() * 0.74;
        if (restore) {
            const QRectF back(box.left() + box.width() * 0.22, box.top(), d, d);
            painter->drawRect(back.adjusted(0, 0, -back.width() * 0.28, -back.height() * 0.28));
            const QRectF front(box.left(), box.top() + box.height() * 0.24, d, d);
            painter->drawRect(front.adjusted(0, 0, -front.width() * 0.28, -front.height() * 0.28));
        } else {
            const QRectF front(box.left() + box.width() * 0.1, box.top() + box.height() * 0.1, d, d);
            painter->drawRect(front.adjusted(0, 0, -front.width() * 0.28, -front.height() * 0.28));
        }
        break;
    }
    case KDecoration3::DecorationButtonType::Close: {
        const qreal d = box.width() * 0.5;
        const QPointF c = box.center();
        painter->drawLine(c + QPointF(-d, -d), c + QPointF(d, d));
        painter->drawLine(c + QPointF(d, -d), c + QPointF(-d, d));
        break;
    }
    case KDecoration3::DecorationButtonType::OnAllDesktops: {
        painter->setBrush(glyphColor());
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
            const qreal iconSize = side * 0.72;
            const QRectF iconRect(square.center().x() - iconSize / 2.0, square.center().y() - iconSize / 2.0, iconSize, iconSize);
            if (w && !w->isActive()) {
                painter->setOpacity(0.7);
            }
            icon.paint(painter, iconRect.toRect(), Qt::AlignCenter, QIcon::Normal);
            painter->setOpacity(1.0);
            break;
        }
        painter->setBrush(glyphColor());
        painter->setPen(Qt::NoPen);
        for (int i = -1; i <= 1; ++i) {
            painter->drawEllipse(box.center() + QPointF(0, i * box.height() * 0.34), side * 0.055, side * 0.055);
        }
        break;
    }
    case KDecoration3::DecorationButtonType::Spacer:
        break;
    default: {
        // Custom/unhandled types: draw a neutral dot so the button is still
        // discoverable and clickable.
        painter->setBrush(glyphColor());
        painter->drawEllipse(box.center(), box.width() * 0.18, box.width() * 0.18);
        break;
    }
    }

    painter->restore();
}

}
}

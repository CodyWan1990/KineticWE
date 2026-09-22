/*
    SPDX-FileCopyrightText: 2026 The KineticWE Developers

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KDecoration3/DecorationButton>

namespace KWin
{
namespace Decoration
{

class KineticDecoration;

/**
 * The button used by the Kinetic decoration.
 *
 * It draws a simple, flat glyph (minimize/maximize/close/...) using the
 * decoration's resolved palette, so the button follows the selected Kinetic
 * variant or the shared color scheme.
 */
class KineticButton : public KDecoration3::DecorationButton
{
    Q_OBJECT

public:
    explicit KineticButton(KDecoration3::DecorationButtonType type, KDecoration3::Decoration *decoration, QObject *parent = nullptr);
    ~KineticButton() override;

    void paint(QPainter *painter, const QRectF &repaintArea) override;

private:
    KineticDecoration *kinetic() const;
    QColor glyphColor() const;
    QColor hoverColor() const;
};

}
}

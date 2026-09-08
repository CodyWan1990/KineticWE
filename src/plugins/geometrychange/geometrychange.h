/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 KineticWE contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "effect/animationeffect.h"
#include "effect/effectwindow.h"

#include <chrono>
#include <QHash>
#include <QStringList>

namespace KWin
{

struct GeometryChangeWindowState
{
    qint64 createdTimeMs = 0;
    bool maximizedAboutToChange = false;
};

// Animated geometry changes for windows resized/moved by programs or scripts.
//
// Based on the scripted kwin4_effect_geometry_change by Peter Fajdiga, but
// reimplemented natively. Unlike the earlier offscreen-redirect attempt, this
// animates via AnimationEffect::animate() (Translation + Scale), the same
// well-tested path the scripted effect uses. That avoids the artifacts that
// arise from redirecting a window to an offscreen texture while it is being
// resized/moved into a tile.
class GeometryChangeEffect : public AnimationEffect
{
    Q_OBJECT

public:
    GeometryChangeEffect();

    void reconfigure(ReconfigureFlags) override;

    int requestedEffectChainPosition() const override
    {
        return 50;
    }

    static bool supported();

protected:
    // Called by AnimationEffect whenever a per-window animation ends. We use it
    // to release the WindowForceBlurRole we set while a window is transformed.
    void animationEnded(EffectWindow *w, AnimationEffect::Attribute a, uint meta) override;

private Q_SLOTS:
    void slotWindowAdded(EffectWindow *w);
    void slotWindowDeleted(EffectWindow *w);
    void slotWindowFrameGeometryChanged(EffectWindow *w, const RectF &oldGeometry);
    void slotWindowMaximizedStateAboutToChange(EffectWindow *w, bool horizontal, bool vertical);
    void slotWindowStartUserMovedResized(EffectWindow *w);
    void slotWindowFinishUserMovedResized(EffectWindow *w);

private:
    bool windowSupportsAnimation(EffectWindow *w) const;
    bool isWindowClassExcluded(const QString &windowClass) const;

    std::chrono::milliseconds m_duration;
    QStringList m_excludedWindowClasses;
    QHash<EffectWindow *, GeometryChangeWindowState> m_windowStates;
    // Live geometry-change animation count per window, used to release the
    // WindowForceBlurRole once the last one finishes (mirrors the scripted
    // effect's animationInstances counter).
    QHash<EffectWindow *, int> m_animationInstances;
    bool m_userResizing = false;
};

} // namespace KWin

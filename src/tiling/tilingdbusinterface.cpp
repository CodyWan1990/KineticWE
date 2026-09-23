/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tilingdbusinterface.h"

#include "tiles/emergelayoutengine.h"
#include "tiles/layoutengine.h"
#include "tilingcontroller.h"

namespace KWin
{

TilingDBusInterface::TilingDBusInterface(TilingController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
}

QString TilingDBusInterface::currentLayout() const
{
    return m_controller ? m_controller->currentLayoutKindString() : QString();
}

QString TilingDBusInterface::currentLayoutDisplay() const
{
    if (!m_controller) {
        return QString();
    }
    const QString kind = m_controller->currentLayoutKindString();
    if (kind.isEmpty()) {
        return QString();
    }
    return LayoutEngine::layoutDisplayName(LayoutEngine::layoutKindFromString(kind));
}

QStringList TilingDBusInterface::enabledLayouts() const
{
    if (!m_controller) {
        return {};
    }
    const QList<LayoutEngine::LayoutKind> enabled = m_controller->enabledLayoutKinds();
    QStringList kinds;
    kinds.reserve(enabled.size());
    for (const LayoutEngine::LayoutKind kind : enabled) {
        kinds.append(LayoutEngine::layoutKindToString(kind));
    }
    return kinds;
}

void TilingDBusInterface::setLayout(const QString &kind)
{
    if (!m_controller) {
        return;
    }
    const LayoutEngine::LayoutKind parsed = LayoutEngine::layoutKindFromString(kind);
    // layoutKindFromString() falls back to MasterStack for unknown names;
    // reject inputs that do not round-trip so a typo cannot silently
    // switch to the fallback layout.
    if (LayoutEngine::layoutKindToString(parsed).compare(kind, Qt::CaseInsensitive) != 0) {
        return;
    }
    m_controller->setLayout(parsed);
}

void TilingDBusInterface::cycleLayout()
{
    if (m_controller) {
        m_controller->cycleLayout();
    }
}

void TilingDBusInterface::toggleOuterGaps()
{
    if (m_controller) {
        m_controller->toggleOuterGaps();
    }
}

void TilingDBusInterface::toggleFloatOnly()
{
    if (m_controller) {
        m_controller->toggleFloatOnly();
    }
}

static QStringList columnIndices(const QList<int> &indices)
{
    QStringList out;
    out.reserve(indices.size());
    for (int index : indices) {
        out.append(QString::number(index));
    }
    return out;
}

QStringList TilingDBusInterface::emergeColumnsLeft() const
{
    if (!m_controller) {
        return {};
    }
    const EmergeLayoutEngine *engine = m_controller->activeEmergeLayoutEngine();
    if (!engine) {
        return {};
    }
    return columnIndices(engine->viewportSnapshot().columnsLeft);
}

QStringList TilingDBusInterface::emergeColumnsRight() const
{
    if (!m_controller) {
        return {};
    }
    const EmergeLayoutEngine *engine = m_controller->activeEmergeLayoutEngine();
    if (!engine) {
        return {};
    }
    return columnIndices(engine->viewportSnapshot().columnsRight);
}

void TilingDBusInterface::focusEmergeColumn(int index)
{
    if (!m_controller) {
        return;
    }
    if (EmergeLayoutEngine *engine = m_controller->activeEmergeLayoutEngine()) {
        engine->focusColumn(index);
    }
}

} // namespace KWin

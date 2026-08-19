/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2025 The KineticWE Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>
#include <QKeySequence>

class QAction;

namespace KWin
{

class NoctaliaSettings : public QObject
{
    Q_OBJECT

public:
    explicit NoctaliaSettings(QObject *parent = nullptr);
    ~NoctaliaSettings() override;

private:
    void registerShortcut(const QString &id, const QString &description, const QStringList &args, const QKeySequence &defaultShortcut = QKeySequence());
    void registerProcessAction(const QString &id, const QString &description, const QStringList &command, const QKeySequence &defaultShortcut = QKeySequence());
    QList<QAction *> m_actions;
};

} // namespace KWin

/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KQuickManagedConfigModule>

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVector>

class KConfigGroup;
class QScreen;
class TilingSettings;

namespace KWin
{

/**
 * Per-output gap override entry, exposed to QML.
 *
 * One entry is created for every currently connected output. Editing the
 * gap values writes them into a per-output kwinrc sub-group so the tiling
 * controller can read them back at runtime.
 */
class OutputGapOverride : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)
    Q_PROPERTY(int gapLeft READ gapLeft WRITE setGapLeft NOTIFY gapLeftChanged)
    Q_PROPERTY(int gapRight READ gapRight WRITE setGapRight NOTIFY gapRightChanged)
    Q_PROPERTY(int gapTop READ gapTop WRITE setGapTop NOTIFY gapTopChanged)
    Q_PROPERTY(int gapBottom READ gapBottom WRITE setGapBottom NOTIFY gapBottomChanged)
    Q_PROPERTY(int gapBetween READ gapBetween WRITE setGapBetween NOTIFY gapBetweenChanged)
    Q_PROPERTY(QString defaultLayout READ defaultLayout WRITE setDefaultLayout NOTIFY defaultLayoutChanged)

public:
    explicit OutputGapOverride(QString name, QString description, int gapLeft, int gapRight,
                               int gapTop, int gapBottom, int gapBetween, QString defaultLayout,
                               QObject *parent = nullptr);

    QString name() const { return m_name; }
    QString description() const { return m_description; }

    int gapLeft() const { return m_gapLeft; }
    int gapRight() const { return m_gapRight; }
    int gapTop() const { return m_gapTop; }
    int gapBottom() const { return m_gapBottom; }
    int gapBetween() const { return m_gapBetween; }
    QString defaultLayout() const { return m_defaultLayout; }

    void setGapLeft(int value);
    void setGapRight(int value);
    void setGapTop(int value);
    void setGapBottom(int value);
    void setGapBetween(int value);
    void setDefaultLayout(const QString &value);

    /**
     * Returns true if the given field has been explicitly changed by the user
     * since the entry was loaded (i.e., it is a deliberate override, not just
     * tracking the global default). Used by refreshFromDefaults() to preserve
     * user-set values when a global default changes.
     */
    bool isFieldDirty(const QByteArray &fieldName) const;

    /**
     * Reset the dirty-tracking state for all fields. Called after load() so
     * freshly loaded values are not mistaken for user edits.
     */
    void clearDirtyFlags();

Q_SIGNALS:
    void gapLeftChanged();
    void gapRightChanged();
    void gapTopChanged();
    void gapBottomChanged();
    void gapBetweenChanged();
    void defaultLayoutChanged();

    /**
     * Emitted whenever any of the gap values change. The owning model
     * uses this to mark itself as modified.
     */
    void modified();

private:
    void markFieldDirty(const QByteArray &fieldName);

    QString m_name;
    QString m_description;
    int m_gapLeft;
    int m_gapRight;
    int m_gapTop;
    int m_gapBottom;
    int m_gapBetween;
    QString m_defaultLayout;
    QSet<QByteArray> m_dirtyFields;
};

/**
 * List model of per-output gap overrides backed by kwinrc.
 *
 * The model is populated from the currently connected QScreen instances and
 * the per-output sub-groups under [Tiling] in kwinrc. Edits are kept in the
 * in-memory entries until TilingKCM::save() is called, at which point the
 * overrides are written back to kwinrc.
 */
class OutputGapOverridesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        GapLeftRole,
        GapRightRole,
        GapTopRole,
        GapBottomRole,
        GapBetweenRole,
        DefaultLayoutRole,
        EntryRole,
    };
    Q_ENUM(Roles)

    explicit OutputGapOverridesModel(QObject *parent = nullptr);
    ~OutputGapOverridesModel() override;

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * Re-populate the model from the current QScreen list and the
     * per-output override sub-groups in the supplied Tiling group.
     */
    void load(KConfigGroup &tilingGroup, const TilingSettings *settings);

    /**
     * Re-sync every entry's value to the current global defaults without
     * touching kwinrc on disk. Used when the user changes a global default
     * (gap, layout, available layouts) so the per-monitor page reflects
     * "matches default" for any entry that hasn't been overridden.
     */
    void refreshFromDefaults(const TilingSettings *settings);

    /**
     * Write the current override values into the per-output sub-groups
     * of the supplied Tiling group. Sub-groups whose values are all
     * equal to the defaults are removed to keep kwinrc tidy.
     */
    void save(KConfigGroup &tilingGroup, const TilingSettings *settings);

    /**
     * Reset every override entry to match the default gap values from
     * the settings object.
     */
    void defaults(const TilingSettings *settings);

    /**
     * Returns true if any entry differs from its loaded state. Used by the
     * KCM to drive the Apply button.
     */
    bool isModified() const { return m_modified; }

    /**
     * Returns true if every entry matches the default gap values. Used by
     * the KCM to drive the Defaults button.
     */
    bool isDefaults(const TilingSettings *settings) const;

Q_SIGNALS:
    void countChanged();
    void modifiedChanged();

private:
    void addEntry(const QString &name, const QString &description, int left, int right,
                  int top, int bottom, int between, const QString &defaultLayout);
    void syncFromScreens();
    OutputGapOverride *entryForName(const QString &name) const;
    void setModified(bool modified);

    QVector<OutputGapOverride *> m_entries;
    QStringList m_screenNames;
    bool m_modified = false;
};

/**
 * Per-(desktop, output) layout override entry, exposed to QML.
 *
 * One entry is created for every (virtual desktop, monitor) pair.
 * Editing the layout value writes it into a composite kwinrc sub-group
 * so the tiling controller can read it back at runtime. Per-(desktop, output)
 * overrides take precedence over per-output overrides and the global default.
 */
class DesktopOutputLayoutOverride : public QObject
{
    Q_OBJECT
    Q_PROPERTY(uint desktopNumber READ desktopNumber CONSTANT)
    Q_PROPERTY(QString desktopName READ desktopName CONSTANT)
    Q_PROPERTY(QString outputName READ outputName CONSTANT)
    Q_PROPERTY(QString outputDescription READ outputDescription CONSTANT)
    Q_PROPERTY(int outputIndex READ outputIndex CONSTANT)
    Q_PROPERTY(QString defaultLayout READ defaultLayout WRITE setDefaultLayout NOTIFY defaultLayoutChanged)

public:
    explicit DesktopOutputLayoutOverride(uint desktopNumber, QString desktopName,
                                         int outputIndex, QString outputName, QString outputDescription,
                                         QString defaultLayout,
                                         QObject *parent = nullptr);

    uint desktopNumber() const { return m_desktopNumber; }
    QString desktopName() const { return m_desktopName; }
    int outputIndex() const { return m_outputIndex; }
    QString outputName() const { return m_outputName; }
    QString outputDescription() const { return m_outputDescription; }
    QString defaultLayout() const { return m_defaultLayout; }

    void setDefaultLayout(const QString &value);

Q_SIGNALS:
    void defaultLayoutChanged();

    /**
     * Emitted when the layout value changes. The owning model
     * uses this to mark itself as modified.
     */
    void modified();

private:
    uint m_desktopNumber;
    QString m_desktopName;
    int m_outputIndex;
    QString m_outputName;
    QString m_outputDescription;
    QString m_defaultLayout;
};

/**
 * Model of per-(desktop, output) layout overrides.
 *
 * Exposes DesktopOutputLayoutOverride entries to QML. Creates one entry for
 * every (virtual desktop, connected monitor) pair. Reads/writes the
 * composite sub-group [Tiling][DesktopOutput "<num>:<output>"] in kwinrc.
 */
class DesktopOutputLayoutOverridesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QStringList outputNames READ outputNames NOTIFY outputNamesChanged)
    Q_PROPERTY(QStringList outputDescriptions READ outputDescriptions NOTIFY outputNamesChanged)

public:
    enum Roles
    {
        DesktopNumberRole = Qt::UserRole + 1,
        DesktopNameRole,
        OutputIndexRole,
        OutputNameRole,
        OutputDescriptionRole,
        DefaultLayoutRole,
        EntryRole,
    };
    Q_ENUM(Roles)

    explicit DesktopOutputLayoutOverridesModel(QObject *parent = nullptr);
    ~DesktopOutputLayoutOverridesModel() override;

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * Returns the DesktopOutputLayoutOverride entry at the given flat row index,
     * or nullptr if out of range. Exposed as Q_INVOKABLE so QML can look up
     * entries without needing access to the C++ enum role values.
     */
    Q_INVOKABLE DesktopOutputLayoutOverride *entryAt(int row) const;

    QStringList outputNames() const { return m_outputNames; }
    QStringList outputDescriptions() const { return m_outputDescriptions; }

    /**
     * Re-populate the model from the current virtual desktops and
     * connected monitors.
     */
    void load(KConfigGroup &tilingGroup, const TilingSettings *settings);

    /**
     * Re-sync every entry's value to the current global defaults.
     */
    void refreshFromDefaults(const TilingSettings *settings);

    /**
     * Write the current override values into the per-(desktop, output)
     * sub-groups of the supplied Tiling group.
     */
    void save(KConfigGroup &tilingGroup, const TilingSettings *settings);

    /**
     * Reset every override entry to match the global default layout.
     */
    void defaults(const TilingSettings *settings);

    bool isModified() const { return m_modified; }
    bool isDefaults(const TilingSettings *settings) const;

Q_SIGNALS:
    void countChanged();
    void modifiedChanged();
    void outputNamesChanged();

private:
    void rebuildModel(const TilingSettings *settings, KConfigGroup *tilingGroup);
    DesktopOutputLayoutOverride *entryAt(uint desktopNum, const QString &outputName) const;
    void setModified(bool modified);
    void syncOutputList();

    QVector<DesktopOutputLayoutOverride *> m_entries;
    QStringList m_outputNames;
    QStringList m_outputDescriptions;
    QList<uint> m_desktopNumbers;
    bool m_modified = false;
};

/**
 * Tiling settings KCM module.
 *
 * Hosts the default settings (via TilingSettings) and a model of
 * per-output gap overrides. Saving the module writes both the default
 * settings and the per-output override sub-groups to kwinrc, then
 * notifies KWin via DBus to reload the configuration.
 */
class TilingKCM : public KQuickManagedConfigModule
{
    Q_OBJECT
    Q_PROPERTY(TilingSettings *settings READ settings CONSTANT)
    Q_PROPERTY(KWin::OutputGapOverridesModel *gapOverridesModel READ gapOverridesModel CONSTANT)
    Q_PROPERTY(KWin::DesktopOutputLayoutOverridesModel *desktopLayoutOverridesModel READ desktopLayoutOverridesModel CONSTANT)

public:
    explicit TilingKCM(QObject *parent, const KPluginMetaData &metaData);
    ~TilingKCM() override;

    TilingSettings *settings() const;
    OutputGapOverridesModel *gapOverridesModel() const;
    DesktopOutputLayoutOverridesModel *desktopLayoutOverridesModel() const;

    bool isSaveNeeded() const override;
    bool isDefaults() const override;

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private:
    TilingSettings *m_settings;
    OutputGapOverridesModel *m_gapOverridesModel;
    DesktopOutputLayoutOverridesModel *m_desktopLayoutOverridesModel;
};

} // namespace KWin

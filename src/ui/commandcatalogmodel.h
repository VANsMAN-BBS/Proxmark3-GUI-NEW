#ifndef COMMANDCATALOGMODEL_H
#define COMMANDCATALOGMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Read-only command list for the RawCommand page.
 *
 * Data comes from the shipped client's `-t` metadata (see
 * scripts/generate-command-catalog.py).  Command text is never translated;
 * only the short description is looked up by stable id in the
 * "CommandCatalog" translation context.
 */
class CommandCatalogModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        CommandRole,
        DescriptionRole,
        EnglishDescriptionRole,
        CategoryRole,
        OfflineRole
    };

    explicit CommandCatalogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool loadFromResource(const QString &path = QStringLiteral(":/commands/commands.json"));
    bool loadFromJson(const QByteArray &json, QString *error = nullptr);

    bool isLoaded() const { return !m_entries.isEmpty(); }
    int totalCount() const { return m_entries.size(); }
    int visibleCount() const { return m_visible.size(); }

    QString commandAt(int row) const;
    QString idAt(int row) const;
    QString descriptionAt(int row) const;
    QString categoryAt(int row) const;

    void setSearchText(const QString &text);
    void setCategoryFilter(const QString &category);
    QString searchText() const { return m_search; }
    QString categoryFilter() const { return m_category; }

    static QStringList categoryFilters();
    static QString categoryForCommand(const QString &command);
    static bool matchesCategory(const QString &command, const QString &category);

private:
    struct Entry
    {
        QString id;
        QString command;
        QString description;
        QString localizedDescription;
        QString category;
        QString searchText;
        bool offline = false;
    };

    void rebuildVisible();
    bool entryMatches(int index) const;

    QVector<Entry> m_entries;
    QVector<int> m_visible;
    QString m_search;
    QString m_category;
};

#endif // COMMANDCATALOGMODEL_H

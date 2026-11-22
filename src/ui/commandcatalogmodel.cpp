#include "commandcatalogmodel.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

CommandCatalogModel::CommandCatalogModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_category(QStringLiteral("all"))
{
}

int CommandCatalogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_visible.size();
}

QVariant CommandCatalogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) {
        return QVariant();
    }
    const Entry &entry = m_entries.at(m_visible.at(index.row()));
    switch (role) {
    case Qt::DisplayRole:
    case CommandRole:
        return entry.command;
    case Qt::ToolTipRole:
        return entry.command + QLatin1Char('\n') + entry.localizedDescription;
    case IdRole:
        return entry.id;
    case DescriptionRole:
        return entry.localizedDescription;
    case EnglishDescriptionRole:
        return entry.description;
    case CategoryRole:
        return entry.category;
    case OfflineRole:
        return entry.offline;
    default:
        break;
    }
    return QVariant();
}

QHash<int, QByteArray> CommandCatalogModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(IdRole, "id");
    roles.insert(CommandRole, "command");
    roles.insert(DescriptionRole, "description");
    roles.insert(EnglishDescriptionRole, "englishDescription");
    roles.insert(CategoryRole, "category");
    roles.insert(OfflineRole, "offline");
    return roles;
}

bool CommandCatalogModel::loadFromResource(const QString &path)
{
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        return false;
    }
    return loadFromJson(file.readAll());
}

bool CommandCatalogModel::loadFromJson(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = parseError.errorString();
        }
        return false;
    }

    const QJsonArray commands = document.object().value(QStringLiteral("commands")).toArray();
    QVector<Entry> entries;
    entries.reserve(commands.size());
    QSet<QString> seenIds;
    QSet<QString> seenCommands;

    for (const QJsonValue &value : commands) {
        const QJsonObject object = value.toObject();
        Entry entry;
        entry.id = object.value(QStringLiteral("id")).toString().trimmed();
        entry.command = object.value(QStringLiteral("command")).toString().trimmed();
        entry.description = object.value(QStringLiteral("description")).toString().trimmed();
        entry.category = object.value(QStringLiteral("category")).toString().trimmed();
        entry.offline = object.value(QStringLiteral("offline")).toBool();

        if (entry.description.isEmpty()) {
            // A few client commands (normally group-level `help`) have no
            // description in the shipped metadata.  Keep the command visible
            // with an explicit, translatable placeholder instead of inventing
            // documentation.
            entry.description = tr("No description provided");
        }

        if (entry.id.isEmpty() || entry.command.isEmpty()) {
            if (error) {
                *error = QStringLiteral("catalog entry without id or command");
            }
            return false;
        }
        if (seenIds.contains(entry.id) || seenCommands.contains(entry.command)) {
            if (error) {
                *error = QStringLiteral("duplicate catalog id or command: %1").arg(entry.id);
            }
            return false;
        }
        seenIds.insert(entry.id);
        seenCommands.insert(entry.command);

        // The source string is the stable id; en_US supplies the English text
        // and zh_CN the localized text.  If a translation is absent we fall
        // back to the shipped client description instead of showing the id.
        const QString translated = QCoreApplication::translate(
            "CommandCatalog", entry.id.toUtf8().constData());
        entry.localizedDescription = (translated == entry.id || translated.isEmpty())
                                          ? entry.description
                                          : translated;

        entry.searchText = (entry.command + QLatin1Char('\n') + entry.id + QLatin1Char('\n')
                            + entry.description + QLatin1Char('\n') + entry.localizedDescription)
                               .toLower();
        entries.append(entry);
    }

    if (entries.isEmpty()) {
        if (error) {
            *error = QStringLiteral("catalog is empty");
        }
        return false;
    }

    beginResetModel();
    m_entries = entries;
    rebuildVisible();
    endResetModel();
    return true;
}

QString CommandCatalogModel::commandAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) {
        return QString();
    }
    return m_entries.at(m_visible.at(row)).command;
}

QString CommandCatalogModel::idAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) {
        return QString();
    }
    return m_entries.at(m_visible.at(row)).id;
}

QString CommandCatalogModel::descriptionAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) {
        return QString();
    }
    return m_entries.at(m_visible.at(row)).localizedDescription;
}

QString CommandCatalogModel::categoryAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) {
        return QString();
    }
    return m_entries.at(m_visible.at(row)).category;
}

void CommandCatalogModel::setSearchText(const QString &text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == m_search) {
        return;
    }
    m_search = normalized;
    beginResetModel();
    rebuildVisible();
    endResetModel();
}

void CommandCatalogModel::setCategoryFilter(const QString &category)
{
    const QString normalized = category.trimmed().isEmpty() ? QStringLiteral("all")
                                                             : category.trimmed();
    if (normalized == m_category) {
        return;
    }
    m_category = normalized;
    beginResetModel();
    rebuildVisible();
    endResetModel();
}

QStringList CommandCatalogModel::categoryFilters()
{
    return QStringList() << QStringLiteral("all") << QStringLiteral("hf")
                         << QStringLiteral("lf") << QStringLiteral("hf14a")
                         << QStringLiteral("hfmf") << QStringLiteral("hf15")
                         << QStringLiteral("lfem") << QStringLiteral("other");
}

QString CommandCatalogModel::categoryForCommand(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.startsWith(QStringLiteral("hf 14a "))) {
        return QStringLiteral("hf14a");
    }
    static const QRegularExpression mifare(
        QStringLiteral("^hf (?:mf|mfp|mfdes|mfu|ntag424)\\b"));
    if (mifare.match(trimmed).hasMatch()) {
        return QStringLiteral("hfmf");
    }
    if (trimmed.startsWith(QStringLiteral("hf 15 "))) {
        return QStringLiteral("hf15");
    }
    if (trimmed.startsWith(QStringLiteral("hf "))) {
        return QStringLiteral("hf");
    }
    if (trimmed.startsWith(QStringLiteral("lf em "))) {
        return QStringLiteral("lfem");
    }
    if (trimmed.startsWith(QStringLiteral("lf "))) {
        return QStringLiteral("lf");
    }
    return QStringLiteral("other");
}

bool CommandCatalogModel::matchesCategory(const QString &command, const QString &category)
{
    const QString trimmed = command.trimmed();
    if (category.isEmpty() || category == QLatin1String("all")) {
        return true;
    }
    if (category == QLatin1String("hf")) {
        return trimmed.startsWith(QStringLiteral("hf "));
    }
    if (category == QLatin1String("lf")) {
        return trimmed.startsWith(QStringLiteral("lf "));
    }
    if (category == QLatin1String("other")) {
        return !trimmed.startsWith(QStringLiteral("hf "))
               && !trimmed.startsWith(QStringLiteral("lf "));
    }
    return categoryForCommand(trimmed) == category;
}

void CommandCatalogModel::rebuildVisible()
{
    m_visible.clear();
    m_visible.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        if (entryMatches(i)) {
            m_visible.append(i);
        }
    }
}

bool CommandCatalogModel::entryMatches(int index) const
{
    const Entry &entry = m_entries.at(index);
    if (!matchesCategory(entry.command, m_category)) {
        return false;
    }
    if (m_search.isEmpty()) {
        return true;
    }
    return entry.searchText.contains(m_search);
}

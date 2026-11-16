#include "station_table.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <algorithm>

namespace {

quint32 makeKey(int area, int line, int station)
{
    if (area < 0 || line < 0 || station < 0)
        return 0;
    return (static_cast<quint32>(area & 0x03) << 16)
         | (static_cast<quint32>(line & 0xFF) << 8)
         | static_cast<quint32>(station & 0xFF);
}

/** Minimal RFC4180 parser: handles quoted fields, doubled quotes and CRLF. */
QVector<QStringList> parseCsv(const QString &text)
{
    QVector<QStringList> rows;
    QStringList row;
    QString field;
    bool inQuotes = false;

    for (int i = 0; i < text.size(); ++i)
    {
        const QChar c = text.at(i);
        if (inQuotes)
        {
            if (c == QLatin1Char('"'))
            {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"'))
                {
                    field += QLatin1Char('"');
                    ++i;
                }
                else
                {
                    inQuotes = false;
                }
            }
            else
            {
                field += c;
            }
            continue;
        }

        if (c == QLatin1Char('"'))
        {
            inQuotes = true;
        }
        else if (c == QLatin1Char(','))
        {
            row.append(field);
            field.clear();
        }
        else if (c == QLatin1Char('\n'))
        {
            row.append(field);
            field.clear();
            if (!row.isEmpty() && !(row.size() == 1 && row.first().isEmpty()))
                rows.append(row);
            row.clear();
        }
        else if (c != QLatin1Char('\r'))
        {
            field += c;
        }
    }

    if (!field.isEmpty() || !row.isEmpty())
    {
        row.append(field);
        rows.append(row);
    }
    return rows;
}

bool parseHexByte(const QString &text, int *out)
{
    bool ok = false;
    const int value = text.trimmed().toInt(&ok, 16);
    if (!ok || value < 0 || value > 0xFF)
        return false;
    *out = value;
    return true;
}

} // namespace

StationTable &StationTable::instance()
{
    static StationTable table;
    return table;
}

StationTable::StationTable() = default;

void StationTable::ensureLoaded()
{
    if (m_loaded)
        return;
    m_loaded = true;

    loadCsv(QStringLiteral(":/felica/station_codes.csv"), false);
    loadCsv(QStringLiteral(":/felica/station_codes_legacy.csv"), true);

    if (m_primary.isEmpty() && m_error.isEmpty())
        m_error = QStringLiteral("station code resources are empty");
}

void StationTable::loadCsv(const QString &path, bool legacy)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (m_error.isEmpty())
            m_error = QStringLiteral("cannot open %1").arg(path);
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    const QString text = stream.readAll();
    file.close();

    const QVector<QStringList> rows = parseCsv(text);
    for (int i = 0; i < rows.size(); ++i)
    {
        const QStringList &row = rows.at(i);
        if (row.size() < 6)
            continue;

        int area = 0;
        int line = 0;
        int station = 0;
        if (!parseHexByte(row.at(0), &area)
            || !parseHexByte(row.at(1), &line)
            || !parseHexByte(row.at(2), &station))
        {
            continue; // header or malformed row
        }

        Transit::StationCandidate candidate;
        candidate.table = legacy ? QStringLiteral("solidx-legacy")
                                 : QStringLiteral("suica-viewer");
        candidate.area = area;
        candidate.company = row.at(3).trimmed();
        candidate.line = row.at(4).trimmed();
        candidate.station = row.at(5).trimmed();
        candidate.note = row.size() > 6 ? row.at(6).trimmed() : QString();

        QHash<quint32, QVector<Transit::StationCandidate> > &target =
            legacy ? m_legacy : m_primary;
        target[makeKey(area, line, station)].append(candidate);
        ++m_count;
    }
}

Transit::StationInfo StationTable::resolve(int line, int station, int area)
{
    ensureLoaded();
    Transit::StationInfo info;
    info.lineCode = line;
    info.stationCode = station;
    info.rawCode = QStringLiteral("%1/%2")
                       .arg(line, 2, 16, QLatin1Char('0'))
                       .arg(station, 2, 16, QLatin1Char('0'))
                       .toUpper();

    const int firstArea = (area >= 0) ? area : 0;
    const int lastArea = (area >= 0) ? area : 3;

    for (int a = firstArea; a <= lastArea; ++a)
    {
        const quint32 key = makeKey(a, line, station);
        const QVector<Transit::StationCandidate> hits = m_primary.value(key);
        for (const Transit::StationCandidate &hit : hits)
            info.candidates.append(hit);
    }
    for (int a = firstArea; a <= lastArea; ++a)
    {
        const quint32 key = makeKey(a, line, station);
        const QVector<Transit::StationCandidate> hits = m_legacy.value(key);
        for (const Transit::StationCandidate &hit : hits)
            info.candidates.append(hit);
    }

    // De-duplicate identical interpretations.
    QVector<Transit::StationCandidate> unique;
    QStringList seen;
    for (const Transit::StationCandidate &candidate : info.candidates)
    {
        const QString key = candidate.table + QLatin1Char('|') + candidate.company
                          + QLatin1Char('|') + candidate.line + QLatin1Char('|')
                          + candidate.station;
        if (seen.contains(key))
            continue;
        seen.append(key);
        unique.append(candidate);
    }
    info.candidates = unique;

    QStringList names;
    for (const Transit::StationCandidate &candidate : info.candidates)
    {
        if (!candidate.station.isEmpty() && !names.contains(candidate.station))
            names.append(candidate.station);
    }

    if (!info.candidates.isEmpty())
    {
        info.resolved = true;
        info.area = area;
        info.primaryName = info.candidates.first().station;
        info.ambiguous = names.size() > 1;
    }
    return info;
}

bool StationTable::isLoaded()
{
    ensureLoaded();
    return m_loaded;
}

QString StationTable::loadError()
{
    ensureLoaded();
    return m_error;
}

int StationTable::entryCount()
{
    ensureLoaded();
    return m_count;
}

StationNameMap &StationNameMap::instance()
{
    static StationNameMap map;
    return map;
}

StationNameMap::StationNameMap() = default;

void StationNameMap::ensureLoaded()
{
    if (m_loaded)
        return;
    m_loaded = true;

    QFile file(QStringLiteral(":/felica/station_names_i18n.csv"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    const QVector<QStringList> rows = parseCsv(stream.readAll());
    for (const QStringList &row : rows)
    {
        if (row.size() < 3)
            continue;
        const QString ja = row.at(0).trimmed();
        if (ja.isEmpty() || ja == QStringLiteral("ja"))
            continue;
        m_names.insert(ja, qMakePair(row.at(1).trimmed(), row.at(2).trimmed()));
    }
}

QString StationNameMap::localized(const QString &japanese, Transit::LabelLanguage language) const
{
    const_cast<StationNameMap *>(this)->ensureLoaded();
    const auto it = m_names.constFind(japanese);
    if (it == m_names.constEnd())
        return japanese;
    if (language == Transit::LabelLanguage::Chinese && !it.value().first.isEmpty())
        return japanese + QStringLiteral("\uff08") + it.value().first + QStringLiteral("\uff09");
    if (language == Transit::LabelLanguage::English && !it.value().second.isEmpty())
        return japanese + QStringLiteral(" (") + it.value().second + QStringLiteral(")");
    return japanese;
}

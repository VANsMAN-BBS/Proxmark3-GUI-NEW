#ifndef FELICA_STATION_TABLE_H
#define FELICA_STATION_TABLE_H

#include <QHash>
#include <QString>
#include <QVector>

#include "transit_types.h"

/**
 * Station code resolver for CJRC (Suica/PASMO) 2-byte station codes.
 *
 * The primary table is the MIT-licensed community table from
 * soltia48/suica-viewer (hex codes). A small set of cross-version conflict
 * rows from SolidX/SuicaTools is kept separately so that ambiguous codes are
 * shown with all candidates instead of silently picking one.
 *
 * The tables are loaded from Qt resources on first use and never written.
 */
class StationTable
{
public:
    static StationTable &instance();

    Transit::StationInfo resolve(int line, int station, int area = -1);
    bool isLoaded();
    QString loadError();
    int entryCount();

private:
    StationTable();
    void ensureLoaded();
    void loadCsv(const QString &path, bool legacy);

    bool m_loaded = false;
    QString m_error;
    int m_count = 0;
    QHash<quint32, QVector<Transit::StationCandidate> > m_primary;
    QHash<quint32, QVector<Transit::StationCandidate> > m_legacy;
};

#endif // FELICA_STATION_TABLE_H

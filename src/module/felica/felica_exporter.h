#ifndef FELICA_EXPORTER_H
#define FELICA_EXPORTER_H

#include <QByteArray>

#include "transit_types.h"

/**
 * Stable JSON/CSV serialization for transit read results.
 *
 * JSON schema_version 1.0 is intentionally generic (card / balance /
 * transactions / gate_history / services / read_status) so future profiles can
 * reuse it. Raw blocks are only included when explicitly requested.
 */
namespace FelicaExport {

QByteArray toJson(const Transit::ReadResult &result, bool includeRawBlocks);
QByteArray historyCsv(const Transit::ReadResult &result);
QByteArray gateCsv(const Transit::ReadResult &result);

} // namespace FelicaExport

#endif // FELICA_EXPORTER_H

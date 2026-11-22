#ifndef EMV_EMVEXPORT_H
#define EMV_EMVEXPORT_H

#include <QByteArray>

#include "emvtypes.h"

namespace EmvExport {

/** UTF-8 with BOM CSV, compatible with the pm3_emv_log CLI export. */
QByteArray toCsv(const QVector<Emv::Txn> &txns);

/** JSON object with metadata and a `transactions` array. */
QByteArray toJson(const Emv::ReadResult &result, bool includeRaw = false);

} // namespace EmvExport

#endif // EMV_EMVEXPORT_H

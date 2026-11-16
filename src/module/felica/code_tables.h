#ifndef FELICA_CODE_TABLES_H
#define FELICA_CODE_TABLES_H

#include <QString>

#include "transit_types.h"

/**
 * FeliCa / CJRC transit code labels.
 *
 * Labels are stored in the form used by Japanese transit apps. Unknown codes
 * are returned as an explicit "unknown" string that contains the raw code;
 * they are never mapped to an invented name.
 */
namespace FelicaCodes {

QString equipmentLabel(int code);
QString transactionLabel(int code);
QString paymentLabel(int code);
QString gateInstructionLabel(int code);
QString gateInOutLabel(int code);
QString intermediateGateLabel(int code);
QString regionLabel(int area);

QString serviceName(int code);
QString serviceAccess(int code);
int serviceDeclaredBlocks(int code);

Transit::TransactionKind kindForTransaction(int transactionCode);

} // namespace FelicaCodes

#endif // FELICA_CODE_TABLES_H

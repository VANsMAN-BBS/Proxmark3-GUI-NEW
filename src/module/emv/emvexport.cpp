#include "emvexport.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace {

QString csvField(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"'))
        || escaped.contains(QLatin1Char('\n')) || escaped.contains(QLatin1Char('\r')))
    {
        escaped = QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return escaped;
}

QString csvLine(const QStringList &fields)
{
    QStringList escaped;
    for (const QString &field : fields)
        escaped.append(csvField(field));
    return escaped.join(QLatin1Char(',')) + QLatin1Char('\n');
}

QJsonObject txnJson(const Emv::Txn &txn, bool includeRaw)
{
    QJsonObject object;
    object.insert(QStringLiteral("date"), txn.date);
    object.insert(QStringLiteral("time"), txn.time);
    object.insert(QStringLiteral("merchant"), txn.merchant);
    object.insert(QStringLiteral("amount"), txn.amount);
    object.insert(QStringLiteral("record_no"), txn.recordNo);
    if (includeRaw)
        object.insert(QStringLiteral("raw"), txn.raw);
    return object;
}

} // namespace

namespace EmvExport {

QByteArray toCsv(const QVector<Emv::Txn> &txns)
{
    QString out;
    out += QStringLiteral("date,time,merchant,amount,record_no\n");
    for (const Emv::Txn &txn : txns)
    {
        out += csvLine(QStringList()
                       << txn.date
                       << txn.time
                       << txn.merchant
                       << txn.amount
                       << QString::number(txn.recordNo));
    }
    QByteArray data = out.toUtf8();
    data.prepend("\xEF\xBB\xBF");
    return data;
}

QByteArray toJson(const Emv::ReadResult &result, bool includeRaw)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    root.insert(QStringLiteral("generated_at"),
                QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("application"), QStringLiteral("Proxmark3GUI"));
    root.insert(QStringLiteral("experimental"), true);
    root.insert(QStringLiteral("read_only"), true);

    QJsonObject readStatus;
    readStatus.insert(QStringLiteral("stop_reason"), result.stopReason);
    readStatus.insert(QStringLiteral("card_detected"), result.cardDetected);
    readStatus.insert(QStringLiteral("returncode"), result.returnCode);
    readStatus.insert(QStringLiteral("record_count"), result.txns.size());
    QJsonArray warnings;
    for (const QString &warning : result.warnings)
        warnings.append(warning);
    readStatus.insert(QStringLiteral("warnings"), warnings);
    root.insert(QStringLiteral("read_status"), readStatus);

    QJsonArray transactions;
    for (const Emv::Txn &txn : result.txns)
        transactions.append(txnJson(txn, includeRaw));
    root.insert(QStringLiteral("transactions"), transactions);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace EmvExport

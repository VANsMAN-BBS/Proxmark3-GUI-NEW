#ifndef OCTOPUS_PARSER_H
#define OCTOPUS_PARSER_H

#include <QByteArray>
#include <QString>

struct OctopusBalance
{
    bool valid = false;
    quint32 raw = 0;
    qint64 availableDeci = 0; // HKD 0.1 units
    qint64 depositDeci = 0;
    qint64 totalDeci = 0;
    QString error;
};

/** Octopus (system 0x8008, service 0x0117, block 0) read-only balance. */
class OctopusBalanceParser
{
public:
    static const quint16 kSystemCode = 0x8008;
    static const quint16 kBalanceService = 0x0117;
    static const int kDepositOffsetDeci = 500; // new-generation HKD 50 deposit

    static OctopusBalance parse(const QByteArray &block0);
};

#endif // OCTOPUS_PARSER_H

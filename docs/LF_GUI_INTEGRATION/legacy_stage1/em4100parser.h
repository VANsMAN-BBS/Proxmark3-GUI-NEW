#ifndef EM4100PARSER_H
#define EM4100PARSER_H

#include "lfparser.h"

/**
 * EM4100 / EM410x parser.
 *
 * Client evidence (Iceman v4.23346 family):
 *   EM 410x ID 0F00112233
 *   EM410x ( RF/64 )
 *   Unique TAG ID      : 0F00112233
 *   DEZ 10         : 4276803383
 *   Pattern CDVI       : FC: 15 CN: 305
 *
 * Decimal values are only taken from the client's de-scramble section. They
 * are never recomputed locally.
 */
class EM4100Parser : public ILfParser
{
public:
    Lf::Protocol protocol() const override;
    QString name() const override;
    int detect(const QString &normalizedText, QStringList *evidence) const override;
    LFParseResult parse(const QString &normalizedText,
                        const QString &originalText) const override;
};

#endif // EM4100PARSER_H

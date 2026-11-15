#ifndef INDALAPARSER_H
#define INDALAPARSER_H

#include "lf/lfparser.h"

/**
 * Indala parser.
 *
 * Client evidence:
 *   Indala (len 64)  Raw: a0000000a0002021
 *   Fmt 26 FC: 123 Card: 1337 Parity: 11
 *   Internal ID... 123456
 *   Heden-2L...... 888
 *   Keyscan 40010. FC: 12 CN: 1337
 *
 * Raw is optional: a card with only decoded fields still yields a result.
 */
class IndalaParser : public ILfParser
{
public:
    Lf::Protocol protocol() const override;
    QString name() const override;
    int detect(const QString &normalizedText, QStringList *evidence) const override;
    LFParseResult parse(const QString &normalizedText,
                        const QString &originalText) const override;
};

#endif // INDALAPARSER_H

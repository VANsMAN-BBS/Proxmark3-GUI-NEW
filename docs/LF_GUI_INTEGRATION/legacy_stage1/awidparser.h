#ifndef AWIDPARSER_H
#define AWIDPARSER_H

#include "lfparser.h"

/**
 * AWID (26/34/36/37/40/50 bit) parser.
 *
 * Client evidence:
 *   AWID - len: 26 FC: 50 Card: 1234 - Wiegand: 26409a4, Raw: 011db2881474411111111111
 *   AWID - len: 37 FC: 1 Card: 2 - Wiegand: ..., Raw: ...
 *   AWID - len: 33 -unknown- (1234) - Wiegand: ..., Raw: ...
 */
class AWIDParser : public ILfParser
{
public:
    Lf::Protocol protocol() const override;
    QString name() const override;
    int detect(const QString &normalizedText, QStringList *evidence) const override;
    LFParseResult parse(const QString &normalizedText,
                        const QString &originalText) const override;
};

#endif // AWIDPARSER_H

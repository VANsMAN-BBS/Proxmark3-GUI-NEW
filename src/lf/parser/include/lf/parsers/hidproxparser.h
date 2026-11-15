#ifndef HIDPROXPARSER_H
#define HIDPROXPARSER_H

#include "lf/lfparser.h"

/**
 * HID Prox parser.
 *
 * The client prints one line per matching Wiegand format:
 *
 *   [H10301  ] HID H10301 26-bit                FC: 65  CN: 29334  parity ( ok )
 *   raw: 200078be5e1e
 *   found 2 matching 26-bit formats
 *
 * Because several formats can decode the same frame, the parser keeps every
 * candidate and only reports facility code / card number when the candidates
 * agree. It never falls back to H10301 silently.
 */
class HIDProxParser : public ILfParser
{
public:
    Lf::Protocol protocol() const override;
    QString name() const override;
    int detect(const QString &normalizedText, QStringList *evidence) const override;
    LFParseResult parse(const QString &normalizedText,
                        const QString &originalText) const override;
};

#endif // HIDPROXPARSER_H

#ifndef LF_PARSERS_GPROXIIPARSER_H
#define LF_PARSERS_GPROXIIPARSER_H

#include "lf/lfparser.h"

class GProxIIParser : public ILfParser
{
public:
    Lf::Protocol protocol() const override;
    QString name() const override;
    int detect(const QString &normalizedText, QStringList *evidence) const override;
    LFParseResult parse(const QString &normalizedText, const QString &originalText) const override;
};

#endif // LF_PARSERS_GPROXIIPARSER_H
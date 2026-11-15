#ifndef LF_PARSERS_FDXBPARSER_H
#define LF_PARSERS_FDXBPARSER_H

#include "lf/lfparser.h"

class FDXBParser : public ILfParser
{
public:
    Lf::Protocol protocol() const override;
    QString name() const override;
    int detect(const QString &normalizedText, QStringList *evidence) const override;
    LFParseResult parse(const QString &normalizedText, const QString &originalText) const override;
};

#endif // LF_PARSERS_FDXBPARSER_H
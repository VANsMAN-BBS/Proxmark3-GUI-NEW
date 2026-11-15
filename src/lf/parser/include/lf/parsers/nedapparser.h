#ifndef LF_PARSERS_NEDAPPARSER_H
#define LF_PARSERS_NEDAPPARSER_H

#include "lf/lfparser.h"

class NedapParser : public ILfParser
{
public:
    Lf::Protocol protocol() const override;
    QString name() const override;
    int detect(const QString &normalizedText, QStringList *evidence) const override;
    LFParseResult parse(const QString &normalizedText, const QString &originalText) const override;
};

#endif // LF_PARSERS_NEDAPPARSER_H
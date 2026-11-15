#ifndef LF_LFPARSERREGISTRY_H
#define LF_LFPARSERREGISTRY_H

#include <QList>
#include <QString>

#include "lfparser.h"

/**
 * Owns the protocol parsers and performs the dispatch.
 *
 * Dispatch is score based and commits to a single parser: it never falls back
 * to another protocol after a parse failure, which prevents mis-identification.
 */
class LFParserRegistry
{
public:
    static LFParserRegistry &instance();

    LFParseResult parse(const QString &text) const;

    /** Parses with one specific parser, bypassing dispatch (diagnostics/tests). */
    LFParseResult parseAs(Lf::Protocol protocol, const QString &text) const;

    QList<Lf::Protocol> protocols() const;
    ILfParser *parser(Lf::Protocol protocol) const;

    /** Takes ownership of the parser. */
    void registerParser(ILfParser *parser);

    ~LFParserRegistry();

private:
    LFParserRegistry();
    LFParserRegistry(const LFParserRegistry &);
    LFParserRegistry &operator=(const LFParserRegistry &);

    QList<ILfParser *> m_parsers;
};

#endif // LF_LFPARSERREGISTRY_H

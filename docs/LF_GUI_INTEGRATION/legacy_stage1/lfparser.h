#ifndef LFPARSER_H
#define LFPARSER_H

#include <QList>
#include <QString>
#include <QStringList>

#include "lfparsetypes.h"

/**
 * A single protocol parser.
 *
 * Implementations must not execute client commands, touch hardware or depend
 * on widgets. They only inspect text and build an LFParseResult.
 */
class ILfParser
{
public:
    virtual ~ILfParser() {}

    virtual Lf::Protocol protocol() const = 0;
    virtual QString name() const = 0;

    /**
     * Returns 0 when the text carries no evidence for this protocol.
     * Higher values mean stronger, more structural evidence:
     *   100  protocol specific structural signature
     *    70  strong labelled evidence
     *    40  keyword only
     * `evidence` receives human readable reasons for diagnostics.
     */
    virtual int detect(const QString &normalizedText, QStringList *evidence) const = 0;

    virtual LFParseResult parse(const QString &normalizedText,
                                const QString &originalText) const = 0;
};

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

#endif // LFPARSER_H

#ifndef LF_LFPARSER_H
#define LF_LFPARSER_H

#include <QString>
#include <QStringList>

#include "lfparsetypes.h"

/**
 * A single protocol parser.
 *
 * Implementations must not execute client commands, touch hardware or depend on
 * widgets. They only inspect client text and build an LFParseResult.
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

// Backwards compatibility with the Stage 1 layout where the registry lived in
// this header. New code may include <lf/lfparserregistry.h> directly.
#include "lfparserregistry.h"

#endif // LF_LFPARSER_H

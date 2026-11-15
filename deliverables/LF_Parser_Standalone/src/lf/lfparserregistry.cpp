#include "lf/lfparserregistry.h"

#include <QStringList>

#include <algorithm>

#include "lf/lftextnormalizer.h"

#include "lf/parsers/awidparser.h"
#include "lf/parsers/em4100parser.h"
#include "lf/parsers/fdxbparser.h"
#include "lf/parsers/gallagherparser.h"
#include "lf/parsers/gproxiiparser.h"
#include "lf/parsers/hidproxparser.h"
#include "lf/parsers/idteckparser.h"
#include "lf/parsers/indalaparser.h"
#include "lf/parsers/ioproxparser.h"
#include "lf/parsers/jablotronparser.h"
#include "lf/parsers/keriparser.h"
#include "lf/parsers/motorolaparser.h"
#include "lf/parsers/nedapparser.h"
#include "lf/parsers/nexwatchparser.h"
#include "lf/parsers/noralsyparser.h"
#include "lf/parsers/pacparser.h"
#include "lf/parsers/paradoxparser.h"
#include "lf/parsers/prescoparser.h"
#include "lf/parsers/pyramidparser.h"
#include "lf/parsers/securakeyparser.h"
#include "lf/parsers/vikingparser.h"
#include "lf/parsers/visa2000parser.h"

namespace {

int protocolPriority(Lf::Protocol protocol)
{
    // Deterministic tie-break order. Protocols with highly specific structural
    // signatures win over generic ones when scores are equal. EM4100/HID/AWID/
    // Indala keep the Stage 1 relative order.
    switch (protocol) {
    case Lf::Protocol::IDTECK:
    case Lf::Protocol::Keri:
    case Lf::Protocol::NexWatch:
    case Lf::Protocol::Motorola:
    case Lf::Protocol::GProxII:
    case Lf::Protocol::Pyramid:
    case Lf::Protocol::Paradox:
    case Lf::Protocol::IOProx:
    case Lf::Protocol::FDXB:
    case Lf::Protocol::Presco:
    case Lf::Protocol::Nedap:
    case Lf::Protocol::Gallagher:
    case Lf::Protocol::Noralsy:
    case Lf::Protocol::SecuraKey:
    case Lf::Protocol::Jablotron:
    case Lf::Protocol::Visa2000:
    case Lf::Protocol::Viking:
    case Lf::Protocol::PAC:
        return 0;
    case Lf::Protocol::EM4100:
        return 1;
    case Lf::Protocol::HIDProx:
        return 2;
    case Lf::Protocol::AWID:
        return 3;
    case Lf::Protocol::Indala:
        return 4;
    case Lf::Protocol::Unknown:
        break;
    }
    return 99;
}

struct Candidate
{
    ILfParser *parser = nullptr;
    int score = 0;
    QStringList evidence;
};

bool candidateBetter(const Candidate &left, const Candidate &right)
{
    if (left.score != right.score)
        return left.score > right.score;
    return protocolPriority(left.parser->protocol())
            < protocolPriority(right.parser->protocol());
}

} // namespace

LFParserRegistry &LFParserRegistry::instance()
{
    static LFParserRegistry registry;
    return registry;
}

LFParserRegistry::LFParserRegistry()
{
    // Specific protocols first: this is also the tie-break intent.
    registerParser(new IDTECKParser());
    registerParser(new KeriParser());
    registerParser(new NexWatchParser());
    registerParser(new MotorolaParser());
    registerParser(new GProxIIParser());
    registerParser(new PyramidParser());
    registerParser(new ParadoxParser());
    registerParser(new IOProxParser());
    registerParser(new FDXBParser());
    registerParser(new PrescoParser());
    registerParser(new NedapParser());
    registerParser(new GallagherParser());
    registerParser(new NoralsyParser());
    registerParser(new SecuraKeyParser());
    registerParser(new JablotronParser());
    registerParser(new Visa2000Parser());
    registerParser(new VikingParser());
    registerParser(new PACParser());

    // Stage 1 protocols.
    registerParser(new EM4100Parser());
    registerParser(new HIDProxParser());
    registerParser(new AWIDParser());
    registerParser(new IndalaParser());
}

LFParserRegistry::~LFParserRegistry()
{
    qDeleteAll(m_parsers);
    m_parsers.clear();
}

void LFParserRegistry::registerParser(ILfParser *parser)
{
    if (parser)
        m_parsers.append(parser);
}

QList<Lf::Protocol> LFParserRegistry::protocols() const
{
    QList<Lf::Protocol> result;
    result.reserve(m_parsers.size());
    for (const ILfParser *parser : m_parsers)
        result.append(parser->protocol());
    return result;
}

ILfParser *LFParserRegistry::parser(Lf::Protocol protocol) const
{
    for (ILfParser *candidate : m_parsers) {
        if (candidate->protocol() == protocol)
            return candidate;
    }
    return nullptr;
}

LFParseResult LFParserRegistry::parseAs(Lf::Protocol protocol, const QString &text) const
{
    ILfParser *selected = parser(protocol);
    LFParseResult result;
    result.sourceText = text;

    if (!selected) {
        result.status = Lf::Status::Unidentified;
        result.addWarning(QStringLiteral("No parser registered for this protocol."));
        return result;
    }

    const QString normalized = LFTextNormalizer::normalize(text);
    if (normalized.trimmed().isEmpty()) {
        result.protocol = protocol;
        result.displayName = Lf::protocolName(protocol);
        result.status = Lf::Status::InvalidInput;
        result.addWarning(QStringLiteral("Input is empty."));
        return result;
    }

    return selected->parse(normalized, text);
}

LFParseResult LFParserRegistry::parse(const QString &text) const
{
    LFParseResult result;
    result.sourceText = text;

    const QString normalized = LFTextNormalizer::normalize(text);
    if (normalized.trimmed().isEmpty()) {
        result.status = Lf::Status::InvalidInput;
        result.addWarning(QStringLiteral("Input is empty."));
        return result;
    }

    QList<Candidate> candidates;
    for (ILfParser *candidate : m_parsers) {
        QStringList evidence;
        const int score = candidate->detect(normalized, &evidence);
        if (score > 0) {
            Candidate entry;
            entry.parser = candidate;
            entry.score = score;
            entry.evidence = evidence;
            candidates.append(entry);
        }
    }

    if (candidates.isEmpty()) {
        result.status = Lf::Status::Unidentified;
        if (!LFTextNormalizer::looksLikeLfOutput(normalized))
            result.addWarning(QStringLiteral("Input does not look like Proxmark3 LF output."));
        else
            result.addWarning(QStringLiteral("No supported LF protocol signature was found."));
        return result;
    }

    std::stable_sort(candidates.begin(), candidates.end(), candidateBetter);

    const Candidate &best = candidates.first();
    result = best.parser->parse(normalized, text);
    result.matchScore = best.score;

    if (result.protocol == Lf::Protocol::Unknown)
        result.protocol = best.parser->protocol();
    if (result.displayName.isEmpty())
        result.displayName = Lf::protocolName(result.protocol);

    // A parser is only selected when detect() found evidence, therefore an
    // "unidentified" verdict from it means "recognised but incomplete".
    if (result.status == Lf::Status::Unidentified) {
        result.status = Lf::Status::Partial;
        result.addWarning(QStringLiteral("Protocol signature detected but no identity field could be extracted."));
    }

    if (candidates.size() > 1 && candidates.at(1).score == best.score
            && candidates.at(1).parser->protocol() != best.parser->protocol()) {
        result.addWarning(QStringLiteral("Multiple protocols matched with equal confidence; selected %1.")
                          .arg(Lf::protocolName(best.parser->protocol())));
    }

    return result;
}

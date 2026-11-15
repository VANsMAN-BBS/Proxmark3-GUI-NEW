#include "lf/parsers/prescoparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol PrescoParser::protocol() const { return Lf::Protocol::Presco; }
QString PrescoParser::name() const { return QStringLiteral("Presco"); }

int PrescoParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression site(
        QStringLiteral("(?i)Presco\\s+Site\\s+code"));
    static const QRegularExpression fullCode(
        QStringLiteral("(?i)Presco\\b[^\\n]*\\bFull\\s+code"));

    if (site.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Presco site/user line"));
        return 100;
    }
    if (fullCode.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Presco full code line"));
        return 90;
    }
    if (normalizedText.contains(QStringLiteral("Presco"), Qt::CaseInsensitive)) {
        if (evidence) evidence->append(QStringLiteral("Presco keyword"));
        return 50;
    }
    return 0;
}

LFParseResult PrescoParser::parse(const QString &normalizedText,
                                  const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("Presco\\s+Site\\s+code:\\s*(\\d+)\\s+User\\s+code:\\s*(\\d+)\\s+Full\\s+code:\\s*([0-9A-Fa-f]{4,12})"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{16,40})"));
    static const QRegularExpression fullOnly(
        QStringLiteral("Presco[^\\n]*?Full\\s+code:\\s*([0-9A-Fa-f]{4,12})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Presco");
    result.sourceText = originalText;
    result.format = QStringLiteral("Presco");

    const QRegularExpressionMatch match = main.match(normalizedText);
    if (match.hasMatch()) {
        quint32 site = 0;
        if (LfParse::toU32(match.captured(1), &site))
            result.facilityCode.set(site);
        quint64 user = 0;
        if (LfParse::toU64(match.captured(2), &user))
            result.cardNumber.set(user);
        result.addExtra(QStringLiteral("fullCode"), match.captured(3).toUpper());
    } else {
        const QString full = LfParse::capture(normalizedText, fullOnly);
        if (!full.isEmpty()) {
            result.addExtra(QStringLiteral("fullCode"), full.toUpper());
            result.addWarning(QStringLiteral("Presco site/user codes were absent; only the full code is available."));
        }
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 16, 40);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()) {
        result.status = Lf::Status::Identified;
        result.confidence = (result.facilityCode.isPresent() && result.cardNumber.isPresent())
                ? Lf::Confidence::High : Lf::Confidence::Medium;
    } else if (!result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Presco marker found but no code was extracted."));
    }
    return result;
}

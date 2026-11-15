#include "lf/parsers/noralsyparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol NoralsyParser::protocol() const { return Lf::Protocol::Noralsy; }
QString NoralsyParser::name() const { return QStringLiteral("Noralsy"); }

int NoralsyParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression card(
        QStringLiteral("(?i)Noralsy\\s*-\\s*Card:"));

    if (card.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("Noralsy card line"));
        return 100;
    }
    if (normalizedText.contains(QStringLiteral("Noralsy"), Qt::CaseInsensitive)) {
        if (evidence) evidence->append(QStringLiteral("Noralsy keyword"));
        return 50;
    }
    return 0;
}

LFParseResult NoralsyParser::parse(const QString &normalizedText,
                                   const QString &originalText) const
{
    static const QRegularExpression main(
        QStringLiteral("Noralsy\\s*-\\s*Card:\\s*(\\d+)(?:\\s*,\\s*Year:\\s*(\\d+))?"));
    static const QRegularExpression raw(
        QStringLiteral("Raw:\\s*([0-9A-Fa-f]{16,24})"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("Noralsy");
    result.sourceText = originalText;
    result.format = QStringLiteral("Noralsy");

    const QRegularExpressionMatch match = main.match(normalizedText);
    if (match.hasMatch()) {
        quint64 id = 0;
        if (LfParse::toU64(match.captured(1), &id)) {
            result.cardNumber.set(id);
            result.decimalId.set(id);
        }
        if (!match.captured(2).isEmpty())
            result.addExtra(QStringLiteral("year"), match.captured(2));
    }

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 16, 24);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (result.cardNumber.isPresent()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.rawData.isEmpty() ? Lf::Confidence::Medium
                                                     : Lf::Confidence::High;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("Noralsy marker found but no card number was extracted."));
    }
    return result;
}

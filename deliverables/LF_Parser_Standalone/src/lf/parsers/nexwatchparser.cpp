#include "lf/parsers/nexwatchparser.h"

#include <QRegularExpression>

#include "parserhelpers.h"

Lf::Protocol NexWatchParser::protocol() const { return Lf::Protocol::NexWatch; }
QString NexWatchParser::name() const { return QStringLiteral("NexWatch"); }

int NexWatchParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression rawId(
        QStringLiteral("(?i)NexWatch\\s+raw\\s+id"));
    static const QRegularExpression id88(
        QStringLiteral("(?i)88bit\\s+id\\s*:"));
    static const QRegularExpression withRaw(
        QStringLiteral("(?i)(?:NexWatch|NXT)\\b[^\\n]*\\bRaw\\s*:"));
    static const QRegularExpression keyword(
        QStringLiteral("(?i)\\b(?:NexWatch|NXT)\\b"));

    if (rawId.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("NexWatch raw id line"));
        return 100;
    }
    if (id88.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("NexWatch 88bit id line"));
        return 95;
    }
    if (withRaw.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("NexWatch raw line"));
        return 80;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence) evidence->append(QStringLiteral("NexWatch keyword"));
        return 50;
    }
    return 0;
}

LFParseResult NexWatchParser::parse(const QString &normalizedText,
                                    const QString &originalText) const
{
    static const QRegularExpression rawId(
        QStringLiteral("NexWatch\\s+raw\\s+id\\s*:\\s*0x([0-9A-Fa-f]{4,8})"));
    static const QRegularExpression fingerprint(
        QStringLiteral("fingerprint\\s*:\\s*(\\S+)"));
    static const QRegularExpression id88(
        QStringLiteral("88bit\\s+id\\s*:\\s*(\\d+)\\s*\\(\\s*0x([0-9A-Fa-f]+)\\s*\\)"));
    static const QRegularExpression mode(
        QStringLiteral("mode\\s*:\\s*([0-9A-Fa-f]+)"));
    static const QRegularExpression raw(
        QStringLiteral("Raw\\s*:\\s*([0-9A-Fa-f]{16,32})"));
    static const QRegularExpression legacyId(
        QStringLiteral("NexWatch\\s+ID\\s*:\\s*(\\d+)"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("NexWatch");
    result.sourceText = originalText;
    result.format = QStringLiteral("NexWatch");

    const QString rawIdValue = LfParse::capture(normalizedText, rawId);
    if (!rawIdValue.isEmpty())
        result.addExtra(QStringLiteral("rawId"), rawIdValue.toUpper());

    const QString fp = LfParse::capture(normalizedText, fingerprint);
    if (!fp.isEmpty())
        result.addExtra(QStringLiteral("fingerprint"), fp);

    const QRegularExpressionMatch idMatch = id88.match(normalizedText);
    if (idMatch.hasMatch()) {
        quint64 id = 0;
        if (LfParse::toU64(idMatch.captured(1), &id)) {
            result.cardNumber.set(id);
            result.decimalId.set(id);
        }
        result.addExtra(QStringLiteral("id88Hex"), idMatch.captured(2).toUpper());
    } else {
        const QString legacy = LfParse::capture(normalizedText, legacyId);
        quint64 id = 0;
        if (!legacy.isEmpty() && LfParse::toU64(legacy, &id)) {
            result.cardNumber.set(id);
            result.decimalId.set(id);
        }
    }

    const QString modeValue = LfParse::capture(normalizedText, mode);
    if (!modeValue.isEmpty())
        result.addExtra(QStringLiteral("mode"), modeValue.toUpper());

    result.rawData = LfParse::hex(LfParse::capture(normalizedText, raw), 16, 32);
    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (result.cardNumber.isPresent() || !result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.cardNumber.isPresent() ? Lf::Confidence::High
                                                          : Lf::Confidence::Medium;
    } else if (!rawIdValue.isEmpty()) {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("NexWatch raw id present but the 88bit id and raw payload are missing."));
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("NexWatch marker found but no identity was extracted."));
    }
    return result;
}

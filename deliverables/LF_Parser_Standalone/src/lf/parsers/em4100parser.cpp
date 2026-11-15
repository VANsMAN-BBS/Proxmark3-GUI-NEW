#include "lf/parsers/em4100parser.h"

#include <QRegularExpression>
#include <QStringList>

#include "lf/lftextnormalizer.h"

Lf::Protocol EM4100Parser::protocol() const
{
    return Lf::Protocol::EM4100;
}

QString EM4100Parser::name() const
{
    return QStringLiteral("EM4100");
}

int EM4100Parser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression fullId(
        QStringLiteral("(?i)\\bEM\\s*410x\\s+(?:XL\\s+)?ID\\s+[0-9A-Fa-f]{6,}"));
    static const QRegularExpression xlId(
        QStringLiteral("(?i)\\bEM\\s*410x\\s+XL\\s+ID\\b"));
    static const QRegularExpression compact(
        QStringLiteral("(?i)\\bEM410x\\b"));
    static const QRegularExpression spaced(
        QStringLiteral("(?i)\\bEM\\s*410x\\b"));

    if (fullId.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("EM 410x ID payload line"));
        return 100;
    }
    if (xlId.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("EM 410x XL ID header"));
        return 90;
    }
    if (compact.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("EM410x marker"));
        return 60;
    }
    if (spaced.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("EM 410x marker"));
        return 50;
    }
    return 0;
}

LFParseResult EM4100Parser::parse(const QString &normalizedText,
                                  const QString &originalText) const
{
    static const QRegularExpression standardId(
        QStringLiteral("\\bEM\\s*410x\\s+ID\\s+([0-9A-Fa-f]{10})\\b"));
    static const QRegularExpression xlId(
        QStringLiteral("\\bEM\\s*410x\\s+XL\\s+ID\\s+([0-9A-Fa-f]{6})([0-9A-Fa-f]{16})\\b"));
    static const QRegularExpression uniqueTag(
        QStringLiteral("Unique TAG ID\\s*:\\s*([0-9A-Fa-f]{10})"));
    static const QRegularExpression rfDivisor(
        QStringLiteral("EM410x\\s*\\(\\s*RF/(\\d+)\\s*\\)"));
    static const QRegularExpression dez10(
        QStringLiteral("DEZ 10\\s*:\\s*(\\d+)"));
    static const QRegularExpression dez8(
        QStringLiteral("DEZ 8\\s*:\\s*(\\d+)"));
    static const QRegularExpression dez14(
        QStringLiteral("DEZ 14/IK2\\s*:\\s*(\\d+)"));
    static const QRegularExpression dez15(
        QStringLiteral("DEZ 15/IK3\\s*:\\s*(\\d+)"));
    static const QRegularExpression electra(
        QStringLiteral("EM\\s*410x\\s+ID\\s+[0-9A-Fa-f]{10}\\s+Electra\\s+(\\d{3})"));
    static const QRegularExpression frame128(
        QStringLiteral("EM\\s*410x\\s+ID\\s+[0-9A-Fa-f]{10}\\s+on\\s+128b\\s+frame\\s+with\\s+data\\s+([0-9A-Fa-f]{11})"));
    static const QRegularExpression shortFrame(
        QStringLiteral("Short ID found on a 128b frame"));
    static const QRegularExpression cdvi(
        QStringLiteral("Pattern CDVI\\s*:\\s*FC:\\s*(\\d+)\\s+CN:\\s*(\\d+)"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("EM4100");
    result.sourceText = originalText;

    QRegularExpressionMatch match = standardId.match(normalizedText);
    bool isXl = false;
    if (match.hasMatch()) {
        result.hexId = LFTextNormalizer::normalizeHex(match.captured(1));
    } else {
        match = xlId.match(normalizedText);
        if (match.hasMatch()) {
            isXl = true;
            const QString high = LFTextNormalizer::normalizeHex(match.captured(1));
            const QString low = LFTextNormalizer::normalizeHex(match.captured(2));
            result.hexId = high + low;
            result.addExtra(QStringLiteral("xlHigh"), high);
            result.addExtra(QStringLiteral("xlLow"), low);
        }
    }

    if (!result.hexId.isEmpty()) {
        result.rawData = result.hexId;
        match = uniqueTag.match(normalizedText);
        if (match.hasMatch())
            result.addExtra(QStringLiteral("uniqueTagId"),
                            LFTextNormalizer::normalizeHex(match.captured(1)));
        else
            result.addExtra(QStringLiteral("uniqueTagId"), result.hexId);
    } else {
        // No primary ID line: accept the de-scramble identity as a weaker source.
        match = uniqueTag.match(normalizedText);
        if (match.hasMatch()) {
            result.hexId = LFTextNormalizer::normalizeHex(match.captured(1));
            result.rawData = result.hexId;
            result.addWarning(QStringLiteral("EM4100 ID taken from the de-scramble section; primary ID line was absent."));
        }
    }

    match = rfDivisor.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("rfDivisor"), match.captured(1));

    match = dez10.match(normalizedText);
    if (match.hasMatch()) {
        bool ok = false;
        const qulonglong value = match.captured(1).toULongLong(&ok);
        if (ok)
            result.decimalId.set(static_cast<quint64>(value));
    } else {
        match = dez14.match(normalizedText);
        if (match.hasMatch()) {
            bool ok = false;
            const qulonglong value = match.captured(1).toULongLong(&ok);
            if (ok)
                result.decimalId.set(static_cast<quint64>(value));
        }
    }

    match = dez8.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("dez8"), match.captured(1));
    match = dez14.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("dez14"), match.captured(1));
    match = dez15.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("dez15"), match.captured(1));

    match = electra.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("electra"), match.captured(1));

    match = frame128.match(normalizedText);
    if (match.hasMatch()) {
        result.addExtra(QStringLiteral("frame"), QStringLiteral("128"));
        result.addExtra(QStringLiteral("frameData"),
                        LFTextNormalizer::normalizeHex(match.captured(1)));
    }

    if (shortFrame.match(normalizedText).hasMatch())
        result.addWarning(QStringLiteral("Client reported a short ID on a 128b frame."));

    // CDVI / Paxton style values are client de-scramble patterns. They are kept
    // as diagnostics but are not promoted to facility code / card number.
    match = cdvi.match(normalizedText);
    if (match.hasMatch()) {
        result.addExtra(QStringLiteral("patternCdviFc"), match.captured(1));
        result.addExtra(QStringLiteral("patternCdviCn"), match.captured(2));
        result.addWarning(QStringLiteral("FC/CN below are client de-scramble patterns, not EM4100 fields."));
    }

    if (isXl)
        result.format = QStringLiteral("EM4100 XL");
    else if (frame128.match(normalizedText).hasMatch())
        result.format = QStringLiteral("EM4100 (128b frame)");
    else
        result.format = QStringLiteral("EM4100");

    if (!result.hexId.isEmpty()) {
        result.status = Lf::Status::Identified;
        result.confidence = result.decimalId.isPresent() ? Lf::Confidence::High
                                                         : Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("EM4100 marker found but no ID payload was extracted."));
    }

    return result;
}

#include "awidparser.h"

#include <QRegularExpression>
#include <QStringList>

#include "lftextnormalizer.h"

Lf::Protocol AWIDParser::protocol() const
{
    return Lf::Protocol::AWID;
}

QString AWIDParser::name() const
{
    return QStringLiteral("AWID");
}

int AWIDParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression lenLine(
        QStringLiteral("(?i)\\bAWID\\s*-\\s*len:\\s*\\d+"));
    static const QRegularExpression withRaw(
        QStringLiteral("(?i)\\bAWID\\b[^\\n]*\\bRaw:"));
    static const QRegularExpression marker(
        QStringLiteral("(?i)\\bAWID\\s+ID\\s+found"));
    static const QRegularExpression keyword(
        QStringLiteral("(?i)\\bAWID\\b"));

    if (lenLine.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("AWID length line"));
        return 100;
    }
    if (withRaw.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("AWID line with raw payload"));
        return 80;
    }
    if (marker.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("AWID found marker"));
        return 60;
    }
    if (keyword.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("AWID keyword"));
        return 40;
    }
    return 0;
}

LFParseResult AWIDParser::parse(const QString &normalizedText,
                                const QString &originalText) const
{
    static const QRegularExpression knownLine(
        QStringLiteral("AWID\\s*-\\s*len:\\s*(\\d+)\\s+FC:\\s*(\\d+)\\s+Card:\\s*(\\d+)[^\\n]*?Raw:\\s*([0-9A-Fa-f]{6,24})"));
    static const QRegularExpression knownNoRaw(
        QStringLiteral("AWID\\s*-\\s*len:\\s*(\\d+)\\s+FC:\\s*(\\d+)\\s+Card:\\s*(\\d+)"));
    static const QRegularExpression unknownLine(
        QStringLiteral("AWID\\s*-\\s*len:\\s*(\\d+)\\s+-unknown-\\s*\\((\\d+)\\)[^\\n]*?Raw:\\s*([0-9A-Fa-f]{6,24})"));
    static const QRegularExpression rawOnly(
        QStringLiteral("AWID[^\\n]*?Raw:\\s*([0-9A-Fa-f]{6,24})"));
    static const QRegularExpression cansec(
        QStringLiteral("Farpointe/Cansec\\s+37-bit\\s*-\\s*FC:\\s*(\\d+)\\s+CN:\\s*(\\d+)"));
    static const QRegularExpression hcc40(
        QStringLiteral("Farpointe\\s+HCC40\\s+-\\s*ID\\s+without\\s+parity:\\s*([0-9A-Fa-f]+)"));
    static const QRegularExpression rbh(
        QStringLiteral("RBH\\s*-\\s*CN:\\s*(\\d+)"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("AWID");
    result.sourceText = originalText;

    int bitLength = 0;
    bool unknownFormat = false;

    QRegularExpressionMatch match = knownLine.match(normalizedText);
    if (match.hasMatch()) {
        bitLength = match.captured(1).toInt();
        result.facilityCode.set(match.captured(2).toUInt());
        result.cardNumber.set(match.captured(3).toULongLong());
        result.rawData = LFTextNormalizer::normalizeHex(match.captured(4));
    } else {
        match = unknownLine.match(normalizedText);
        if (match.hasMatch()) {
            unknownFormat = true;
            bitLength = match.captured(1).toInt();
            result.cardNumber.set(match.captured(2).toULongLong());
            result.rawData = LFTextNormalizer::normalizeHex(match.captured(3));
            result.addWarning(QStringLiteral("AWID length is not a known format; "
                                             "the reported number is the client's raw card value."));
        } else {
            match = knownNoRaw.match(normalizedText);
            if (match.hasMatch()) {
                bitLength = match.captured(1).toInt();
                result.facilityCode.set(match.captured(2).toUInt());
                result.cardNumber.set(match.captured(3).toULongLong());
                result.addWarning(QStringLiteral("AWID raw payload was not present in the client output."));
            } else {
                match = rawOnly.match(normalizedText);
                if (match.hasMatch()) {
                    result.rawData = LFTextNormalizer::normalizeHex(match.captured(1));
                    result.addWarning(QStringLiteral("AWID length and card fields were not present; "
                                                     "only the raw payload was recovered."));
                }
            }
        }
    }

    if (bitLength > 0)
        result.bitLength.set(bitLength);

    match = cansec.match(normalizedText);
    if (match.hasMatch()) {
        result.addExtra(QStringLiteral("cansec37Fc"), match.captured(1));
        result.addExtra(QStringLiteral("cansec37Cn"), match.captured(2));
        result.addWarning(QStringLiteral("AWID 37-bit also carries a Cansec interpretation; "
                                         "it is kept as an extra field."));
    }

    match = hcc40.match(normalizedText);
    if (match.hasMatch()) {
        result.addExtra(QStringLiteral("hcc40Id"),
                        LFTextNormalizer::normalizeHex(match.captured(1)));
    }

    match = rbh.match(normalizedText);
    if (match.hasMatch())
        result.addExtra(QStringLiteral("rbhCn"), match.captured(1));

    if (bitLength > 0) {
        if (unknownFormat)
            result.format = QStringLiteral("AWID %1-bit (unknown)").arg(bitLength);
        else
            result.format = QStringLiteral("AWID %1-bit").arg(bitLength);
    } else {
        result.format = QStringLiteral("AWID");
    }

    if (result.facilityCode.isPresent() || result.cardNumber.isPresent()
            || !result.rawData.isEmpty()) {
        result.status = Lf::Status::Identified;
        if (result.facilityCode.isPresent() && result.cardNumber.isPresent()
                && !result.rawData.isEmpty() && !unknownFormat)
            result.confidence = Lf::Confidence::High;
        else if (unknownFormat)
            result.confidence = Lf::Confidence::Low;
        else
            result.confidence = Lf::Confidence::Medium;
    } else {
        result.status = Lf::Status::Partial;
        result.confidence = Lf::Confidence::Low;
        result.addWarning(QStringLiteral("AWID marker found but no card fields were extracted."));
    }

    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    return result;
}

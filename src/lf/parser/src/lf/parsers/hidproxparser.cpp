#include "lf/parsers/hidproxparser.h"

#include <QRegularExpression>
#include <QStringList>

#include "lf/lftextnormalizer.h"

namespace {

struct HidCandidate
{
    QString wiegandName;
    QString description;
    QString parity;
    Lf::Optional<int> bitLength;
    Lf::Optional<quint32> facilityCode;
    Lf::Optional<quint64> cardNumber;
    Lf::Optional<quint32> issueLevel;
    Lf::Optional<quint32> oemCode;

    QString identityKey() const
    {
        return QStringLiteral("%1|%2|%3")
                .arg(bitLength.isPresent() ? QString::number(bitLength.value()) : QStringLiteral("-"))
                .arg(facilityCode.isPresent() ? QString::number(facilityCode.value()) : QStringLiteral("-"))
                .arg(cardNumber.isPresent() ? QString::number(cardNumber.value()) : QStringLiteral("-"));
    }

    QString summary() const
    {
        QStringList parts;
        parts << wiegandName;
        if (facilityCode.isPresent())
            parts << QStringLiteral("FC=%1").arg(facilityCode.value());
        if (cardNumber.isPresent())
            parts << QStringLiteral("CN=%1").arg(cardNumber.value());
        if (bitLength.isPresent())
            parts << QStringLiteral("%1-bit").arg(bitLength.value());
        return parts.join(QLatin1Char(' '));
    }

    bool isHidDescription() const
    {
        return description.contains(QStringLiteral("HID"), Qt::CaseInsensitive);
    }
};

bool captureU32(const QString &text, const QRegularExpression &expression, quint32 *value)
{
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch())
        return false;
    bool ok = false;
    const qulonglong parsed = match.captured(1).toULongLong(&ok);
    if (!ok || parsed > 0xFFFFFFFFULL)
        return false;
    *value = static_cast<quint32>(parsed);
    return true;
}

bool captureU64(const QString &text, const QRegularExpression &expression, quint64 *value)
{
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch())
        return false;
    bool ok = false;
    const qulonglong parsed = match.captured(1).toULongLong(&ok);
    if (!ok)
        return false;
    *value = static_cast<quint64>(parsed);
    return true;
}

int earliestFieldIndex(const QString &text)
{
    static const QStringList tokens = {
        QStringLiteral("FC:"),
        QStringLiteral("CN:"),
        QStringLiteral("Issue:"),
        QStringLiteral("OEM:"),
        QStringLiteral("parity (")
    };

    int earliest = -1;
    for (const QString &token : tokens) {
        const int index = text.indexOf(token);
        if (index >= 0 && (earliest < 0 || index < earliest))
            earliest = index;
    }
    return earliest;
}

bool parseCandidateLine(const QString &line, HidCandidate *candidate)
{
    static const QRegularExpression header(
        QStringLiteral("^\\s*(?:\\[[+\\-=!]{1,4}\\]\\s*)?\\[([A-Za-z][A-Za-z0-9_]{0,31})\\s*\\]\\s*(.+)$"));
    static const QRegularExpression bitLength(
        QStringLiteral("(\\d+)-bit"));
    static const QRegularExpression facilityCode(
        QStringLiteral("\\bFC:\\s*(\\d+)"));
    static const QRegularExpression cardNumber(
        QStringLiteral("\\bCN:\\s*(\\d+)"));
    static const QRegularExpression issueLevel(
        QStringLiteral("\\bIssue:\\s*(\\d+)"));
    static const QRegularExpression oemCode(
        QStringLiteral("\\bOEM:\\s*(\\d+)"));
    static const QRegularExpression parity(
        QStringLiteral("parity\\s*\\(\\s*(ok|fail)\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = header.match(line);
    if (!match.hasMatch())
        return false;

    const QString rest = match.captured(2);
    const int fieldsStart = earliestFieldIndex(rest);
    if (fieldsStart < 0)
        return false;

    candidate->wiegandName = match.captured(1).trimmed();
    candidate->description = rest.left(fieldsStart).trimmed();
    const QString fields = rest.mid(fieldsStart);

    const QRegularExpressionMatch bitMatch = bitLength.match(candidate->description);
    if (bitMatch.hasMatch()) {
        bool ok = false;
        const int bits = bitMatch.captured(1).toInt(&ok);
        if (ok && bits > 0 && bits <= 256)
            candidate->bitLength.set(bits);
    }

    quint32 number = 0;
    if (captureU32(fields, facilityCode, &number))
        candidate->facilityCode.set(number);

    quint64 bigNumber = 0;
    if (captureU64(fields, cardNumber, &bigNumber))
        candidate->cardNumber.set(bigNumber);

    if (captureU32(fields, issueLevel, &number))
        candidate->issueLevel.set(number);

    if (captureU32(fields, oemCode, &number))
        candidate->oemCode.set(number);

    const QRegularExpressionMatch parityMatch = parity.match(fields);
    if (parityMatch.hasMatch())
        candidate->parity = parityMatch.captured(1).toLower();

    return candidate->facilityCode.isPresent()
            || candidate->cardNumber.isPresent();
}

QString classifyFormat(const HidCandidate &candidate)
{
    const QString bits = candidate.bitLength.isPresent()
            ? QString::number(candidate.bitLength.value())
            : QString();

    if (candidate.wiegandName.compare(QStringLiteral("H10301"), Qt::CaseInsensitive) == 0) {
        return bits.isEmpty() ? QStringLiteral("H10301")
                              : QStringLiteral("H10301 (%1-bit)").arg(bits);
    }

    if (candidate.description.contains(QStringLiteral("Corporate 1000"), Qt::CaseInsensitive)) {
        return bits.isEmpty() ? QStringLiteral("Corporate 1000")
                              : QStringLiteral("Corporate 1000 (%1-bit)").arg(bits);
    }

    if (!bits.isEmpty()) {
        if (candidate.description.contains(QStringLiteral("HID"), Qt::CaseInsensitive))
            return QStringLiteral("HID %1-bit (%2)").arg(bits, candidate.wiegandName);
        return QStringLiteral("%1 (%2-bit)").arg(candidate.wiegandName, bits);
    }

    if (!candidate.description.isEmpty())
        return QStringLiteral("%1 (%2)").arg(candidate.wiegandName, candidate.description);

    return candidate.wiegandName;
}

HidCandidate preferredCandidate(const QList<HidCandidate> &candidates)
{
    for (const HidCandidate &candidate : candidates) {
        if (candidate.wiegandName.compare(QStringLiteral("H10301"), Qt::CaseInsensitive) == 0)
            return candidate;
    }
    for (const HidCandidate &candidate : candidates) {
        if (candidate.isHidDescription())
            return candidate;
    }
    return candidates.first();
}

} // namespace

Lf::Protocol HIDProxParser::protocol() const
{
    return Lf::Protocol::HIDProx;
}

QString HIDProxParser::name() const
{
    return QStringLiteral("HID Prox");
}

int HIDProxParser::detect(const QString &normalizedText, QStringList *evidence) const
{
    static const QRegularExpression candidateLine(
        QStringLiteral("(?m)^\\s*(?:\\[[+\\-=!]{1,4}\\]\\s*)?\\[[A-Za-z][A-Za-z0-9_]{0,31}\\s*\\]\\s*[^\\n]*\\b\\d+-bit\\b[^\\n]*\\bFC:"));

    if (candidateLine.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("Wiegand format candidate line with FC"));
        return 100;
    }

    const bool hasHidMarker = normalizedText.contains(QStringLiteral("HID Prox"), Qt::CaseInsensitive)
            || normalizedText.contains(QStringLiteral("HID Corporate"), Qt::CaseInsensitive);
    const bool hasFc = normalizedText.contains(QStringLiteral("FC:"));

    if (hasHidMarker && hasFc) {
        if (evidence)
            evidence->append(QStringLiteral("HID marker with FC field"));
        return 90;
    }

    static const QRegularExpression matchedFormats(
        QStringLiteral("found\\s+\\d+\\s+matching\\s+\\d+-bit"));
    if (matchedFormats.match(normalizedText).hasMatch()) {
        if (evidence)
            evidence->append(QStringLiteral("Wiegand match summary line"));
        return 80;
    }

    if (hasHidMarker) {
        if (evidence)
            evidence->append(QStringLiteral("HID Prox marker"));
        return 50;
    }

    return 0;
}

LFParseResult HIDProxParser::parse(const QString &normalizedText,
                                   const QString &originalText) const
{
    static const QRegularExpression rawLine(
        QStringLiteral("(?im)^\\s*(?:\\[\\+\\]\\s*)?raw:\\s*([0-9A-Fa-f]{6,24})\\s*$"));
    static const QRegularExpression bareFcCn(
        QStringLiteral("\\bFC:\\s*(\\d+)\\s+CN:\\s*(\\d+)"));
    static const QRegularExpression matchedFormats(
        QStringLiteral("found\\s+\\d+\\s+matching\\s+(\\d+)-bit"));

    LFParseResult result;
    result.protocol = protocol();
    result.displayName = QStringLiteral("HID Prox");
    result.sourceText = originalText;

    QList<HidCandidate> candidates;
    const QStringList lines = normalizedText.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        HidCandidate candidate;
        if (!parseCandidateLine(line, &candidate))
            continue;
        bool duplicate = false;
        for (const HidCandidate &existing : candidates) {
            if (existing.identityKey() == candidate.identityKey()
                    && existing.wiegandName == candidate.wiegandName
                    && existing.description == candidate.description) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            candidates.append(candidate);
    }

    Lf::Optional<int> globalBits;
    const QRegularExpressionMatch bitsMatch = matchedFormats.match(normalizedText);
    if (bitsMatch.hasMatch()) {
        bool ok = false;
        const int bits = bitsMatch.captured(1).toInt(&ok);
        if (ok && bits > 0)
            globalBits.set(bits);
    }

    const QRegularExpressionMatch rawMatch = rawLine.match(normalizedText);
    const QString raw = rawMatch.hasMatch()
            ? LFTextNormalizer::normalizeHex(rawMatch.captured(1))
            : QString();
    if (!raw.isEmpty())
        result.rawData = raw;

    HidCandidate chosen;
    bool haveChosen = false;
    bool ambiguous = false;

    if (candidates.isEmpty()) {
        const QRegularExpressionMatch bareMatch = bareFcCn.match(normalizedText);
        if (bareMatch.hasMatch()) {
            bool fcOk = false;
            bool cnOk = false;
            const qulonglong fcValue = bareMatch.captured(1).toULongLong(&fcOk);
            const qulonglong cnValue = bareMatch.captured(2).toULongLong(&cnOk);
            if (fcOk && cnOk) {
                result.facilityCode.set(static_cast<quint32>(fcValue));
                result.cardNumber.set(static_cast<quint64>(cnValue));
                result.format = QStringLiteral("Unknown HID format");
                result.addWarning(QStringLiteral("Client printed FC/CN without a Wiegand format name; "
                                                 "refusing to assume H10301."));
                result.status = Lf::Status::Partial;
                result.confidence = Lf::Confidence::Low;
            }
        }
        if (!result.facilityCode.isPresent() && result.rawData.isEmpty()) {
            result.status = Lf::Status::Partial;
            result.confidence = Lf::Confidence::Low;
            result.addWarning(QStringLiteral("HID Prox marker found but no Wiegand format or raw value was extracted."));
            return result;
        }
        if (result.rawData.isEmpty())
            result.confidence = Lf::Confidence::Low;
        else if (!result.facilityCode.isPresent())
            result.status = Lf::Status::Partial;
        result.hexId = result.rawData;
        return result;
    }

    if (candidates.size() == 1) {
        chosen = candidates.first();
        haveChosen = true;
    } else {
        bool sameIdentity = true;
        const QString reference = candidates.first().identityKey();
        for (const HidCandidate &candidate : candidates) {
            if (candidate.identityKey() != reference) {
                sameIdentity = false;
                break;
            }
        }

        if (sameIdentity) {
            chosen = preferredCandidate(candidates);
            haveChosen = true;
            result.addWarning(QStringLiteral("%1 Wiegand formats matched with identical values; "
                                             "values are reported once.").arg(candidates.size()));
        } else {
            QList<HidCandidate> hidCandidates;
            for (const HidCandidate &candidate : candidates) {
                if (candidate.isHidDescription())
                    hidCandidates.append(candidate);
            }

            if (hidCandidates.size() == 1) {
                chosen = hidCandidates.first();
                haveChosen = true;
                result.addWarning(QStringLiteral("Multiple Wiegand interpretations matched; "
                                                 "selected the single HID-specific one."));
            } else if (hidCandidates.size() > 1) {
                const QString hidReference = hidCandidates.first().identityKey();
                bool hidSame = true;
                for (const HidCandidate &candidate : hidCandidates) {
                    if (candidate.identityKey() != hidReference) {
                        hidSame = false;
                        break;
                    }
                }
                if (hidSame) {
                    chosen = preferredCandidate(hidCandidates);
                    haveChosen = true;
                    result.addWarning(QStringLiteral("%1 HID formats matched with identical values.")
                                      .arg(hidCandidates.size()));
                }
            }

            if (!haveChosen) {
                ambiguous = true;
                result.format = QStringLiteral("Ambiguous");
                result.status = Lf::Status::Partial;
                result.confidence = Lf::Confidence::Low;
                QStringList details;
                for (const HidCandidate &candidate : candidates)
                    details << candidate.summary();
                result.addWarning(QStringLiteral("Wiegand candidates disagree (%1); "
                                                 "facility code and card number were not assumed.")
                                  .arg(details.join(QStringLiteral("; "))));
            }
        }
    }

    for (const HidCandidate &candidate : candidates)
        result.formatCandidates << classifyFormat(candidate);

    if (haveChosen) {
        if (chosen.facilityCode.isPresent())
            result.facilityCode.set(chosen.facilityCode.value());
        if (chosen.cardNumber.isPresent())
            result.cardNumber.set(chosen.cardNumber.value());
        if (chosen.bitLength.isPresent())
            result.bitLength.set(chosen.bitLength.value());
        else if (globalBits.isPresent())
            result.bitLength.set(globalBits.value());

        result.format = classifyFormat(chosen);

        if (chosen.issueLevel.isPresent())
            result.addExtra(QStringLiteral("issueLevel"), QString::number(chosen.issueLevel.value()));
        if (chosen.oemCode.isPresent())
            result.addExtra(QStringLiteral("oemCode"), QString::number(chosen.oemCode.value()));
        if (!chosen.parity.isEmpty()) {
            result.addExtra(QStringLiteral("parity"), chosen.parity);
            if (chosen.parity == QStringLiteral("fail"))
                result.addWarning(QStringLiteral("Wiegand parity check failed on the client side."));
        }
        result.addExtra(QStringLiteral("wiegandFormat"), chosen.wiegandName);

        if (chosen.description.contains(QStringLiteral("Indala"), Qt::CaseInsensitive))
            result.addWarning(QStringLiteral("The matched Wiegand format is an Indala variant."));

        const bool hasValues = result.facilityCode.isPresent() || result.cardNumber.isPresent();
        result.status = hasValues ? Lf::Status::Identified : Lf::Status::Partial;

        if (result.status == Lf::Status::Identified) {
            if (result.facilityCode.isPresent() && result.cardNumber.isPresent()
                    && chosen.parity == QStringLiteral("ok") && result.bitLength.isPresent())
                result.confidence = Lf::Confidence::High;
            else
                result.confidence = Lf::Confidence::Medium;
        } else {
            result.confidence = Lf::Confidence::Low;
        }
    } else if (ambiguous) {
        if (globalBits.isPresent())
            result.bitLength.set(globalBits.value());
        result.addWarning(QStringLiteral("HID format could not be determined safely."));
    }

    if (!result.rawData.isEmpty())
        result.hexId = result.rawData;

    if (result.status == Lf::Status::Identified && result.rawData.isEmpty()
            && result.formatCandidates.size() > 1) {
        result.confidence = Lf::Confidence::Medium;
    }

    return result;
}

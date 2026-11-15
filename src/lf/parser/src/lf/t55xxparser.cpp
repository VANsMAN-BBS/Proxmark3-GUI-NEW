#include "lf/t55xxparser.h"

#include <QRegularExpression>

#include "lf/lftextnormalizer.h"

namespace LfT55xx {

namespace {

struct ModEntry { quint8 code; Modulation modulation; const char *name; };

const ModEntry kModulations[] = {
    {0x00, Modulation::NRZ, "NRZ"},
    {0x01, Modulation::PSK1, "PSK1"},
    {0x02, Modulation::PSK2, "PSK2"},
    {0x03, Modulation::PSK3, "PSK3"},
    {0x04, Modulation::FSK1, "FSK1"},
    {0x05, Modulation::FSK2, "FSK2"},
    {0x06, Modulation::FSK1a, "FSK1a"},
    {0x07, Modulation::FSK2a, "FSK2a"},
    {0x08, Modulation::ASK, "ASK"},
    {0x10, Modulation::Biphase, "Biphase"},
    {0x18, Modulation::BiphaseInverted, "Biphase inverted"}
};

const int kBitRateDivisors[] = {8, 16, 32, 40, 50, 64, 100, 128};

bool captureBool(const QString &text, const QRegularExpression &expression, bool *value)
{
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch())
        return false;
    const QString captured = match.captured(1).trimmed().toLower();
    *value = (captured == QStringLiteral("yes")
              || captured == QStringLiteral("true")
              || captured == QStringLiteral("1"));
    return true;
}

} // namespace

QString modulationName(Modulation modulation)
{
    for (const ModEntry &entry : kModulations) {
        if (entry.modulation == modulation)
            return QString::fromLatin1(entry.name);
    }
    return QStringLiteral("Unknown");
}

DetectInfo parseDetect(const QString &text)
{
    DetectInfo info;
    info.sourceText = text;

    const QString clean = LFTextNormalizer::normalize(text);
    if (clean.trimmed().isEmpty()) {
        info.warnings.append(QStringLiteral("Input is empty."));
        return info;
    }

    static const QRegularExpression chipType(
        QStringLiteral("Chip\\s*type\\s*\\.*\\s*:?\\s*(T55x7|T5555|T5577)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression password(
        QStringLiteral("Password\\s*set\\s*\\.*\\s*:?\\s*(Yes|No)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression block0(
        QStringLiteral("Block0\\s*\\.*\\s*:?\\s*([0-9A-Fa-f]{8})"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression modulation(
        QStringLiteral("Modulation\\s*\\.*\\s*:?\\s*([A-Za-z0-9 /-]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bitRate(
        QStringLiteral("Bit\\s*rate\\s*\\.*\\s*:?\\s*(?:RF/)?(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression offset(
        QStringLiteral("Offset\\s*\\.*\\s*:?\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression inverted(
        QStringLiteral("Inverted\\s*\\.*\\s*:?\\s*(Yes|No|True|False|1|0)"),
        QRegularExpression::CaseInsensitiveOption);

    info.detected = clean.contains(QStringLiteral("T55xx"), Qt::CaseInsensitive)
            || clean.contains(QStringLiteral("T5577"), Qt::CaseInsensitive)
            || clean.contains(QStringLiteral("T5555"), Qt::CaseInsensitive)
            || clean.contains(QStringLiteral("Chip type"), Qt::CaseInsensitive);

    QRegularExpressionMatch match = chipType.match(clean);
    if (match.hasMatch()) {
        info.chipType = match.captured(1);
        info.detected = true;
    }

    bool flag = false;
    if (captureBool(clean, password, &flag))
        info.passwordSet.set(flag);

    match = block0.match(clean);
    if (match.hasMatch()) {
        info.block0Hex = match.captured(1).toUpper();
        bool ok = false;
        const quint32 value = info.block0Hex.toUInt(&ok, 16);
        if (ok)
            info.block0.set(value);
    }

    match = modulation.match(clean);
    if (match.hasMatch()) {
        info.modulationText = match.captured(1).trimmed();
        info.extraFields.append(LfExtraField(QStringLiteral("modulation"), info.modulationText));
    }

    match = bitRate.match(clean);
    if (match.hasMatch()) {
        bool ok = false;
        const int value = match.captured(1).toInt(&ok);
        if (ok)
            info.bitRate.set(value);
    }

    match = offset.match(clean);
    if (match.hasMatch()) {
        bool ok = false;
        const int value = match.captured(1).toInt(&ok);
        if (ok)
            info.offset.set(value);
    }

    if (captureBool(clean, inverted, &flag))
        info.inverted.set(flag);

    if (!info.detected)
        info.warnings.append(QStringLiteral("No T55xx chip was reported in this output."));
    if (info.detected && !info.block0.isPresent())
        info.warnings.append(QStringLiteral("Chip reported without a readable block0 value."));

    return info;
}

DetectInfo parseInfo(const QString &text)
{
    return parseDetect(text);
}

Block0 parseBlock0(const QString &block0Hex)
{
    Block0 block;
    const QString hex = LFTextNormalizer::normalizeHex(block0Hex);
    if (hex.size() != 8) {
        block.warnings.append(QStringLiteral("block0 must be exactly 8 hex digits."));
        return block;
    }

    bool ok = false;
    const quint32 value = hex.toUInt(&ok, 16);
    if (!ok) {
        block.warnings.append(QStringLiteral("block0 is not a valid 32-bit hexadecimal value."));
        return block;
    }

    block.valid = true;
    block.raw = value;

    const quint8 modCode = static_cast<quint8>((value >> 12) & 0x1F);
    bool modKnown = false;
    for (const ModEntry &entry : kModulations) {
        if (entry.code == modCode) {
            block.modulation.set(entry.modulation);
            modKnown = true;
            break;
        }
    }
    if (!modKnown)
        block.warnings.append(QStringLiteral("Unknown modulation code 0x%1.").arg(modCode, 0, 16));

    block.extend = ((value >> 17) & 0x01) != 0;
    const int dbr = static_cast<int>((value >> 18) & (block.extend ? 0x3F : 0x07));
    block.bitRateIndex.set(dbr);
    if (dbr >= 0 && dbr < 8)
        block.bitRateDivisor.set(kBitRateDivisors[dbr]);
    else
        block.warnings.append(QStringLiteral("Extended data bit rate index %1 is not mapped.").arg(dbr));

    block.maxBlock.set(static_cast<int>((value >> 5) & 0x07));
    block.flagBits = static_cast<quint8>(value & 0x1F);
    block.masterKey.set(static_cast<quint8>((value >> 8) & 0x0F));

    block.fields.append(LfExtraField(QStringLiteral("raw"), hex));
    block.fields.append(LfExtraField(QStringLiteral("modulation"),
                                     modulationName(block.modulation.valueOr(Modulation::Unknown))));
    block.fields.append(LfExtraField(QStringLiteral("extend"),
                                     block.extend ? QStringLiteral("true") : QStringLiteral("false")));
    block.fields.append(LfExtraField(QStringLiteral("dataBitRateIndex"), QString::number(dbr)));
    if (block.bitRateDivisor.isPresent())
        block.fields.append(LfExtraField(QStringLiteral("dataBitRate"), QStringLiteral("RF/%1").arg(block.bitRateDivisor.value())));
    block.fields.append(LfExtraField(QStringLiteral("maxBlock"), QString::number(block.maxBlock.value())));
    block.fields.append(LfExtraField(QStringLiteral("flagBits"),
                                     QStringLiteral("0x%1").arg(QString::number(block.flagBits, 16).rightJustified(2, QLatin1Char('0')).toUpper())));
    block.fields.append(LfExtraField(QStringLiteral("masterKey"),
                                     QStringLiteral("0x%1").arg(QString::number(block.masterKey.value(), 16).rightJustified(2, QLatin1Char('0')).toUpper())));

    block.warnings.append(QStringLiteral("Bits 0..4 and 8..11 are exposed for diagnostics only; "
                                         "their meaning is not verified by this module."));
    return block;
}

} // namespace LfT55xx

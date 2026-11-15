#include "lfparsetypes.h"

namespace Lf {

QString protocolName(Protocol protocol)
{
    switch (protocol) {
    case Protocol::EM4100:
        return QStringLiteral("EM4100");
    case Protocol::HIDProx:
        return QStringLiteral("HID Prox");
    case Protocol::AWID:
        return QStringLiteral("AWID");
    case Protocol::Indala:
        return QStringLiteral("Indala");
    case Protocol::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

QString statusName(Status status)
{
    switch (status) {
    case Status::Identified:
        return QStringLiteral("identified");
    case Status::Partial:
        return QStringLiteral("partial");
    case Status::Unidentified:
        return QStringLiteral("unidentified");
    case Status::InvalidInput:
        return QStringLiteral("invalid-input");
    }
    return QStringLiteral("unknown");
}

QString confidenceName(Confidence confidence)
{
    switch (confidence) {
    case Confidence::High:
        return QStringLiteral("high");
    case Confidence::Medium:
        return QStringLiteral("medium");
    case Confidence::Low:
        return QStringLiteral("low");
    case Confidence::None:
        break;
    }
    return QStringLiteral("none");
}

} // namespace Lf

bool LFParseResult::hasIdentity() const
{
    return !rawData.isEmpty()
            || !hexId.isEmpty()
            || facilityCode.isPresent()
            || cardNumber.isPresent()
            || decimalId.isPresent();
}

void LFParseResult::addWarning(const QString &warning)
{
    if (!warning.isEmpty() && !warnings.contains(warning))
        warnings.append(warning);
}

void LFParseResult::addExtra(const QString &name, const QString &value)
{
    if (name.isEmpty())
        return;
    extraFields.append(LfExtraField(name, value));
}

void LFParseResult::addUnparsed(const QString &field)
{
    if (!field.isEmpty() && !unparsedFields.contains(field))
        unparsedFields.append(field);
}

QString LFParseResult::summary() const
{
    QStringList parts;
    parts << Lf::protocolName(protocol);
    if (!format.isEmpty())
        parts << format;
    if (facilityCode.isPresent())
        parts << QStringLiteral("FC=%1").arg(facilityCode.value());
    if (cardNumber.isPresent())
        parts << QStringLiteral("CN=%1").arg(cardNumber.value());
    if (!rawData.isEmpty())
        parts << QStringLiteral("raw=%1").arg(rawData);
    if (!hexId.isEmpty())
        parts << QStringLiteral("hex=%1").arg(hexId);
    return parts.join(QStringLiteral(", "));
}

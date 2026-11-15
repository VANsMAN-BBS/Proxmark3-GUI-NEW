#include "ntagmodel.h"

#include <QStringList>

namespace Ntag {
namespace {

QString printable(const QByteArray &data, int limit = 64)
{
    QString out;
    const int count = qMin(limit, data.size());
    for(int i = 0; i < count; i++)
    {
        const unsigned char value = static_cast<unsigned char>(data.at(i));
        out += (value >= 0x20 && value < 0x7F) ? QChar(value) : QLatin1Char('.');
    }
    if(data.size() > count)
        out += QStringLiteral("...");
    return out;
}

} // namespace

QString modelName(Model model)
{
    switch(model)
    {
    case Model::NTAG213: return QStringLiteral("NTAG213");
    case Model::NTAG215: return QStringLiteral("NTAG215");
    case Model::NTAG216: return QStringLiteral("NTAG216");
    default: return QStringLiteral("Unknown Type 2 Tag");
    }
}

QString confidenceName(Confidence confidence)
{
    switch(confidence)
    {
    case Confidence::Exact: return QStringLiteral("Exact");
    case Confidence::Probable: return QStringLiteral("Probable");
    default: return QStringLiteral("Unknown");
    }
}

QString pageStateName(PageState state)
{
    switch(state)
    {
    case PageState::Read: return QStringLiteral("Read");
    case PageState::Failed: return QStringLiteral("Read failed");
    case PageState::Unavailable: return QStringLiteral("Unavailable");
    default: return QStringLiteral("Not read");
    }
}

QString pageClassName(PageClass pageClass)
{
    switch(pageClass)
    {
    case PageClass::Writable: return QStringLiteral("Writable");
    case PageClass::ReadOnly: return QStringLiteral("ReadOnly");
    case PageClass::Manufacturer: return QStringLiteral("Manufacturer");
    case PageClass::UID: return QStringLiteral("UID");
    case PageClass::Lock: return QStringLiteral("Lock");
    case PageClass::Config: return QStringLiteral("Config");
    case PageClass::Security: return QStringLiteral("Security");
    case PageClass::NotApplicable: return QStringLiteral("NotApplicable");
    default: return QStringLiteral("Unknown");
    }
}

QString verifyStatusName(VerifyStatus status)
{
    switch(status)
    {
    case VerifyStatus::Passed: return QStringLiteral("Passed");
    case VerifyStatus::PassedWithExceptions: return QStringLiteral("Passed with Exceptions");
    case VerifyStatus::Failed: return QStringLiteral("Failed");
    default: return QStringLiteral("Incomplete");
    }
}

Version parseVersion(const QByteArray &raw)
{
    Version version;
    QByteArray bytes = raw;
    // Some clients/dumps prefix the 7-byte GET_VERSION answer with 0x00.
    if(bytes.size() >= 8 && static_cast<quint8>(bytes.at(0)) == 0x00)
        bytes.remove(0, 1);
    if(bytes.size() < 7)
        return version;
    version.valid = true;
    version.raw = bytes.left(7);
    version.vendor = static_cast<quint8>(bytes.at(0));
    version.productType = static_cast<quint8>(bytes.at(1));
    version.productSubtype = static_cast<quint8>(bytes.at(2));
    version.major = static_cast<quint8>(bytes.at(3));
    version.minor = static_cast<quint8>(bytes.at(4));
    version.storageSize = static_cast<quint8>(bytes.at(5));
    version.protocol = static_cast<quint8>(bytes.at(6));
    return version;
}

Capability capacityFor(Model model, Confidence confidence, const QString &basis)
{
    Capability capacity;
    capacity.model = model;
    capacity.confidence = confidence;
    capacity.basis = basis;
    switch(model)
    {
    case Model::NTAG213:
        capacity.pages = 45;
        capacity.userLast = 39;
        capacity.lockPage = 40;
        capacity.cfg0 = 41;
        capacity.cfg1 = 42;
        capacity.pwd = 43;
        capacity.pack = 44;
        break;
    case Model::NTAG215:
        capacity.pages = 135;
        capacity.userLast = 129;
        capacity.lockPage = 130;
        capacity.cfg0 = 131;
        capacity.cfg1 = 132;
        capacity.pwd = 133;
        capacity.pack = 134;
        break;
    case Model::NTAG216:
        capacity.pages = 231;
        capacity.userLast = 225;
        capacity.lockPage = 226;
        capacity.cfg0 = 227;
        capacity.cfg1 = 228;
        capacity.pwd = 229;
        capacity.pack = 230;
        break;
    default:
        break;
    }
    return capacity;
}

CC parseCC(const QByteArray &raw4)
{
    CC cc;
    if(raw4.size() < 4)
        return cc;
    cc.raw = raw4.left(4);
    cc.magic = static_cast<quint8>(raw4.at(0));
    cc.version = static_cast<quint8>(raw4.at(1));
    cc.size = static_cast<quint8>(raw4.at(2));
    cc.ndefCapacity = cc.size * 8;
    cc.readWrite = (static_cast<quint8>(raw4.at(3)) & 0x0F) == 0;
    cc.multiBlock = (static_cast<quint8>(raw4.at(3)) & 0x01) != 0;
    cc.valid = cc.magic == 0xE1;
    return cc;
}

Capability identify(const Version &version, const CC &cc, const QString &clientType)
{
    Model fromVersion = Model::Unknown;
    QString versionBasis;
    switch(version.storageSize)
    {
    case 0x0F: fromVersion = Model::NTAG213; break;
    case 0x11: fromVersion = Model::NTAG215; break;
    case 0x13: fromVersion = Model::NTAG216; break;
    default: break;
    }
    if(version.valid && fromVersion != Model::Unknown)
        versionBasis = QStringLiteral("GET_VERSION storage 0x%1")
                .arg(version.storageSize, 2, 16, QLatin1Char('0')).toUpper();

    Model fromCC = Model::Unknown;
    if(cc.valid)
    {
        switch(cc.size)
        {
        case 0x12: fromCC = Model::NTAG213; break;
        case 0x3E: fromCC = Model::NTAG215; break;
        case 0x6D: fromCC = Model::NTAG216; break;
        default: break;
        }
    }

    Model fromType = Model::Unknown;
    if(clientType.contains(QStringLiteral("NTAG 213"), Qt::CaseInsensitive))
        fromType = Model::NTAG213;
    else if(clientType.contains(QStringLiteral("NTAG 215"), Qt::CaseInsensitive))
        fromType = Model::NTAG215;
    else if(clientType.contains(QStringLiteral("NTAG 216"), Qt::CaseInsensitive))
        fromType = Model::NTAG216;

    const Model model = (fromVersion != Model::Unknown) ? fromVersion
                      : (fromCC != Model::Unknown) ? fromCC
                      : fromType;

    if(model == Model::Unknown)
        return capacityFor(Model::Unknown, Confidence::Unknown,
                           QStringLiteral("No GET_VERSION storage size, CC or client type matched"));

    QStringList basis;
    if(fromVersion != Model::Unknown)
        basis << versionBasis;
    if(fromCC != Model::Unknown)
        basis << QStringLiteral("CC size 0x%1").arg(cc.size, 2, 16, QLatin1Char('0')).toUpper();
    if(fromType != Model::Unknown)
        basis << QStringLiteral("client type");
    if(version.valid && fromVersion != Model::Unknown && cc.valid
            && fromCC != Model::Unknown && fromCC != fromVersion)
        basis << QStringLiteral("WARNING: CC conflicts with GET_VERSION");

    Confidence confidence = Confidence::Probable;
    if(fromVersion != Model::Unknown && (!cc.valid || fromCC == Model::Unknown || fromCC == fromVersion))
        confidence = Confidence::Exact;
    else if(fromVersion == Model::Unknown && fromCC != Model::Unknown)
        confidence = Confidence::Probable;
    else if(fromVersion == Model::Unknown && fromCC == Model::Unknown)
        confidence = Confidence::Probable;

    return capacityFor(model, confidence, basis.join(QStringLiteral("; ")));
}

Dump parseDump(const QByteArray &file)
{
    Dump dump;
    if(file.size() < 56)
    {
        dump.error = QStringLiteral("dump file is shorter than the 56-byte header");
        return dump;
    }
    const int payload = file.size() - 56;
    if(payload <= 0 || payload % 4 != 0)
    {
        dump.error = QStringLiteral("dump payload is not a multiple of 4 bytes");
        return dump;
    }
    dump.versionRaw = file.left(8);
    dump.maxPage = static_cast<quint8>(file.at(0x0B));
    const int count = payload / 4;
    for(int i = 0; i < count; i++)
        dump.pages.append(file.mid(56 + i * 4, 4));
    if(dump.maxPage + 1 > count)
    {
        // The header claims more pages than the file holds; keep what we have
        // and report the inconsistency instead of inventing pages.
        dump.error = QStringLiteral("header max page %1 exceeds the %2 pages in the file")
                .arg(dump.maxPage).arg(count);
    }
    dump.valid = true;
    return dump;
}

QString regionName(const Capability &capacity, int page)
{
    if(page < 0)
        return QString();
    if(capacity.pages > 0 && page >= capacity.pages)
        return QStringLiteral("Out of range");
    if(page == 0) return QStringLiteral("Manufacturer / UID0-2 / BCC0");
    if(page == 1) return QStringLiteral("UID3-6");
    if(page == 2) return QStringLiteral("BCC1 / Internal / Lock");
    if(page == 3) return QStringLiteral("Capability Container");
    if(capacity.lockPage >= 0 && page == capacity.lockPage) return QStringLiteral("Dynamic lock");
    if(capacity.cfg0 >= 0 && page == capacity.cfg0) return QStringLiteral("Configuration 0");
    if(capacity.cfg1 >= 0 && page == capacity.cfg1) return QStringLiteral("Configuration 1");
    if(capacity.pwd >= 0 && page == capacity.pwd) return QStringLiteral("PWD");
    if(capacity.pack >= 0 && page == capacity.pack) return QStringLiteral("PACK");
    if(page >= 4 && (capacity.userLast < 0 || page <= capacity.userLast))
        return QStringLiteral("User data");
    return QStringLiteral("Vendor / reserved");
}

QString uriPrefix(quint8 code)
{
    static const char *prefixes[] = {
        "", "http://www.", "https://www.", "http://", "https://",
        "tel:", "mailto:", "ftp://anonymous:anonymous@", "ftp://ftp.",
        "ftps://", "sftp://", "smb://", "nfs://", "ftp://", "dav://",
        "news:", "telnet://", "imap:", "rtsp://", "urn:", "pop:",
        "sip:", "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://",
        "tcpobex://", "irdaobex://", "file://", "urn:epc:id:", "urn:epc:tag:",
        "urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:"
    };
    if(code < static_cast<quint8>(sizeof(prefixes) / sizeof(prefixes[0])))
        return QString::fromLatin1(prefixes[code]);
    return QString();
}

namespace {

bool parseRecord(const QByteArray &message, int &offset, NdefRecord *record,
                 QString *error)
{
    if(offset >= message.size())
    {
        *error = QStringLiteral("truncated NDEF record header");
        return false;
    }
    const int start = offset;
    const quint8 header = static_cast<quint8>(message.at(offset++));
    if(offset >= message.size())
    {
        *error = QStringLiteral("truncated NDEF type length");
        return false;
    }
    const int typeLength = static_cast<quint8>(message.at(offset++));
    const bool shortRecord = (header & 0x10) != 0;
    const bool hasId = (header & 0x08) != 0;
    const quint8 tnf = header & 0x07;

    quint32 payloadLength = 0;
    if(shortRecord)
    {
        if(offset + 1 > message.size())
        {
            *error = QStringLiteral("truncated NDEF payload length");
            return false;
        }
        payloadLength = static_cast<quint8>(message.at(offset++));
    }
    else
    {
        if(offset + 4 > message.size())
        {
            *error = QStringLiteral("truncated NDEF payload length");
            return false;
        }
        payloadLength = (static_cast<quint8>(message.at(offset)) << 24)
                | (static_cast<quint8>(message.at(offset + 1)) << 16)
                | (static_cast<quint8>(message.at(offset + 2)) << 8)
                | static_cast<quint8>(message.at(offset + 3));
        offset += 4;
    }

    int idLength = 0;
    if(hasId)
    {
        if(offset + 1 > message.size())
        {
            *error = QStringLiteral("truncated NDEF ID length");
            return false;
        }
        idLength = static_cast<quint8>(message.at(offset++));
    }

    if(offset + typeLength > message.size())
    {
        *error = QStringLiteral("truncated NDEF type field");
        return false;
    }
    const QByteArray type = message.mid(offset, typeLength);
    offset += typeLength;

    if(offset + idLength > message.size())
    {
        *error = QStringLiteral("truncated NDEF ID field");
        return false;
    }
    offset += idLength;
    if(payloadLength > static_cast<quint32>(message.size() - offset))
    {
        *error = QStringLiteral("NDEF payload length exceeds the message");
        return false;
    }
    const QByteArray payload = message.mid(offset, static_cast<int>(payloadLength));
    offset += static_cast<int>(payloadLength);

    record->tnf = QStringLiteral("0x%1").arg(tnf, 2, 16, QLatin1Char('0')).toUpper();
    record->type = QString::fromLatin1(type);
    record->payload = payload;
    record->raw = message.mid(start, offset - start);

    if(tnf == 0x01 && type == QByteArray("T") && payload.size() >= 1)
    {
        const quint8 status = static_cast<quint8>(payload.at(0));
        const int languageLength = status & 0x3F;
        if(1 + languageLength <= payload.size())
        {
            const QByteArray text = payload.mid(1 + languageLength);
            record->typeName = QStringLiteral("Text");
            if(status & 0x80)
            {
                record->text = QString::fromUtf8(text);
            }
            else
            {
                // NDEF text with the UTF-8 flag clear is UTF-16BE.
                QString utf16;
                for(int i = 0; i + 1 < text.size(); i += 2)
                    utf16 += QChar((static_cast<quint8>(text.at(i)) << 8)
                                   | static_cast<quint8>(text.at(i + 1)));
                record->text = utf16;
            }
        }
    }
    else if(tnf == 0x01 && type == QByteArray("U") && payload.size() >= 1)
    {
        const quint8 prefix = static_cast<quint8>(payload.at(0));
        record->typeName = QStringLiteral("URI");
        record->uri = uriPrefix(prefix) + QString::fromUtf8(payload.mid(1));
        record->text = record->uri;
    }
    else if(tnf == 0x02)
    {
        record->typeName = QStringLiteral("MIME");
        record->mime = record->type;
        record->text = printable(payload);
    }
    else if(tnf == 0x04)
    {
        record->typeName = QStringLiteral("External");
        record->text = printable(payload);
    }
    else if(tnf == 0x00)
    {
        record->typeName = QStringLiteral("Empty");
    }
    else
    {
        record->typeName = QStringLiteral("Unknown");
        record->text = printable(payload);
    }
    return true;
}

} // namespace

NdefResult parseNdef(const QByteArray &userArea)
{
    NdefResult result;
    if(userArea.isEmpty())
    {
        result.error = QStringLiteral("empty user data area");
        return result;
    }

    int offset = 0;
    QByteArray message;
    while(offset < userArea.size())
    {
        const quint8 tag = static_cast<quint8>(userArea.at(offset));
        if(tag == 0x00) { offset++; continue; }
        if(tag == 0xFE) break;
        if(offset + 1 >= userArea.size())
        {
            result.error = QStringLiteral("truncated TLV header");
            return result;
        }
        int length = static_cast<quint8>(userArea.at(offset + 1));
        int headerLength = 2;
        if(length == 0xFF)
        {
            if(offset + 3 >= userArea.size())
            {
                result.error = QStringLiteral("truncated 3-byte TLV length");
                return result;
            }
            length = (static_cast<quint8>(userArea.at(offset + 2)) << 8)
                    | static_cast<quint8>(userArea.at(offset + 3));
            headerLength = 4;
        }
        if(offset + headerLength + length > userArea.size())
        {
            result.error = QStringLiteral("TLV length exceeds the user data area");
            return result;
        }
        if(tag == 0x03)
        {
            message = userArea.mid(offset + headerLength, length);
            break;
        }
        offset += headerLength + length;
    }

    if(message.isEmpty())
    {
        result.error = QStringLiteral("no NDEF message TLV (0x03) found");
        return result;
    }
    result.message = message;

    int moffset = 0;
    while(moffset < message.size())
    {
        NdefRecord record;
        QString error;
        if(!parseRecord(message, moffset, &record, &error))
        {
            result.error = error;
            return result;
        }
        result.records.append(record);
    }
    result.ok = !result.records.isEmpty();
    if(!result.ok)
        result.error = QStringLiteral("no NDEF records");
    return result;
}

PageClass classifyPage(const Capability &capacity, int page)
{
    if(capacity.model == Model::Unknown || capacity.pages <= 0)
        return PageClass::Unknown;
    if(page < 0 || page >= capacity.pages)
        return PageClass::NotApplicable;
    if(page == 0)
        return PageClass::Manufacturer;
    if(page == 1)
        return PageClass::UID;
    if(page == 2)
        return PageClass::Lock;
    if(page == 3)
        return PageClass::Config;
    if(capacity.lockPage >= 0 && page == capacity.lockPage)
        return PageClass::Lock;
    if((capacity.cfg0 >= 0 && page == capacity.cfg0) ||
       (capacity.cfg1 >= 0 && page == capacity.cfg1))
        return PageClass::Config;
    if((capacity.pwd >= 0 && page == capacity.pwd) ||
       (capacity.pack >= 0 && page == capacity.pack))
        return PageClass::Security;
    const int userLast = capacity.userLast >= 0
            ? capacity.userLast : capacity.pages - 1;
    if(page >= capacity.userFirst && page <= userLast)
        return PageClass::Writable;
    return PageClass::NotApplicable;
}

ErasePlan planErase(const Capability &capacity, const QList<QByteArray> &pages)
{
    ErasePlan plan;
    plan.totalPages = capacity.pages;
    plan.userFirst = capacity.userFirst;
    plan.userLast = capacity.userLast;

    if(capacity.model == Model::Unknown || capacity.pages <= 0 ||
       capacity.confidence == Confidence::Unknown)
    {
        plan.error = QStringLiteral("model or layout is unknown");
        return plan;
    }
    if(capacity.userLast < capacity.userFirst)
    {
        plan.error = QStringLiteral("no writable user area in the layout");
        return plan;
    }
    if(pages.size() < capacity.pages)
    {
        plan.error = QStringLiteral("tag data is incomplete (%1 of %2 pages read)")
                .arg(pages.size()).arg(capacity.pages);
        return plan;
    }

    for(int page = capacity.userFirst; page <= capacity.userLast; page++)
    {
        if(page < 0 || page >= pages.size() || pages.at(page).size() != 4)
        {
            plan.error = QStringLiteral("user page %1 was not read").arg(page);
            return plan;
        }
        const QByteArray &data = pages.at(page);
        if(data == QByteArray(4, '\0'))
            plan.blankPages.append(page);
        else
            plan.clearPages.append(page);
    }

    if(pages.size() > 2 && pages.at(2).size() == 4)
    {
        plan.staticLock = pages.at(2).mid(2, 2);
        for(char byte : plan.staticLock)
            if(byte != '\0')
                plan.lockBitsPresent = true;
    }
    if(capacity.lockPage >= 0 && capacity.lockPage < pages.size())
    {
        const QByteArray lock = pages.at(capacity.lockPage);
        const int count = qMin(3, lock.size());
        plan.dynamicLock = lock.left(count);
        for(int i = 0; i < count; i++)
            if(lock.at(i) != '\0')
                plan.lockBitsPresent = true;
    }

    plan.valid = true;
    return plan;
}

VerifyReport verifyWrite(const Capability &capacity,
                         const QMap<int, QByteArray> &expected,
                         const QList<QByteArray> &actual,
                         const QVector<PageState> &states)
{
    VerifyReport report;
    bool anyMismatch = false;
    bool anyUnchecked = false;
    bool anyException = false;

    for(auto it = expected.constBegin(); it != expected.constEnd(); ++it)
    {
        const int page = it.key();
        PageCheck check;
        check.page = page;
        check.pageClass = classifyPage(capacity, page);
        check.expected = it.value();

        const bool stateRead = page < states.size()
                ? states.at(page) == PageState::Read : true;
        const bool hasActual = page >= 0 && page < actual.size() &&
                actual.at(page).size() == 4 && stateRead;
        if(hasActual)
            check.actual = actual.at(page);

        if(check.pageClass != PageClass::Writable)
        {
            // Read-only/system pages never count as write failures.
            check.verdict = hasActual ? PageVerdict::Preserved
                                      : PageVerdict::NotApplicable;
            if(!hasActual)
                check.note = QStringLiteral("not readable");
            anyException = true;
            report.checks.append(check);
            continue;
        }

        report.writableTargets++;
        if(!hasActual)
        {
            check.verdict = PageVerdict::Unchecked;
            check.note = QStringLiteral("page was not read");
            anyUnchecked = true;
            report.unchecked++;
        }
        else if(check.actual == check.expected)
        {
            check.verdict = PageVerdict::Match;
            report.writablePassed++;
        }
        else
        {
            check.verdict = PageVerdict::Mismatch;
            anyMismatch = true;
            report.writableFailed++;
        }
        report.checks.append(check);
    }

    if(anyMismatch)
        report.status = VerifyStatus::Failed;
    else if(anyUnchecked || report.writableTargets == 0)
        report.status = VerifyStatus::Incomplete;
    else if(anyException)
        report.status = VerifyStatus::PassedWithExceptions;
    else
        report.status = VerifyStatus::Passed;
    return report;
}

} // namespace Ntag


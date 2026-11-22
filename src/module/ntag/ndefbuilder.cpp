#include "ndefbuilder.h"

#include "ntagmodel.h"

#include <QChar>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace Ndef {
namespace {

const int kMaxTlvMessage = 0xFFFF;

QByteArray recordHeader(quint8 tnf, int typeLength, int payloadLength,
                        int idLength, bool first, bool last)
{
    quint8 header = static_cast<quint8>(tnf & 0x07);
    if(first)
        header |= 0x80; // MB
    if(last)
        header |= 0x40; // ME
    const bool shortRecord = payloadLength <= 255;
    if(shortRecord)
        header |= 0x10; // SR
    if(idLength > 0)
        header |= 0x08; // IL

    QByteArray record;
    record.append(static_cast<char>(header));
    record.append(static_cast<char>(typeLength & 0xFF));
    if(shortRecord)
    {
        record.append(static_cast<char>(payloadLength & 0xFF));
    }
    else
    {
        record.append(static_cast<char>((payloadLength >> 24) & 0xFF));
        record.append(static_cast<char>((payloadLength >> 16) & 0xFF));
        record.append(static_cast<char>((payloadLength >> 8) & 0xFF));
        record.append(static_cast<char>(payloadLength & 0xFF));
    }
    if(idLength > 0)
        record.append(static_cast<char>(idLength & 0xFF));
    return record;
}

bool finishPayload(LaunchPayload *payload, const QByteArray &message,
                   const QString &summary)
{
    payload->summary = summary;
    payload->message = message;
    payload->tlv = wrapTlv(message);
    if(payload->tlv.isEmpty())
    {
        payload->error = QStringLiteral("NDEF message is too large for a Type 2 Tag TLV");
        payload->message.clear();
        return false;
    }
    payload->requiredBytes = tlvLength(message);
    payload->userArea = payload->tlv;
    payload->userArea.append(static_cast<char>(0xFE));
    while(payload->userArea.size() % 4 != 0)
        payload->userArea.append('\0');
    payload->ok = true;
    return true;
}

QByteArray uriRecord(const QString &uri, quint8 prefixCode)
{
    const QString prefix = Ntag::uriPrefix(prefixCode);
    QByteArray rest;
    if(!prefix.isEmpty() && uri.startsWith(prefix))
        rest = uri.mid(prefix.size()).toUtf8();
    else
        rest = uri.toUtf8();

    QByteArray payloadBytes;
    payloadBytes.append(static_cast<char>(prefixCode));
    payloadBytes += rest;

    QList<RecordSpec> records;
    RecordSpec record;
    record.tnf = 0x01;
    record.type = QByteArrayLiteral("U");
    record.payload = payloadBytes;
    records.append(record);
    return buildMessage(records, nullptr);
}

quint8 bestPrefix(const QString &uri)
{
    quint8 best = 0;
    int bestLength = 0;
    for(int code = 1; code <= 0x23; code++)
    {
        const QString prefix = Ntag::uriPrefix(static_cast<quint8>(code));
        if(prefix.isEmpty())
            continue;
        if(uri.startsWith(prefix) && prefix.size() > bestLength)
        {
            best = static_cast<quint8>(code);
            bestLength = prefix.size();
        }
    }
    return best;
}

bool hasScheme(const QString &uri)
{
    static const QRegularExpression scheme(
                QStringLiteral("^[A-Za-z][A-Za-z0-9+.\\-]*:"));
    return scheme.match(uri).hasMatch();
}

bool hasControlChars(const QString &text)
{
    for(const QChar ch : text)
        if(ch.unicode() < 0x20 || ch.unicode() == 0x7F)
            return true;
    return false;
}

QString percentEncode(const QString &text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

QString digitsOnly(const QString &text)
{
    QString out;
    for(const QChar ch : text)
        if(ch.isDigit() || ch == QLatin1Char('+'))
            out += ch;
    return out;
}

bool validPhone(const QString &text, QString *error)
{
    const QString digits = digitsOnly(text);
    if(digits.count(QRegularExpression(QStringLiteral("\\d"))) < 3)
    {
        *error = QStringLiteral("phone number needs at least 3 digits");
        return false;
    }
    for(const QChar ch : text)
    {
        if(!ch.isDigit() && ch != QLatin1Char('+') && ch != QLatin1Char('-')
                && ch != QLatin1Char(' ') && ch != QLatin1Char('(')
                && ch != QLatin1Char(')'))
        {
            *error = QStringLiteral("phone number contains an unsupported character");
            return false;
        }
    }
    return true;
}

bool validLanguage(const QString &language, QString *error)
{
    if(language.size() > 63)
    {
        *error = QStringLiteral("language code must be at most 63 bytes");
        return false;
    }
    for(const QChar ch : language)
    {
        const bool ok = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
                || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
                || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                || ch == QLatin1Char('-');
        if(!ok)
        {
            *error = QStringLiteral("language code must be ASCII letters, digits or '-'");
            return false;
        }
    }
    return true;
}

bool validMimeType(const QString &mime, QString *error, bool *nonAscii)
{
    if(nonAscii)
        *nonAscii = false;
    if(mime.isEmpty())
    {
        *error = QStringLiteral("MIME type is empty");
        return false;
    }
    if(mime.toUtf8().size() > 255)
    {
        *error = QStringLiteral("MIME type is longer than 255 bytes");
        return false;
    }
    if(!mime.contains(QLatin1Char('/')))
    {
        *error = QStringLiteral("MIME type needs a '/' such as text/plain");
        return false;
    }
    const QStringList parts = mime.split(QLatin1Char('/'));
    if(parts.size() != 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty())
    {
        *error = QStringLiteral("MIME type needs non-empty type and subtype");
        return false;
    }
    for(const QChar ch : mime)
    {
        if(ch.unicode() < 0x20 || ch.unicode() == 0x7F || ch.isSpace())
        {
            *error = QStringLiteral("MIME type contains whitespace or a control character");
            return false;
        }
        if(ch.unicode() > 0x7E && nonAscii)
            *nonAscii = true;
    }
    return true;
}

bool parseHexPayload(const QString &text, QByteArray *bytes, QString *error)
{
    QString cleaned = text;
    cleaned.remove(QRegularExpression(QStringLiteral("[\\s:,\\-_]")));
    if(cleaned.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        cleaned.remove(0, 2);
    if(cleaned.isEmpty())
    {
        *error = QStringLiteral("hex payload is empty");
        return false;
    }
    if(cleaned.size() % 2 != 0)
    {
        *error = QStringLiteral("hex payload has an odd number of digits");
        return false;
    }
    for(const QChar ch : cleaned)
    {
        const bool hex = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
                || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
        if(!hex)
        {
            *error = QStringLiteral("hex payload contains '%1'").arg(ch);
            return false;
        }
    }
    *bytes = QByteArray::fromHex(cleaned.toLatin1());
    return true;
}

QString vcardEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char(';'), QStringLiteral("\\;"));
    escaped.replace(QLatin1Char(','), QStringLiteral("\\,"));
    escaped.replace(QLatin1Char('\r'), QLatin1Char(' '));
    escaped.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return escaped.trimmed();
}

QByteArray wscAttribute(quint16 id, const QByteArray &value)
{
    QByteArray out;
    out.append(static_cast<char>((id >> 8) & 0xFF));
    out.append(static_cast<char>(id & 0xFF));
    const int length = value.size();
    out.append(static_cast<char>((length >> 8) & 0xFF));
    out.append(static_cast<char>(length & 0xFF));
    out += value;
    return out;
}

QByteArray wscU16(quint16 value)
{
    QByteArray out;
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
    return out;
}

bool parseMac(const QString &text, QByteArray *mac, QString *error)
{
    QString cleaned = text;
    cleaned.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
    if(cleaned.size() != 12)
    {
        *error = QStringLiteral("Bluetooth address needs 6 bytes (12 hex digits)");
        return false;
    }
    *mac = QByteArray::fromHex(cleaned.toLatin1());
    return true;
}

} // namespace

QByteArray buildMessage(const QList<RecordSpec> &records, QString *error)
{
    if(error)
        error->clear();
    if(records.isEmpty())
    {
        if(error)
            *error = QStringLiteral("the NDEF message has no records");
        return QByteArray();
    }

    QByteArray message;
    for(int index = 0; index < records.size(); index++)
    {
        const RecordSpec &record = records.at(index);
        if(record.tnf > 0x07)
        {
            if(error)
                *error = QStringLiteral("record %1 has an invalid TNF").arg(index + 1);
            return QByteArray();
        }
        if(record.type.size() > 255)
        {
            if(error)
                *error = QStringLiteral("record %1 type is longer than 255 bytes")
                        .arg(index + 1);
            return QByteArray();
        }
        if(record.id.size() > 255)
        {
            if(error)
                *error = QStringLiteral("record %1 ID is longer than 255 bytes")
                        .arg(index + 1);
            return QByteArray();
        }
        if(static_cast<quint8>(record.tnf) == 0x00
                && (!record.type.isEmpty() || !record.payload.isEmpty()
                    || !record.id.isEmpty()))
        {
            if(error)
                *error = QStringLiteral("record %1 is TNF 0 but carries data")
                        .arg(index + 1);
            return QByteArray();
        }
        const bool first = index == 0;
        const bool last = index == records.size() - 1;
        message += recordHeader(static_cast<quint8>(record.tnf),
                                record.type.size(), record.payload.size(),
                                record.id.size(), first, last);
        message += record.type;
        if(!record.id.isEmpty())
            message += record.id;
        message += record.payload;
        if(message.size() > kMaxTlvMessage)
        {
            if(error)
                *error = QStringLiteral("the NDEF message exceeds 65535 bytes");
            return QByteArray();
        }
    }
    return message;
}

QByteArray wrapTlv(const QByteArray &message)
{
    if(message.size() < 0 || message.size() > kMaxTlvMessage)
        return QByteArray();

    QByteArray tlv;
    tlv.append(static_cast<char>(0x03));
    if(message.size() < 255)
    {
        tlv.append(static_cast<char>(message.size()));
    }
    else
    {
        tlv.append(static_cast<char>(0xFF));
        tlv.append(static_cast<char>((message.size() >> 8) & 0xFF));
        tlv.append(static_cast<char>(message.size() & 0xFF));
    }
    tlv += message;
    return tlv;
}

int tlvLength(const QByteArray &message)
{
    if(message.size() < 0 || message.size() > kMaxTlvMessage)
        return 0;
    const int header = message.size() < 255 ? 2 : 4;
    return header + message.size() + 1;
}

LaunchPayload buildUri(const QString &uri)
{
    LaunchPayload payload;
    const QString trimmed = uri.trimmed();
    if(trimmed.isEmpty())
    {
        payload.error = QStringLiteral("URI is empty");
        return payload;
    }
    if(hasControlChars(trimmed))
    {
        payload.error = QStringLiteral("URI contains control characters");
        return payload;
    }
    if(!hasScheme(trimmed))
    {
        payload.error = QStringLiteral("URI needs a scheme such as https: or tel:");
        return payload;
    }
    if(trimmed.contains(QLatin1Char(' ')))
    {
        payload.error = QStringLiteral("URI must not contain spaces; percent-encode them");
        return payload;
    }

    const quint8 prefix = bestPrefix(trimmed);
    const QByteArray message = uriRecord(trimmed, prefix);
    if(message.isEmpty())
    {
        payload.error = QStringLiteral("the URI record is too large for a Type 2 Tag");
        return payload;
    }
    payload.uri = trimmed;
    finishPayload(&payload, message, QStringLiteral("URI (%1)").arg(trimmed));
    return payload;
}

LaunchPayload buildText(const QString &text, const QString &language)
{
    LaunchPayload payload;
    if(text.isEmpty())
    {
        payload.error = QStringLiteral("text is empty");
        return payload;
    }
    QString lang = language;
    QString error;
    if(lang.isEmpty())
        lang = QStringLiteral("en");
    if(!validLanguage(lang, &error))
    {
        payload.error = error;
        return payload;
    }

    QByteArray recordPayload;
    recordPayload.append(static_cast<char>(0x80 | (lang.size() & 0x3F))); // UTF-8 flag
    recordPayload += lang.toLatin1();
    recordPayload += text.toUtf8();

    QList<RecordSpec> records;
    RecordSpec record;
    record.tnf = 0x01;
    record.type = QByteArrayLiteral("T");
    record.payload = recordPayload;
    records.append(record);

    const QByteArray message = buildMessage(records, &error);
    if(message.isEmpty())
    {
        payload.error = error;
        return payload;
    }
    finishPayload(&payload, message,
                  QStringLiteral("Text (%1, %2)").arg(lang, text.left(32)));
    return payload;
}

LaunchPayload buildMime(const QString &mimeType, const QByteArray &mimePayload)
{
    LaunchPayload payload;
    QString error;
    bool nonAscii = false;
    if(!validMimeType(mimeType, &error, &nonAscii))
    {
        payload.error = error;
        return payload;
    }
    if(nonAscii)
        payload.warnings << QStringLiteral(
            "MIME type contains non-ASCII bytes; it is preserved exactly, but "
            "some readers may not decode the record type.");

    QList<RecordSpec> records;
    RecordSpec record;
    record.tnf = 0x02;
    record.type = mimeType.toUtf8();
    record.payload = mimePayload;
    records.append(record);

    const QByteArray message = buildMessage(records, &error);
    if(message.isEmpty())
    {
        payload.error = error;
        return payload;
    }
    finishPayload(&payload, message,
                  QStringLiteral("MIME (%1, %2 bytes)").arg(mimeType)
                  .arg(mimePayload.size()));
    return payload;
}

LaunchPayload buildVCard(const QString &name, const QString &organization,
                         const QString &phone, const QString &email,
                         const QString &url, const QString &address)
{
    LaunchPayload payload;
    if(name.trimmed().isEmpty() && organization.trimmed().isEmpty()
            && phone.trimmed().isEmpty() && email.trimmed().isEmpty()
            && url.trimmed().isEmpty() && address.trimmed().isEmpty())
    {
        payload.error = QStringLiteral("vCard needs at least one populated field");
        return payload;
    }

    QString vcard;
    vcard += QStringLiteral("BEGIN:VCARD\r\n");
    vcard += QStringLiteral("VERSION:3.0\r\n");
    const QString displayName = vcardEscape(name.trimmed().isEmpty()
                                            ? organization : name);
    vcard += QStringLiteral("FN:") + displayName + QStringLiteral("\r\n");
    if(!organization.trimmed().isEmpty())
        vcard += QStringLiteral("ORG:") + vcardEscape(organization) + QStringLiteral("\r\n");
    if(!address.trimmed().isEmpty())
        vcard += QStringLiteral("ADR:;;") + vcardEscape(address) + QStringLiteral(";;;;\r\n");
    if(!phone.trimmed().isEmpty())
        vcard += QStringLiteral("TEL:") + vcardEscape(phone) + QStringLiteral("\r\n");
    if(!email.trimmed().isEmpty())
        vcard += QStringLiteral("EMAIL:") + vcardEscape(email) + QStringLiteral("\r\n");
    if(!url.trimmed().isEmpty())
        vcard += QStringLiteral("URL:") + vcardEscape(url) + QStringLiteral("\r\n");
    vcard += QStringLiteral("END:VCARD\r\n");

    QString error;
    if(!validMimeType(QStringLiteral("text/vcard"), &error, nullptr))
    {
        payload.error = error;
        return payload;
    }
    QList<RecordSpec> records;
    RecordSpec record;
    record.tnf = 0x02;
    record.type = QByteArrayLiteral("text/vcard");
    record.payload = vcard.toUtf8();
    records.append(record);

    const QByteArray message = buildMessage(records, &error);
    if(message.isEmpty())
    {
        payload.error = error;
        return payload;
    }
    finishPayload(&payload, message,
                  QStringLiteral("vCard (%1, %2 bytes)")
                  .arg(displayName).arg(record.payload.size()));
    return payload;
}

LaunchPayload buildWifi(const QString &ssid, const QString &password,
                        const QString &auth)
{
    LaunchPayload payload;
    const QByteArray ssidBytes = ssid.toUtf8();
    const QByteArray keyBytes = password.toUtf8();
    if(ssidBytes.isEmpty())
    {
        payload.error = QStringLiteral("Wi-Fi SSID is empty");
        return payload;
    }
    if(ssidBytes.size() > 32)
    {
        payload.error = QStringLiteral("Wi-Fi SSID must be at most 32 bytes in UTF-8");
        return payload;
    }
    if(keyBytes.size() > 63)
    {
        payload.error = QStringLiteral("Wi-Fi password must be at most 63 bytes");
        return payload;
    }

    quint16 authType = 0x0020; // WPA2-PSK
    quint16 encType = 0x000C;  // AES
    const QString mode = auth.trimmed().toUpper();
    bool open = false;
    if(mode.isEmpty() || mode == QLatin1String("WPA2")
            || mode == QLatin1String("WPA2-PSK")
            || mode == QLatin1String("WPA/WPA2"))
    {
        // NFC Tools uses 0x0022 (WPA/WPA2-PSK mixed) for its default WPA2
        // selection; 0x0020 is available for a WPA2-only network.
        authType = 0x0022;
    }
    else if(mode == QLatin1String("WPA2-ONLY"))
    {
        authType = 0x0020;
    }
    else if(mode == QLatin1String("WPA") || mode == QLatin1String("WPA-PSK"))
    {
        authType = 0x0002;
        encType = 0x0008; // TKIP
    }
    else if(mode == QLatin1String("OPEN") || mode == QLatin1String("NONE"))
    {
        authType = 0x0001;
        encType = 0x0001;
        open = true;
    }
    else
    {
        payload.error = QStringLiteral("Wi-Fi auth must be WPA2, WPA or OPEN");
        return payload;
    }

    QByteArray credential;
    credential += wscAttribute(0x1026, QByteArray(1, '\x01')); // network index
    credential += wscAttribute(0x1045, ssidBytes);             // SSID
    credential += wscAttribute(0x1003, wscU16(authType));      // auth type
    credential += wscAttribute(0x100F, wscU16(encType));       // encryption
    if(!open)
        credential += wscAttribute(0x1027, keyBytes);          // network key
    credential += wscAttribute(0x1020, QByteArray::fromHex("FFFFFFFFFFFF")); // MAC

    QByteArray wsc;
    wsc += wscAttribute(0x100E, credential);

    QString error;
    QList<RecordSpec> records;
    RecordSpec record;
    record.tnf = 0x02;
    record.type = QByteArrayLiteral("application/vnd.wfa.wsc");
    record.payload = wsc;
    records.append(record);
    const QByteArray message = buildMessage(records, &error);
    if(message.isEmpty())
    {
        payload.error = error;
        return payload;
    }
    finishPayload(&payload, message,
                  QStringLiteral("Wi-Fi WSC (%1)").arg(ssid));
    return payload;
}

LaunchPayload buildBluetooth(const QString &address)
{
    LaunchPayload payload;
    QByteArray mac;
    QString error;
    if(!parseMac(address, &mac, &error))
    {
        payload.error = error;
        return payload;
    }

    QByteArray oob;
    oob.append(static_cast<char>(0x08)); // OOB length, little-endian
    oob.append(static_cast<char>(0x00));
    for(int i = mac.size() - 1; i >= 0; i--) // BD_ADDR is little-endian
        oob.append(mac.at(i));

    QList<RecordSpec> records;
    RecordSpec record;
    record.tnf = 0x02;
    record.type = QByteArrayLiteral("application/vnd.bluetooth.ep.oob");
    record.payload = oob;
    records.append(record);
    const QByteArray message = buildMessage(records, &error);
    if(message.isEmpty())
    {
        payload.error = error;
        return payload;
    }
    finishPayload(&payload, message,
                  QStringLiteral("Bluetooth OOB (%1)").arg(address));
    return payload;
}

LaunchPayload buildOpenPrintTag(const QByteArray &payload64)
{
    LaunchPayload payload;
    if(payload64.size() != 64)
    {
        payload.error = QStringLiteral("OpenPrintTag payload must be exactly 64 bytes (%1 given)")
                .arg(payload64.size());
        return payload;
    }
    QString error;
    QList<RecordSpec> records;
    RecordSpec record;
    record.tnf = 0x02;
    record.type = QByteArrayLiteral("application/vnd.openprinttag");
    record.payload = payload64;
    records.append(record);
    const QByteArray message = buildMessage(records, &error);
    if(message.isEmpty())
    {
        payload.error = error;
        return payload;
    }
    finishPayload(&payload, message,
                  QStringLiteral("OpenPrintTag (%1 bytes)").arg(payload64.size()));
    return payload;
}

LaunchPayload buildExternal(const QString &externalType, const QByteArray &externalPayload)
{
    LaunchPayload payload;
    const QString type = externalType.trimmed();
    if(type.isEmpty())
    {
        payload.error = QStringLiteral("external type is empty");
        return payload;
    }
    if(!type.contains(QLatin1Char(':')))
    {
        payload.error = QStringLiteral("external type needs the form domain:service");
        return payload;
    }
    for(const QChar ch : type)
    {
        if(ch.unicode() < 0x21 || ch.unicode() > 0x7E)
        {
            payload.error = QStringLiteral(
                "external type must be printable US-ASCII without spaces");
            return payload;
        }
    }
    if(type.size() > 255)
    {
        payload.error = QStringLiteral("external type is longer than 255 bytes");
        return payload;
    }

    QString error;
    QList<RecordSpec> records;
    RecordSpec record;
    record.tnf = 0x04;
    record.type = type.toLatin1();
    record.payload = externalPayload;
    records.append(record);
    const QByteArray message = buildMessage(records, &error);
    if(message.isEmpty())
    {
        payload.error = error;
        return payload;
    }
    finishPayload(&payload, message,
                  QStringLiteral("External (%1, %2 bytes)")
                  .arg(type).arg(externalPayload.size()));
    return payload;
}

LaunchPayload buildAar(const QString &packageName)
{
    const QString package = packageName.trimmed();
    if(package.isEmpty())
    {
        LaunchPayload payload;
        payload.error = QStringLiteral("Android package name is empty");
        return payload;
    }
    for(const QChar ch : package)
    {
        if(ch.unicode() < 0x21 || ch.unicode() > 0x7E)
        {
            LaunchPayload payload;
            payload.error = QStringLiteral(
                "Android package name must be printable US-ASCII without spaces");
            return payload;
        }
    }
    return buildExternal(QStringLiteral("android.com:pkg"), package.toLatin1());
}

LaunchPayload buildLaunch(const LaunchRequest &request)
{
    LaunchPayload payload;
    switch(request.type)
    {
    case LaunchType::Url:
    {
        const QString url = request.primary.trimmed();
        if(url.isEmpty())
        {
            payload.error = QStringLiteral("URL is empty");
            return payload;
        }
        if(!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                && !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        {
            payload.error = QStringLiteral("URL must start with http:// or https://");
            return payload;
        }
        return buildUri(url);
    }

    case LaunchType::Phone:
    {
        QString error;
        if(!validPhone(request.primary, &error))
        {
            payload.error = error;
            return payload;
        }
        return buildUri(QStringLiteral("tel:") + digitsOnly(request.primary));
    }

    case LaunchType::Sms:
    {
        QString error;
        if(!validPhone(request.primary, &error))
        {
            payload.error = error;
            return payload;
        }
        QString uri = QStringLiteral("sms:") + digitsOnly(request.primary);
        if(!request.body.trimmed().isEmpty())
            uri += QStringLiteral("?body=") + percentEncode(request.body);
        return buildUri(uri);
    }

    case LaunchType::Email:
    {
        const QString address = request.primary.trimmed();
        static const QRegularExpression email(
                    QStringLiteral("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$"));
        if(!email.match(address).hasMatch())
        {
            payload.error = QStringLiteral("e-mail address is not valid");
            return payload;
        }
        QString uri = QStringLiteral("mailto:") + address;
        QStringList query;
        if(!request.subject.trimmed().isEmpty())
            query << QStringLiteral("subject=") + percentEncode(request.subject);
        if(!request.body.trimmed().isEmpty())
            query << QStringLiteral("body=") + percentEncode(request.body);
        if(!query.isEmpty())
            uri += QStringLiteral("?") + query.join(QLatin1Char('&'));
        return buildUri(uri);
    }

    case LaunchType::CustomUri:
        return buildUri(request.primary);

    case LaunchType::Text:
        return buildText(request.primary,
                         request.subject.trimmed().isEmpty()
                         ? QStringLiteral("en") : request.subject.trimmed());

    case LaunchType::Mime:
    {
        QByteArray bytes = request.body.toUtf8();
        if(!request.extra.trimmed().isEmpty())
        {
            QString error;
            if(!parseHexPayload(request.extra, &bytes, &error))
            {
                payload.error = error;
                return payload;
            }
        }
        return buildMime(request.primary.trimmed(), bytes);
    }

    case LaunchType::VCard:
        return buildVCard(request.primary, request.subject, request.body,
                          request.extra);

    case LaunchType::Wifi:
        return buildWifi(request.primary, request.subject, request.body);

    case LaunchType::Bluetooth:
        return buildBluetooth(request.primary);

    case LaunchType::OpenPrintTag:
    {
        QByteArray bytes;
        QString error;
        const QString hex = request.extra.trimmed().isEmpty()
                ? request.body : request.extra;
        if(!parseHexPayload(hex, &bytes, &error))
        {
            payload.error = error;
            return payload;
        }
        return buildOpenPrintTag(bytes);
    }

    case LaunchType::External:
    {
        QByteArray bytes = request.body.toUtf8();
        if(!request.extra.trimmed().isEmpty())
        {
            QString error;
            if(!parseHexPayload(request.extra, &bytes, &error))
            {
                payload.error = error;
                return payload;
            }
        }
        return buildExternal(request.primary, bytes);
    }

    default:
        payload.error = QStringLiteral("unsupported launch type");
        return payload;
    }
}

} // namespace Ndef

#include "ndefbuilder.h"

#include "ntagmodel.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace Ndef {
namespace {

QByteArray uriRecord(const QString &uri, quint8 prefixCode)
{
    const QString prefix = Ntag::uriPrefix(prefixCode);
    QByteArray rest;
    if(!prefix.isEmpty() && uri.startsWith(prefix))
        rest = uri.mid(prefix.size()).toUtf8();
    else
        rest = uri.toUtf8();

    QByteArray payload;
    payload.append(static_cast<char>(prefixCode));
    payload += rest;

    QByteArray record;
    if(payload.size() <= 255)
    {
        // MB=1, ME=1, SR=1, TNF=1 (well-known)
        record.append(static_cast<char>(0xD1));
        record.append(static_cast<char>(0x01));
        record.append('U');
        record.append(static_cast<char>(payload.size()));
    }
    else
    {
        // Long record: 4-byte payload length, SR=0
        record.append(static_cast<char>(0xC1));
        record.append(static_cast<char>(0x01));
        record.append('U');
        const quint32 size = static_cast<quint32>(payload.size());
        record.append(static_cast<char>((size >> 24) & 0xFF));
        record.append(static_cast<char>((size >> 16) & 0xFF));
        record.append(static_cast<char>((size >> 8) & 0xFF));
        record.append(static_cast<char>(size & 0xFF));
    }
    record += payload;
    return record;
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

} // namespace

QByteArray wrapTlv(const QByteArray &message)
{
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
    // TLV header + message + optional terminator
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
    payload.uri = trimmed;
    payload.message = uriRecord(trimmed, prefix);
    payload.tlv = wrapTlv(payload.message);
    payload.requiredBytes = tlvLength(payload.message);
    payload.userArea = payload.tlv;
    payload.userArea.append(static_cast<char>(0xFE));
    while(payload.userArea.size() % 4 != 0)
        payload.userArea.append('\0');
    payload.ok = true;
    return payload;
}

LaunchPayload buildLaunch(const LaunchRequest &request)
{
    LaunchPayload payload;
    switch(request.type)
    {
    case LaunchType::Wifi:
        payload.error = QStringLiteral(
                    "Wi-Fi records are not implemented: no verified payload "
                    "format is available, so the write is refused.");
        return payload;

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

    default:
        payload.error = QStringLiteral("unsupported launch type");
        return payload;
    }
}

} // namespace Ndef

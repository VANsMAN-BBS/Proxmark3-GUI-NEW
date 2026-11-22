#ifndef NDEFBUILDER_H
#define NDEFBUILDER_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * NDEF / Type 2 Tag TLV encoder for the Quick Launch page.
 *
 * Pure QtCore logic: no widgets, no client calls.  The produced bytes follow
 * the NFC Forum URI / Text / MIME / External record definitions and the
 * Type 2 Tag TLV layout (0x03 length message 0xFE).
 *
 * LaunchRequest field mapping (the GUI reuses the same four fields):
 *   Url          primary = URL
 *   Phone        primary = phone number
 *   Sms          primary = phone number, body = message
 *   Email        primary = address, subject = subject, body = message
 *   CustomUri    primary = any URI with a scheme
 *   Text         primary = text, subject = language code (default "en")
 *   Mime         primary = MIME type, body = UTF-8 payload,
 *                extra = optional hex payload (overrides body)
 *   VCard        primary = name, subject = organization,
 *                body = phone, extra = e-mail
 *   Wifi         primary = SSID, subject = password, body = auth
 *   Bluetooth    primary = MAC address
 *   OpenPrintTag primary = type (optional), body/extra = 64-byte hex payload
 *   External     primary = external type, body = UTF-8 payload,
 *                extra = optional hex payload (overrides body)
 */
namespace Ndef {

enum class LaunchType {
    Url,
    Phone,
    Sms,
    Email,
    CustomUri,
    Text,
    Mime,
    VCard,
    Wifi,
    Bluetooth,
    OpenPrintTag,
    External
};

struct LaunchRequest
{
    LaunchType type = LaunchType::Url;
    QString primary;
    QString subject;
    QString body;
    QString extra;
};

struct LaunchPayload
{
    bool ok = false;
    QString error;
    QStringList warnings;
    QString uri;          // final URI encoded in the record, empty for non-URI records
    QString summary;      // short human-readable record description
    QByteArray message;   // complete NDEF message
    QByteArray tlv;       // 0x03 length message
    QByteArray userArea;  // TLV + 0xFE terminator, padded to 4-byte pages
    int requiredBytes = 0; // bytes needed in the CC NDEF area
};

/** One generic NDEF record. */
struct RecordSpec
{
    quint8 tnf = 1;
    QByteArray type;
    QByteArray payload;
    QByteArray id;
};

/** Builds a URI record (TNF 1, type 'U') for an already valid URI. */
LaunchPayload buildUri(const QString &uri);

/** Builds a UTF-8 Text record (TNF 1, type 'T'). */
LaunchPayload buildText(const QString &text,
                        const QString &language = QStringLiteral("en"));

/** Builds a MIME record (TNF 2).  Non-ASCII MIME types are kept as-is but
 *  reported in `warnings`; the payload is never changed. */
LaunchPayload buildMime(const QString &mimeType, const QByteArray &payload);

/** Builds a vCard 3.0 MIME record (text/vcard) with CRLF line endings. */
LaunchPayload buildVCard(const QString &name,
                         const QString &organization = QString(),
                         const QString &phone = QString(),
                         const QString &email = QString(),
                         const QString &url = QString(),
                         const QString &address = QString());

/** Builds a Wi-Fi Simple Configuration MIME record
 *  (application/vnd.wfa.wsc).  auth is WPA2 (default), WPA or OPEN. */
LaunchPayload buildWifi(const QString &ssid, const QString &password,
                        const QString &auth = QStringLiteral("WPA2"));

/** Builds a Bluetooth Extended Inquiry Response OOB MIME record. */
LaunchPayload buildBluetooth(const QString &address);

/** Builds an OpenPrintTag MIME record with a 64-byte raw payload. */
LaunchPayload buildOpenPrintTag(const QByteArray &payload64);

/** Builds an NFC Forum External record (TNF 4). */
LaunchPayload buildExternal(const QString &externalType, const QByteArray &payload);

/** Builds an Android Application Record (TNF 4, android.com:pkg). */
LaunchPayload buildAar(const QString &packageName);

/** Builds a launch payload for one of the supported launch types. */
LaunchPayload buildLaunch(const LaunchRequest &request);

/** Builds a multi-record NDEF message with correct MB/ME/SR/IL flags. */
QByteArray buildMessage(const QList<RecordSpec> &records, QString *error = nullptr);

/** Wraps an NDEF message into a Type 2 Tag TLV (0x03 ... 0xFE).
 *  Returns an empty array when the message is too large for the 0xFF form. */
QByteArray wrapTlv(const QByteArray &message);

/** Total bytes the TLV occupies, including the terminator. */
int tlvLength(const QByteArray &message);

} // namespace Ndef

#endif // NDEFBUILDER_H

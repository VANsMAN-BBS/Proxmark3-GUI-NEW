#ifndef NDEFBUILDER_H
#define NDEFBUILDER_H

#include <QByteArray>
#include <QString>

/**
 * NDEF / Type 2 Tag TLV encoder for the Quick Launch page.
 *
 * Pure QtCore logic: no widgets, no client calls.  The produced bytes follow
 * the NFC Forum URI Record Type Definition and the Type 2 Tag TLV layout
 * (0x03 length message 0xFE).
 */
namespace Ndef {

enum class LaunchType {
    Url,
    Phone,
    Sms,
    Email,
    CustomUri,
    Wifi // deliberately unsupported: no reliable, verified payload
};

struct LaunchRequest
{
    LaunchType type = LaunchType::Url;
    QString primary;    // URL / phone number / e-mail address / URI
    QString subject;    // e-mail subject
    QString body;       // SMS or e-mail body
};

struct LaunchPayload
{
    bool ok = false;
    QString error;
    QString uri;          // final URI encoded in the record
    QByteArray message;   // complete NDEF message
    QByteArray tlv;       // 0x03 length message
    QByteArray userArea;  // TLV + 0xFE terminator, padded to 4-byte pages
    int requiredBytes = 0; // bytes needed in the CC NDEF area
};

/** Builds a URI record (TNF 1, type 'U') for an already valid URI. */
LaunchPayload buildUri(const QString &uri);

/** Builds a launch payload for one of the supported launch types. */
LaunchPayload buildLaunch(const LaunchRequest &request);

/** Wraps an NDEF message into a Type 2 Tag TLV (0x03 ... 0xFE). */
QByteArray wrapTlv(const QByteArray &message);

/** Total bytes the TLV occupies, including the terminator. */
int tlvLength(const QByteArray &message);

} // namespace Ndef

#endif // NDEFBUILDER_H

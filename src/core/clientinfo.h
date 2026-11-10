#ifndef CLIENTINFO_H
#define CLIENTINFO_H

#include <QString>
#include <QMetaType>

/**
 * Information reported by the Proxmark3 client after connecting.
 *
 * Only fields that can be parsed from the real client output are filled in.
 * Empty fields must be treated as "unknown", never as a guessed value.
 */
struct ClientInfo
{
    bool connected = false;
    bool isIceman = false;

    QString clientVersion;    // e.g. "Iceman/master/v4.23346-suspect"
    QString firmwareVersion;  // e.g. "Iceman/master/v4.23346-suspect"
    QString bootromVersion;
    QString model;            // e.g. "PM3 GENERIC"
    QString hardware;         // e.g. "AT91SAM7S256 Rev D"
    QString rawVersionOutput;

    bool hasFirmwareVersion() const { return !firmwareVersion.isEmpty(); }
    bool hasModel() const { return !model.isEmpty(); }
};

Q_DECLARE_METATYPE(ClientInfo)

#endif // CLIENTINFO_H

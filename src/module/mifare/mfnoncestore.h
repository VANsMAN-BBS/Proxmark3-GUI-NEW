#ifndef MFNONCESTORE_H
#define MFNONCESTORE_H

#include "mfgen3types.h"

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVector>

/**
 * Nonce evidence / key / dump (de)serialisation for the Gen3 pipeline.
 *
 * Two evidence formats are deliberately kept apart and never guessed from the
 * file extension: the header (magic or FileType) decides the parser, and every
 * field is validated with a concrete error message.
 *
 *   PM3 client  : {"FileType":"fm11rf08s_nonces[_with_data]", nt:{...}, ...}
 *   PM3GUI      : {"magic":"PM3GUI_FM11_NONCES_V1", nt:[[..],[..]], ...}
 */
namespace mfgen3
{
namespace NonceStore
{

/** Parsed PM3-client evidence plus the optional block image. */
struct Pm3Evidence
{
    NonceSet set;
    QByteArray dump;      ///< 1024 bytes when the file carried "blocks"
    bool hasDump = false;
};

/**
 * Parse a PM3 client nonce JSON (`hf-mf-<UID>-nonces[_with_data].json`).
 * Returns false and fills error with "<field>: <reason>" on any violation.
 */
bool loadPm3File(const QString &path, Pm3Evidence *out, QString *error);

/** Serialise a nonce set (and optional 1024-byte swap) as PM3 client JSON. */
bool savePm3File(const QString &path, const NonceSet &set,
                 const QByteArray &dump, bool withData, QString *error);

/** Parse the PM3GUI archive format (strong `magic` check). */
bool loadPm3GuiFile(const QString &path, NonceSet *out, QString *error);

/** Serialise the PM3GUI archive format. */
bool savePm3GuiFile(const QString &path, const NonceSet &set, QString *error);

/** Validate a raw nonce set (dimensions / ranges); empty result when valid. */
QString validateNonceSet(const NonceSet &set);

/** Which format a file claims to be, from its header only. */
QString detectFormat(const QString &path);

/** "A1B2C3D4E5F6" -> 0xA1B2C3D4E5F6 (12 hex, case-insensitive), 0 on failure. */
quint64 parseKey(const QString &text, bool *ok = nullptr);
/** 48-bit key to 12 upper-case hex digits. */
QString formatKey(quint64 key);

/** Write a `.dic` dictionary (one 12-hex key per line, upper case). */
bool writeDicFile(const QString &path, const QVector<quint64> &keys, QString *error);

/** Parse a key file with `[0-9a-fA-F]{12}` per line (tolerates any noise). */
QVector<quint64> readKeyList(const QString &path);

/** 192-byte key.bin image: 16x6 bytes KeyA, then 16x6 bytes KeyB. */
QByteArray buildKeyBin(const CrackResult &result);
bool loadKeyBin(const QString &path, QString keys[kSectors][2]);

/** Default keys tried by the "known key" read path. */
QVector<quint64> defaultKeys();

} // namespace NonceStore
} // namespace mfgen3

#endif // MFNONCESTORE_H

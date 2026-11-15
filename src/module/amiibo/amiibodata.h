#ifndef AMIIBODATA_H
#define AMIIBODATA_H

#include <QByteArray>
#include <QString>

/**
 * Amiibo data classification and lossless conversions.
 *
 * Two modes are kept strictly apart:
 *   RawBin  - the bytes the user imported (540/1024/2048 raw image, or a
 *             56-byte-header dump file).  These bytes are authoritative for
 *             simulation and are never regenerated from parsed fields.
 *   Image   - page payload without the dump header, used for display/write.
 *
 * No widgets, no client calls: testable with synthetic bytes.
 */
namespace Amiibo {

enum class Kind {
    Invalid,
    RawImage,   // headerless page payload (180/540/924/1024/2048 bytes)
    DumpFile    // 56-byte header + 45/135/231 pages (bin dump)
};

struct Info
{
    Kind kind = Kind::Invalid;
    int pages = 0;              // pages in the payload
    int payloadOffset = 0;      // 0 for RawImage, 56 for DumpFile
    QByteArray payload;         // pages only, original order
    QByteArray header;          // dump header when kind == DumpFile
    QString error;
};

/** Classifies a file and extracts the page payload without altering it. */
Info classify(const QByteArray &file);

/** SHA-256 of the given bytes, lowercase hex. */
QString sha256(const QByteArray &data);

/** Wraps a page payload into the dump format expected by restore/eload. */
QByteArray makeDump(const QByteArray &payload, int pages,
                    const QByteArray &version = QByteArray());

/**
 * The Windows client opens file names as narrow strings, so non-ASCII paths
 * fail even when the file exists.  The caller copies the identical bytes to
 * an ASCII path; this only reports whether that adaptation is needed.
 */
bool requiresAsciiClientPath(const QString &path);

} // namespace Amiibo

#endif // AMIIBODATA_H

#ifndef MFIMAGE_H
#define MFIMAGE_H

#include <QByteArray>
#include <QDateTime>
#include <QStringList>

/**
 * Card-image helpers used by the MIFARE page and its UI tests.
 *
 * A MIFARE Mini/1K/2K/4K image is `cardType.block_size` blocks of 16 bytes
 * (320 / 1024 / 2048 / 4096 bytes), not a fixed 1 KB buffer.  Keeping the
 * serialisation here avoids the "backup silently stops at block 64" trap and
 * lets the tests cover all layouts without a widget or a device.
 */
namespace mfimage
{

/** Serialise up to `blocks` 32-hex-digit rows into a card image.
 *  `hasData` reports whether at least one valid row was written. */
inline QByteArray imageFromRows(const QStringList &rows, int blocks, bool *hasData = nullptr)
{
    QByteArray image(blocks > 0 ? blocks * 16 : 0, '\0');
    bool any = false;
    const int limit = qMin(blocks, rows.size());
    for(int block = 0; block < limit; ++block)
    {
        const QString hex = rows.at(block).trimmed().toUpper();
        if(hex.size() != 32)
            continue;
        bool valid = true;
        for(int i = 0; i < 32; ++i)
        {
            const QChar ch = hex.at(i);
            if(!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
                 (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))))
            {
                valid = false;
                break;
            }
        }
        if(!valid)
            continue;
        any = true;
        for(int i = 0; i < 16; ++i)
        {
            bool ok = false;
            const uint value = hex.mid(i * 2, 2).toUInt(&ok, 16);
            if(ok)
                image[block * 16 + i] = char(value);
        }
    }
    if(hasData)
        *hasData = any;
    return image;
}

/** `backup_<uid>_<card type>_<bytes>B_<stamp>.bin` - the size is visible. */
inline QString backupFileName(const QString &uid, const QString &cardType, int blocks,
                              const QDateTime &stamp)
{
    const QString safeUid = uid.isEmpty() ? QStringLiteral("card") : uid;
    const QString safeType = cardType.isEmpty() ? QStringLiteral("unknown") : cardType;
    return QStringLiteral("backup_%1_%2_%3B_%4.bin")
           .arg(safeUid, safeType)
           .arg(blocks > 0 ? blocks * 16 : 0)
           .arg(stamp.toString(QStringLiteral("yyyyMMdd_HHmmss")));
}

} // namespace mfimage

#endif // MFIMAGE_H
